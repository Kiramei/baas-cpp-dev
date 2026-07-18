#include "script/host/ConfigHost.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace baas::script::host {
namespace {

using runtime::HostArguments;
using runtime::HostCallContext;
using runtime::HostEffectState;
using runtime::HostErrorCode;
using runtime::HostResult;
using runtime::HostValue;
using runtime::HostValueType;
using runtime::JsonArray;
using runtime::JsonKind;
using runtime::JsonObject;
using runtime::JsonValue;

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept
{
    for (std::size_t offset{}; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        std::size_t width{};
        std::uint32_t scalar{};
        if (first <= 0x7FU) { width = 1; scalar = first; }
        else if (first >= 0xC2U && first <= 0xDFU) { width = 2; scalar = first & 0x1FU; }
        else if (first >= 0xE0U && first <= 0xEFU) { width = 3; scalar = first & 0x0FU; }
        else if (first >= 0xF0U && first <= 0xF4U) { width = 4; scalar = first & 0x07U; }
        else return false;
        if (width > value.size() - offset) return false;
        for (std::size_t index = 1; index < width; ++index) {
            const auto next = static_cast<unsigned char>(value[offset + index]);
            if ((next & 0xC0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (next & 0x3FU);
        }
        if ((width == 2 && scalar < 0x80U) ||
            (width == 3 && scalar < 0x800U) ||
            (width == 4 && scalar < 0x10000U) || scalar > 0x10FFFFU ||
            (scalar >= 0xD800U && scalar <= 0xDFFFU)) return false;
        offset += width;
    }
    return true;
}

[[nodiscard]] JsonValue detail(const char* key, const char* value)
{
    return JsonValue(JsonObject{{key, JsonValue(value)}});
}

[[nodiscard]] HostResult failure(
    const HostErrorCode code, std::string message, const bool retryable = false,
    const HostEffectState effect = HostEffectState::NotStarted,
    std::optional<JsonValue> details = std::nullopt) noexcept
{
    return HostResult::failure(
        {code, std::move(message), retryable, effect, std::move(details)});
}

[[nodiscard]] HostResult invalid(std::string message)
{
    return failure(HostErrorCode::InvalidArgument, std::move(message));
}

[[nodiscard]] HostResult budget(std::string message, const char* scope)
{
    return failure(HostErrorCode::BudgetExceeded, std::move(message), false,
                   HostEffectState::NotStarted, detail("budget_scope", scope));
}

[[nodiscard]] HostResult cancelled(const HostEffectState effect = HostEffectState::NotStarted)
{
    return failure(HostErrorCode::Cancelled, "config transaction cancelled", false, effect);
}

[[nodiscard]] HostResult deadline(const HostEffectState effect = HostEffectState::NotStarted)
{
    return failure(HostErrorCode::DeadlineExceeded,
                   "config transaction deadline exceeded", false, effect,
                   detail("deadline_scope", "context"));
}

struct JsonMetrics final {
    std::size_t nodes{};
    std::size_t string_bytes{};
    std::size_t total_bytes{};
    std::size_t work{};
};

enum class JsonValidationError {
    None,
    InvalidUtf8,
    NonFinite,
    DuplicateKey,
    Depth,
    Nodes,
    Strings,
    Bytes,
    Work,
};

[[nodiscard]] JsonValidationError checked_add(
    std::size_t& target, const std::size_t amount, const std::size_t maximum,
    const JsonValidationError error) noexcept
{
    if (amount > maximum || target > maximum - amount) return error;
    target += amount;
    return JsonValidationError::None;
}

[[nodiscard]] JsonValidationError checked_add_product(
    std::size_t& target, const std::size_t count, const std::size_t width,
    const std::size_t maximum, const JsonValidationError error) noexcept
{
    if (width != 0 && count > maximum / width) return error;
    return checked_add(target, count * width, maximum, error);
}

[[nodiscard]] JsonValidationError validate_json(
    const JsonValue& root, const runtime::JsonBridgeLimits& limits,
    JsonMetrics& metrics) noexcept
{
    struct Pending final { const JsonValue* value; std::size_t depth; };
    try {
        std::vector<Pending> pending{{&root, 1}};
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (current.depth > std::min<std::size_t>(limits.max_depth, 1024))
                return JsonValidationError::Depth;
            if (const auto error = checked_add(
                    metrics.nodes, 1, limits.max_nodes,
                    JsonValidationError::Nodes); error != JsonValidationError::None)
                return error;
            if (const auto error = checked_add(
                    metrics.work, 1, limits.max_work,
                    JsonValidationError::Work); error != JsonValidationError::None)
                return error;
            if (const auto error = checked_add(
                    metrics.total_bytes, 1, limits.max_total_bytes,
                    JsonValidationError::Bytes); error != JsonValidationError::None)
                return error;

            switch (current.value->kind()) {
                case JsonKind::Null: break;
                case JsonKind::Boolean:
                    if (const auto error = checked_add(
                            metrics.total_bytes, 1, limits.max_total_bytes,
                            JsonValidationError::Bytes); error != JsonValidationError::None)
                        return error;
                    break;
                case JsonKind::Integer:
                    if (const auto error = checked_add(
                            metrics.total_bytes, sizeof(std::int64_t),
                            limits.max_total_bytes, JsonValidationError::Bytes);
                        error != JsonValidationError::None) return error;
                    break;
                case JsonKind::Float:
                    if (!std::isfinite(std::get<double>(current.value->value())))
                        return JsonValidationError::NonFinite;
                    if (const auto error = checked_add(
                            metrics.total_bytes, sizeof(double), limits.max_total_bytes,
                            JsonValidationError::Bytes); error != JsonValidationError::None)
                        return error;
                    break;
                case JsonKind::String: {
                    const auto& string = std::get<std::string>(current.value->value());
                    if (!valid_utf8(string)) return JsonValidationError::InvalidUtf8;
                    if (const auto error = checked_add(
                            metrics.string_bytes, string.size(), limits.max_string_bytes,
                            JsonValidationError::Strings); error != JsonValidationError::None)
                        return error;
                    if (const auto error = checked_add(
                            metrics.total_bytes, string.size(), limits.max_total_bytes,
                            JsonValidationError::Bytes); error != JsonValidationError::None)
                        return error;
                    break;
                }
                case JsonKind::Array: {
                    const auto& values = std::get<JsonArray>(current.value->value());
                    if (const auto error = checked_add(
                            metrics.work, values.size(), limits.max_work,
                            JsonValidationError::Work); error != JsonValidationError::None)
                        return error;
                    if (const auto error = checked_add_product(
                            metrics.total_bytes, values.size(), sizeof(std::size_t),
                            limits.max_total_bytes, JsonValidationError::Bytes);
                        error != JsonValidationError::None) return error;
                    for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator)
                        pending.push_back({&*iterator, current.depth + 1});
                    break;
                }
                case JsonKind::Object: {
                    const auto& entries = std::get<JsonObject>(current.value->value());
                    std::set<std::string_view> keys;
                    if (const auto error = checked_add(
                            metrics.work, entries.size(), limits.max_work,
                            JsonValidationError::Work); error != JsonValidationError::None)
                        return error;
                    if (const auto error = checked_add_product(
                            metrics.total_bytes, entries.size(), sizeof(std::size_t),
                            limits.max_total_bytes, JsonValidationError::Bytes);
                        error != JsonValidationError::None) return error;
                    for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
                        if (!valid_utf8(iterator->first))
                            return JsonValidationError::InvalidUtf8;
                        if (!keys.insert(iterator->first).second)
                            return JsonValidationError::DuplicateKey;
                        if (const auto error = checked_add(
                                metrics.string_bytes, iterator->first.size(),
                                limits.max_string_bytes, JsonValidationError::Strings);
                            error != JsonValidationError::None) return error;
                        if (const auto error = checked_add(
                                metrics.total_bytes, iterator->first.size(),
                                limits.max_total_bytes, JsonValidationError::Bytes);
                            error != JsonValidationError::None) return error;
                        pending.push_back({&iterator->second, current.depth + 1});
                    }
                    break;
                }
            }
        }
    } catch (...) {
        return JsonValidationError::Work;
    }
    return JsonValidationError::None;
}

[[nodiscard]] bool is_limit_error(const JsonValidationError error) noexcept
{
    return error == JsonValidationError::Depth || error == JsonValidationError::Nodes ||
        error == JsonValidationError::Strings || error == JsonValidationError::Bytes ||
        error == JsonValidationError::Work;
}

[[nodiscard]] std::optional<std::vector<std::string>> decode_pointer(
    const std::string_view pointer, const ConfigHostLimits& limits) noexcept
{
    if (pointer.empty() || pointer.front() != '/' || pointer.size() > limits.max_path_bytes ||
        !valid_utf8(pointer)) return std::nullopt;
    try {
        std::vector<std::string> result;
        std::size_t offset = 1;
        for (;;) {
            if (result.size() >= limits.max_path_segments) return std::nullopt;
            const auto end = pointer.find('/', offset);
            const auto encoded = pointer.substr(
                offset, end == std::string_view::npos ? pointer.size() - offset : end - offset);
            if (encoded.empty()) return std::nullopt;
            std::string decoded;
            decoded.reserve(encoded.size());
            for (std::size_t index{}; index < encoded.size(); ++index) {
                const auto character = encoded[index];
                if (character != '~') {
                    decoded.push_back(character);
                    continue;
                }
                if (index + 1 >= encoded.size()) return std::nullopt;
                const auto escape = encoded[++index];
                if (escape == '0') decoded.push_back('~');
                else if (escape == '1') decoded.push_back('/');
                else return std::nullopt;
            }
            if (decoded.empty() || !valid_utf8(decoded)) return std::nullopt;
            result.push_back(std::move(decoded));
            if (end == std::string_view::npos) break;
            offset = end + 1;
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool path_prefix(
    const std::vector<std::string>& left,
    const std::vector<std::string>& right) noexcept
{
    return left.size() <= right.size() &&
        std::equal(left.begin(), left.end(), right.begin());
}

[[nodiscard]] const JsonValue* find_path(
    const JsonObject& root, const std::vector<std::string>& path) noexcept
{
    const JsonObject* object = &root;
    const JsonValue* value{};
    for (std::size_t index{}; index < path.size(); ++index) {
        const auto found = std::find_if(
            object->begin(), object->end(), [&](const auto& entry) {
                return entry.first == path[index];
            });
        if (found == object->end()) return nullptr;
        value = &found->second;
        if (index + 1 == path.size()) return value;
        if (value->kind() != JsonKind::Object) return nullptr;
        object = &std::get<JsonObject>(value->value());
    }
    return value;
}

[[nodiscard]] JsonValue commit_value(const ConfigCommit& commit)
{
    return JsonValue(JsonObject{
        {"revision", JsonValue(commit.revision)},
        {"snapshot_id", JsonValue(commit.snapshot_id)}});
}

[[nodiscard]] bool valid_effect(const HostEffectState state) noexcept
{
    return state >= HostEffectState::NotStarted && state <= HostEffectState::Unknown;
}

}  // namespace

struct ConfigHost::Impl final {
    Impl(std::shared_ptr<const ConfigHostSnapshot> snapshot_value,
         std::shared_ptr<ConfigHostPort> port_value,
         const ConfigHostLimits limits_value)
        : snapshot(std::move(snapshot_value)), port(std::move(port_value)), limits(limits_value) {}

    [[nodiscard]] HostResult snapshot_result()
    {
        const auto used = read_operations.fetch_add(1, std::memory_order_acq_rel);
        if (used >= limits.max_read_operations) {
            read_operations.fetch_sub(1, std::memory_order_acq_rel);
            return budget("config read operation budget exceeded", "host_operation");
        }
        snapshot_reads.fetch_add(1, std::memory_order_relaxed);
        return HostResult::success(HostValue(commit_value(
            {snapshot->revision, snapshot->identity.snapshot_id})));
    }

    [[nodiscard]] HostResult get(const HostArguments& arguments)
    {
        if (arguments.size() != 2 || !arguments[0] ||
            arguments[0]->type() != HostValueType::String ||
            (arguments[1] && arguments[1]->type() != HostValueType::Json))
            return invalid("invalid config get arguments");
        const auto path = decode_pointer(
            std::get<std::string>(arguments[0]->storage()), limits);
        if (!path) return invalid("config path is not a canonical JSON Pointer");

        const auto operation = read_operations.fetch_add(1, std::memory_order_acq_rel);
        if (operation >= limits.max_read_operations) {
            read_operations.fetch_sub(1, std::memory_order_acq_rel);
            return budget("config read operation budget exceeded", "host_operation");
        }
        value_reads.fetch_add(1, std::memory_order_relaxed);
        const JsonValue* selected = find_path(snapshot->values, *path);
        JsonValue output = selected ? *selected :
            (arguments[1] ? std::get<JsonValue>(arguments[1]->storage()) : JsonValue());
        JsonMetrics metrics;
        const auto validation = validate_json(output, limits.json, metrics);
        if (validation != JsonValidationError::None) {
            value_reads.fetch_sub(1, std::memory_order_acq_rel);
            read_operations.fetch_sub(1, std::memory_order_acq_rel);
            return is_limit_error(validation)
                ? budget("config read byte budget exceeded", "external_memory")
                : failure(HostErrorCode::Internal,
                          "pinned config snapshot contains invalid JSON");
        }
        auto used = read_bytes.load(std::memory_order_relaxed);
        for (;;) {
            if (metrics.total_bytes > limits.max_total_read_bytes -
                    std::min(used, limits.max_total_read_bytes)) {
                value_reads.fetch_sub(1, std::memory_order_acq_rel);
                read_operations.fetch_sub(1, std::memory_order_acq_rel);
                return budget("config aggregate read byte budget exceeded", "external_memory");
            }
            if (read_bytes.compare_exchange_weak(
                    used, used + metrics.total_bytes,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) break;
        }
        return HostResult::success(HostValue(std::move(output)));
    }

    [[nodiscard]] HostResult transact(
        const HostCallContext& context, const HostArguments& arguments)
    {
        if (arguments.size() != 2 || !arguments[0] || !arguments[1] ||
            arguments[0]->type() != HostValueType::Integer ||
            arguments[1]->type() != HostValueType::Json ||
            std::get<JsonValue>(arguments[1]->storage()).kind() != JsonKind::Object)
            return invalid("invalid config transact arguments");
        const auto expected = std::get<std::int64_t>(arguments[0]->storage());
        if (expected < 0) return invalid("config expected revision must be non-negative");
        const auto& entries = std::get<JsonObject>(
            std::get<JsonValue>(arguments[1]->storage()).value());
        if (entries.empty() || entries.size() > limits.max_patch_entries)
            return invalid("config patch entry count is invalid");

        const auto attempt = transaction_attempts.fetch_add(1, std::memory_order_acq_rel);
        if (attempt >= limits.max_write_operations) {
            transaction_attempts.fetch_sub(1, std::memory_order_acq_rel);
            return budget("config write operation budget exceeded", "host_operation");
        }
        if (context.deadline_exceeded()) return deadline();
        if (context.cancelled()) return cancelled();

        std::vector<ConfigPatchEntry> patch;
        try {
            patch.reserve(entries.size());
            std::size_t checked{};
            std::size_t patch_bytes{};
            for (const auto& [pointer, value] : entries) {
                if (++checked % limits.cooperative_check_interval == 0) {
                    if (context.deadline_exceeded()) return deadline();
                    if (context.cancelled()) return cancelled();
                }
                auto path = decode_pointer(pointer, limits);
                if (!path) return invalid("config patch path is not canonical");
                JsonMetrics metrics;
                const auto validation = validate_json(value, limits.json, metrics);
                if (validation != JsonValidationError::None)
                    return is_limit_error(validation)
                        ? budget("config patch JSON budget exceeded", "external_memory")
                        : invalid("config patch contains invalid JSON");
                if (pointer.size() > limits.max_total_write_bytes ||
                    metrics.total_bytes > limits.max_total_write_bytes - pointer.size() ||
                    pointer.size() + metrics.total_bytes >
                        limits.max_total_write_bytes -
                            std::min(patch_bytes, limits.max_total_write_bytes))
                    return budget("config patch byte budget exceeded", "external_memory");
                patch_bytes += pointer.size() + metrics.total_bytes;
                patch.push_back({std::move(*path), value});
            }
            std::sort(patch.begin(), patch.end(), [](const auto& left, const auto& right) {
                return left.path < right.path;
            });
            for (std::size_t index = 1; index < patch.size(); ++index) {
                if (path_prefix(patch[index - 1].path, patch[index].path))
                    return invalid("config patch paths overlap");
            }
            auto used = write_bytes.load(std::memory_order_relaxed);
            for (;;) {
                if (patch_bytes > limits.max_total_write_bytes -
                        std::min(used, limits.max_total_write_bytes))
                    return budget("config aggregate write byte budget exceeded", "external_memory");
                if (write_bytes.compare_exchange_weak(
                        used, used + patch_bytes,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) break;
            }
        } catch (...) {
            return failure(HostErrorCode::Internal,
                           "config patch validation failed internally");
        }

        if (context.deadline_exceeded()) return deadline();
        if (context.cancelled()) return cancelled();
        ConfigPortTransactionResult result;
        try {
            result = port->transact(
                {expected, std::move(patch), context.cancellation});
        } catch (...) {
            return failure(HostErrorCode::Internal,
                           "config persistence adapter failed internally");
        }
        if (const auto* commit = std::get_if<ConfigCommit>(&result)) {
            if (commit->revision <= expected || commit->revision < 0 ||
                commit->snapshot_id.empty() ||
                commit->snapshot_id.size() > limits.max_identity_bytes ||
                !valid_utf8(commit->snapshot_id) ||
                commit->snapshot_id == snapshot->identity.snapshot_id)
                return failure(HostErrorCode::Internal,
                               "config persistence adapter returned an invalid commit");
            transaction_commits.fetch_add(1, std::memory_order_relaxed);
            return HostResult::success(HostValue(commit_value(*commit)));
        }
        const auto error = std::get<ConfigPortError>(result);
        if (!valid_effect(error.effect_state))
            return failure(HostErrorCode::Internal,
                           "config persistence adapter returned an invalid effect state");
        switch (error.code) {
            case ConfigPortErrorCode::Conflict:
                transaction_conflicts.fetch_add(1, std::memory_order_relaxed);
                return failure(HostErrorCode::ConfigConflict,
                               "config revision conflict", false,
                               HostEffectState::NotStarted);
            case ConfigPortErrorCode::InvalidPatch:
                return failure(HostErrorCode::InvalidArgument,
                               "config patch failed application validation", false,
                               HostEffectState::NotStarted);
            case ConfigPortErrorCode::Cancelled:
                return cancelled(error.effect_state);
            case ConfigPortErrorCode::DeadlineExceeded:
                return deadline(error.effect_state);
            case ConfigPortErrorCode::Unavailable:
                return failure(HostErrorCode::Unavailable,
                               "config persistence is unavailable",
                               error.retryable, error.effect_state);
            case ConfigPortErrorCode::Internal:
                return failure(HostErrorCode::Internal,
                               "config persistence failed internally", false,
                               error.effect_state);
        }
        return failure(HostErrorCode::Internal,
                       "config persistence returned an unknown status");
    }

    std::shared_ptr<const ConfigHostSnapshot> snapshot;
    std::shared_ptr<ConfigHostPort> port;
    ConfigHostLimits limits;
    std::atomic<std::size_t> read_operations{};
    std::atomic<std::size_t> snapshot_reads{};
    std::atomic<std::size_t> value_reads{};
    std::atomic<std::size_t> read_bytes{};
    std::atomic<std::size_t> transaction_attempts{};
    std::atomic<std::size_t> transaction_commits{};
    std::atomic<std::size_t> transaction_conflicts{};
    std::atomic<std::size_t> write_bytes{};
};

ConfigHost::ConfigHost(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
ConfigHost::~ConfigHost() = default;

const std::shared_ptr<const ConfigHostSnapshot>&
ConfigHost::pinned_snapshot() const noexcept { return impl_->snapshot; }

const std::shared_ptr<ConfigHostPort>& ConfigHost::port() const noexcept
{
    return impl_->port;
}

ConfigHostStats ConfigHost::stats() const noexcept
{
    return {
        impl_->snapshot_reads.load(std::memory_order_relaxed),
        impl_->value_reads.load(std::memory_order_relaxed),
        impl_->read_bytes.load(std::memory_order_relaxed),
        impl_->transaction_attempts.load(std::memory_order_relaxed),
        impl_->transaction_commits.load(std::memory_order_relaxed),
        impl_->transaction_conflicts.load(std::memory_order_relaxed),
        impl_->write_bytes.load(std::memory_order_relaxed)};
}

ConfigHostRuntime make_config_host_runtime(
    std::shared_ptr<const ConfigHostSnapshot> snapshot,
    std::shared_ptr<ConfigHostPort> port,
    const ConfigHostLimits limits)
{
    const auto valid_limits = limits.max_identity_bytes != 0 &&
        limits.max_path_bytes != 0 && limits.max_path_segments != 0 &&
        limits.max_patch_entries != 0 && limits.max_read_operations != 0 &&
        limits.max_write_operations != 0 && limits.max_total_read_bytes != 0 &&
        limits.max_total_write_bytes != 0 &&
        limits.cooperative_check_interval != 0 && limits.json.max_depth != 0 &&
        limits.json.max_nodes != 0 && limits.json.max_string_bytes != 0 &&
        limits.json.max_total_bytes != 0 && limits.json.max_work != 0;
    if (!valid_limits)
        throw std::invalid_argument("config Host limits must be non-zero");
    if (!snapshot || !port)
        throw std::invalid_argument("config Host snapshot or port is absent");
    const auto config_id_bytes = snapshot->identity.config_id.size();
    const auto snapshot_id_bytes = snapshot->identity.snapshot_id.size();
    const auto identity_overflow = config_id_bytes > limits.max_identity_bytes ||
        snapshot_id_bytes > limits.max_identity_bytes -
            std::min(config_id_bytes, limits.max_identity_bytes);
    if (snapshot->identity.config_id.empty() ||
        snapshot->identity.snapshot_id.empty() || snapshot->revision < 0 ||
        identity_overflow ||
        !valid_utf8(snapshot->identity.config_id) ||
        !valid_utf8(snapshot->identity.snapshot_id) ||
        port->identity() != snapshot->identity)
        throw std::invalid_argument("config Host identity is invalid or mismatched");
    JsonMetrics metrics;
    if (validate_json(JsonValue(snapshot->values), limits.json, metrics) !=
        JsonValidationError::None)
        throw std::invalid_argument("config Host snapshot is not bounded JSON");

    auto host = std::shared_ptr<ConfigHost>(new ConfigHost(
        std::make_unique<ConfigHost::Impl>(snapshot, port, limits)));
    runtime::SynchronousNativeBinding snapshot_binding{
        "host.config.snapshot.v1",
        {{}, HostValueType::Json, "config_read_operations",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [host](const HostCallContext&, const HostArguments& arguments) {
            if (!arguments.empty()) return invalid("config snapshot takes no arguments");
            return host->impl_->snapshot_result();
        }};
    runtime::SynchronousNativeBinding get_binding{
        "host.config.get.v1",
        {{{"path", HostValueType::String, true},
          {"default", HostValueType::Json, false}},
         HostValueType::Json, "config_read_operations",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Preflight},
        [host](const HostCallContext&, const HostArguments& arguments) {
            return host->impl_->get(arguments);
        }};
    runtime::SynchronousNativeBinding transact_binding{
        "host.config.transact.v1",
        {{{"expected_revision", HostValueType::Integer, true},
          {"patch", HostValueType::OrderedStringJsonMap, true}},
         HostValueType::Json, "config_write_operations",
         runtime::HostExecutionMode::ThreadSafe,
         runtime::HostCancellationMode::Cooperative},
        [host](const HostCallContext& context, const HostArguments& arguments) {
            return host->impl_->transact(context, arguments);
        }};

    auto metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{{
            "baas/config", {1, 0},
            {{"get", "host.config.get.v1", "config.read"},
             {"snapshot", "host.config.snapshot.v1", "config.read"},
             {"transact", "host.config.transact.v1", "config.write"}}}});
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            std::move(snapshot_binding), std::move(get_binding),
            std::move(transact_binding)});
    return {std::move(host), std::move(metadata), std::move(bindings)};
}

}  // namespace baas::script::host
