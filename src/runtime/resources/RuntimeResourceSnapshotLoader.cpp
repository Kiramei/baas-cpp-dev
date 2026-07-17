#include "runtime/resources/RuntimeResourceSnapshotLoader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace baas::runtime::resources {
namespace {

using Json = nlohmann::json;
namespace repository = ::baas::runtime::repository;
namespace snapshot_resources = ::baas::resources;

struct LoaderFailure final {
    RuntimeResourceSnapshotLoadError error;
};

[[noreturn]] void fail(const RuntimeResourceSnapshotLoadError error) { throw LoaderFailure{error}; }

void check_cancelled(const std::stop_token stop) {
    if (stop.stop_requested())
        fail(RuntimeResourceSnapshotLoadError::cancelled);
}

class WorkBudget final {
  public:
    explicit WorkBudget(const std::size_t limit) noexcept : remaining_(limit) {}

    void charge(const std::size_t amount) {
        if (amount > remaining_)
            fail(RuntimeResourceSnapshotLoadError::work_limit_exceeded);
        remaining_ -= amount;
    }

  private:
    std::size_t remaining_;
};

[[nodiscard]] bool valid_limits(const RuntimeResourceSnapshotLoaderLimits& limits) noexcept {
    return limits.max_manifest_bytes != 0 && limits.max_entries != 0 &&
           limits.max_total_bytes != 0 && limits.max_file_bytes != 0 &&
           limits.max_string_bytes != 0 && limits.max_json_depth != 0 &&
           limits.max_json_nodes != 0 && limits.max_work != 0 &&
           limits.max_file_bytes <= limits.max_total_bytes;
}

[[nodiscard]] const repository::RuntimeRepositoryReadEntry*
manifested(const repository::RuntimeRepositoryReadView& resources,
           const std::string_view path) noexcept {
    const auto entries = resources.entries();
    const auto found =
        std::ranges::lower_bound(entries, path, {}, &repository::RuntimeRepositoryReadEntry::path);
    return found != entries.end() && found->path == path ? &*found : nullptr;
}

[[nodiscard]] std::string ascii_fold(const std::string_view value) {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](const unsigned char byte) {
        return static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte);
    });
    return result;
}

[[nodiscard]] bool lower_sha256(const std::string_view value) noexcept {
    return value.size() == 64 && std::ranges::all_of(value, [](const char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

[[nodiscard]] bool exact_fields(const Json& object,
                                const std::initializer_list<std::string_view> required,
                                const std::initializer_list<std::string_view> optional = {}) {
    if (!object.is_object() || object.size() < required.size() ||
        object.size() > required.size() + optional.size())
        return false;
    for (const auto field : required)
        if (!object.contains(field))
            return false;
    for (const auto& [key, unused] : object.items()) {
        static_cast<void>(unused);
        const bool known = std::find(required.begin(), required.end(), key) != required.end() ||
                           std::find(optional.begin(), optional.end(), key) != optional.end();
        if (!known)
            return false;
    }
    return true;
}

struct DuplicateKeyState final {
    bool duplicate{};
    std::size_t nodes{};
    std::vector<std::set<std::string, std::less<>>> objects;
};

[[nodiscard]] Json parse_strict_json(const std::string& text,
                                     const RuntimeResourceSnapshotLoaderLimits& limits) {
    DuplicateKeyState state;
    auto callback = [&state, &limits](int depth, const Json::parse_event_t event, Json& parsed) {
        if ((event == Json::parse_event_t::key || event == Json::parse_event_t::value) &&
            parsed.is_string() &&
            parsed.get_ref<const std::string&>().size() > limits.max_string_bytes)
            fail(RuntimeResourceSnapshotLoadError::string_limit_exceeded);
        if (event == Json::parse_event_t::object_start ||
            event == Json::parse_event_t::array_start || event == Json::parse_event_t::value) {
            ++state.nodes;
        }
        if (depth < 0)
            fail(RuntimeResourceSnapshotLoadError::invalid_manifest);
        if (event == Json::parse_event_t::object_start) {
            state.objects.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (state.objects.empty() ||
                !state.objects.back().insert(parsed.get<std::string>()).second)
                state.duplicate = true;
        } else if (event == Json::parse_event_t::object_end && !state.objects.empty()) {
            state.objects.pop_back();
        }
        return true;
    };
    auto bounded_callback = [&callback, &state, &limits](
                                const int depth, const Json::parse_event_t event, Json& parsed) {
        if (depth < 0 || static_cast<std::size_t>(depth) > limits.max_json_depth)
            fail(RuntimeResourceSnapshotLoadError::invalid_manifest);
        const bool accepted = callback(depth, event, parsed);
        if (state.nodes > limits.max_json_nodes)
            fail(RuntimeResourceSnapshotLoadError::invalid_manifest);
        return accepted;
    };
    // Repository manifests are strict JSON. In particular, do not enable
    // nlohmann's optional C/C++ comment extension here: accepting it would
    // make the signed package grammar differ between validators.
    auto result = Json::parse(text, bounded_callback, true, false);
    if (state.duplicate)
        fail(RuntimeResourceSnapshotLoadError::invalid_manifest);
    return result;
}

[[nodiscard]] std::vector<std::byte>
read_verified(const repository::RuntimeRepositoryReadView& resources, const std::string_view path,
              const std::uintmax_t limit, const std::stop_token stop) {
    try {
        return resources.read(path, limit, stop);
    } catch (const repository::RuntimeRepositoryReadError& error) {
        using enum repository::RuntimeRepositoryReadErrorCode;
        if (error.code() == cancelled)
            fail(RuntimeResourceSnapshotLoadError::cancelled);
        if (error.code() == resource_exhausted)
            fail(RuntimeResourceSnapshotLoadError::resource_exhausted);
        if (error.code() == file_limit_exceeded)
            fail(RuntimeResourceSnapshotLoadError::file_limit_exceeded);
        fail(RuntimeResourceSnapshotLoadError::repository_read_failed);
    }
}

[[nodiscard]] std::string bytes_as_string(std::vector<std::byte>& bytes) {
    std::string text(bytes.size(), '\0');
    if (!bytes.empty())
        std::memcpy(text.data(), bytes.data(), bytes.size());
    return text;
}

#ifdef BAAS_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTING
std::atomic<RuntimeResourceSnapshotLoaderHook> loader_hook{};
void invoke_hook(const RuntimeResourceSnapshotLoaderHookPoint point) {
    if (const auto hook = loader_hook.load(std::memory_order_acquire))
        hook(point);
}
#else
void invoke_hook(const RuntimeResourceSnapshotLoaderHookPoint) noexcept {}
#endif

struct ParsedEntry final {
    std::string id;
    std::string path;
    std::string media_type;
    std::size_t size{};
    std::string sha256;
    std::optional<std::string> locale;
    std::optional<std::string> activity;
};

[[nodiscard]] ParsedEntry parse_entry(const Json& value,
                                      const RuntimeResourceSnapshotLoaderLimits& limits,
                                      WorkBudget& work) {
    if (!exact_fields(value, {"id", "path", "media_type", "size", "sha256"},
                      {"locale", "activity"}))
        fail(RuntimeResourceSnapshotLoadError::invalid_manifest);
    if (!value.at("id").is_string() || !value.at("path").is_string() ||
        !value.at("media_type").is_string() || !value.at("size").is_number_unsigned() ||
        !value.at("sha256").is_string() ||
        (value.contains("locale") && !value.at("locale").is_string()) ||
        (value.contains("activity") && !value.at("activity").is_string()))
        fail(RuntimeResourceSnapshotLoadError::invalid_manifest);

    ParsedEntry result;
    result.id = value.at("id").get<std::string>();
    result.path = value.at("path").get<std::string>();
    result.media_type = value.at("media_type").get<std::string>();
    result.sha256 = value.at("sha256").get<std::string>();
    if (value.contains("locale"))
        result.locale = value.at("locale").get<std::string>();
    if (value.contains("activity"))
        result.activity = value.at("activity").get<std::string>();
    for (const auto size : {result.id.size(), result.path.size(), result.media_type.size(),
                            result.sha256.size(), result.locale ? result.locale->size() : 0U,
                            result.activity ? result.activity->size() : 0U}) {
        if (size > limits.max_string_bytes)
            fail(RuntimeResourceSnapshotLoadError::string_limit_exceeded);
        work.charge(size);
    }
    const auto declared = value.at("size").get<std::uint64_t>();
    if (declared > std::numeric_limits<std::size_t>::max())
        fail(RuntimeResourceSnapshotLoadError::file_limit_exceeded);
    result.size = static_cast<std::size_t>(declared);
    if (result.size > limits.max_file_bytes)
        fail(RuntimeResourceSnapshotLoadError::file_limit_exceeded);
    if (!lower_sha256(result.sha256))
        fail(RuntimeResourceSnapshotLoadError::invalid_manifest);
    return result;
}

[[nodiscard]] RuntimeResourceSnapshotLoadError
map_snapshot_error(const snapshot_resources::ResourceErrorCode code) noexcept {
    using enum snapshot_resources::ResourceErrorCode;
    switch (code) {
    case InvalidLimits:
        return RuntimeResourceSnapshotLoadError::invalid_limits;
    case EntryLimitExceeded:
        return RuntimeResourceSnapshotLoadError::entry_limit_exceeded;
    case ByteLimitExceeded:
        return RuntimeResourceSnapshotLoadError::total_byte_limit_exceeded;
    case InvalidLocale:
    case InvalidActivity:
        return RuntimeResourceSnapshotLoadError::snapshot_validation_failed;
    case DuplicateVariant:
        return RuntimeResourceSnapshotLoadError::duplicate_entry;
    case SizeMismatch:
    case DigestMismatch:
        return RuntimeResourceSnapshotLoadError::manifest_entry_mismatch;
    case InvalidResourceId:
    case InvalidMediaType:
    case InvalidDigest:
        return RuntimeResourceSnapshotLoadError::snapshot_validation_failed;
    }
    return RuntimeResourceSnapshotLoadError::snapshot_validation_failed;
}

} // namespace

std::string_view
runtime_resource_snapshot_load_error_name(const RuntimeResourceSnapshotLoadError error) noexcept {
    using enum RuntimeResourceSnapshotLoadError;
    switch (error) {
    case none:
        return "RRL000_NONE";
    case invalid_limits:
        return "RRL001_INVALID_LIMITS";
    case wrong_repository:
        return "RRL002_WRONG_REPOSITORY";
    case manifest_not_found:
        return "RRL003_MANIFEST_NOT_FOUND";
    case manifest_too_large:
        return "RRL004_MANIFEST_TOO_LARGE";
    case repository_read_failed:
        return "RRL005_REPOSITORY_READ_FAILED";
    case invalid_manifest:
        return "RRL006_INVALID_MANIFEST";
    case entry_limit_exceeded:
        return "RRL007_ENTRY_LIMIT_EXCEEDED";
    case file_limit_exceeded:
        return "RRL008_FILE_LIMIT_EXCEEDED";
    case total_byte_limit_exceeded:
        return "RRL009_TOTAL_BYTE_LIMIT_EXCEEDED";
    case string_limit_exceeded:
        return "RRL010_STRING_LIMIT_EXCEEDED";
    case work_limit_exceeded:
        return "RRL011_WORK_LIMIT_EXCEEDED";
    case path_not_manifested:
        return "RRL012_PATH_NOT_MANIFESTED";
    case manifest_entry_mismatch:
        return "RRL013_MANIFEST_ENTRY_MISMATCH";
    case duplicate_entry:
        return "RRL014_DUPLICATE_ENTRY";
    case invalid_selector:
        return "RRL015_INVALID_SELECTOR";
    case snapshot_validation_failed:
        return "RRL016_SNAPSHOT_VALIDATION_FAILED";
    case cancelled:
        return "RRL017_CANCELLED";
    case resource_exhausted:
        return "RRL018_RESOURCE_EXHAUSTED";
    case internal_failure:
        return "RRL019_INTERNAL_FAILURE";
    }
    return "RRL999_UNKNOWN";
}

RuntimeResourceSnapshotLoadResult
load_runtime_resource_snapshot(const repository::RuntimeRepositoryReadView& resources,
                               snapshot_resources::ResourceSelector selector,
                               const RuntimeResourceSnapshotLoaderLimits& limits,
                               const std::stop_token stop_token) noexcept {
    try {
        if (!valid_limits(limits))
            fail(RuntimeResourceSnapshotLoadError::invalid_limits);
        if (resources.repository_id() != "resources")
            fail(RuntimeResourceSnapshotLoadError::wrong_repository);
        if (!snapshot_resources::valid_resource_locale(selector.locale, limits.max_string_bytes) ||
            (selector.current_activity && !snapshot_resources::valid_resource_activity(
                                              *selector.current_activity, limits.max_string_bytes)))
            fail(RuntimeResourceSnapshotLoadError::invalid_selector);
        check_cancelled(stop_token);

        const auto* manifest = manifested(resources, runtime_resource_manifest_path);
        if (manifest == nullptr)
            fail(RuntimeResourceSnapshotLoadError::manifest_not_found);
        if (manifest->size > limits.max_manifest_bytes)
            fail(RuntimeResourceSnapshotLoadError::manifest_too_large);
        WorkBudget work{limits.max_work};
        // Reading, copying to parser input, and parsing are three independent
        // linear passes. Reserve all of them before touching repository bytes.
        work.charge(static_cast<std::size_t>(manifest->size));
        work.charge(static_cast<std::size_t>(manifest->size));
        work.charge(static_cast<std::size_t>(manifest->size));
        invoke_hook(RuntimeResourceSnapshotLoaderHookPoint::before_manifest_read);
        auto manifest_bytes = read_verified(resources, runtime_resource_manifest_path,
                                            limits.max_manifest_bytes, stop_token);
        check_cancelled(stop_token);
        auto document = parse_strict_json(bytes_as_string(manifest_bytes), limits);
        invoke_hook(RuntimeResourceSnapshotLoaderHookPoint::after_manifest_parse);
        check_cancelled(stop_token);
        if (!exact_fields(document, {"schema", "entries"}) || !document.at("schema").is_string() ||
            document.at("schema").get<std::string_view>() != runtime_resource_manifest_schema ||
            !document.at("entries").is_array())
            fail(RuntimeResourceSnapshotLoadError::invalid_manifest);

        const auto& entry_documents = document.at("entries");
        if (entry_documents.size() > limits.max_entries)
            fail(RuntimeResourceSnapshotLoadError::entry_limit_exceeded);
        std::vector<ParsedEntry> entries;
        entries.reserve(entry_documents.size());
        std::set<std::string, std::less<>> paths;
        std::set<std::string, std::less<>> folded_paths;
        std::set<std::tuple<std::string, std::string, std::string>, std::less<>> variants;
        std::size_t total_bytes{};
        for (const auto& value : entry_documents) {
            check_cancelled(stop_token);
            work.charge(1);
            auto entry = parse_entry(value, limits, work);
            const auto* repository_entry = manifested(resources, entry.path);
            if (repository_entry == nullptr)
                fail(RuntimeResourceSnapshotLoadError::path_not_manifested);
            if (repository_entry->size != entry.size || repository_entry->sha256 != entry.sha256)
                fail(RuntimeResourceSnapshotLoadError::manifest_entry_mismatch);
            if (!paths.insert(entry.path).second ||
                !folded_paths.insert(ascii_fold(entry.path)).second ||
                !variants.emplace(entry.id, entry.locale.value_or(""), entry.activity.value_or(""))
                     .second)
                fail(RuntimeResourceSnapshotLoadError::duplicate_entry);
            if (entry.size > limits.max_total_bytes - total_bytes)
                fail(RuntimeResourceSnapshotLoadError::total_byte_limit_exceeded);
            total_bytes += entry.size;
            entries.push_back(std::move(entry));
        }

        std::vector<snapshot_resources::ResourcePayload> payloads;
        payloads.reserve(entries.size());
        for (auto& entry : entries) {
            check_cancelled(stop_token);
            work.charge(entry.size);
            auto bytes = read_verified(resources, entry.path, limits.max_file_bytes, stop_token);
            check_cancelled(stop_token);
            invoke_hook(RuntimeResourceSnapshotLoaderHookPoint::before_payload_copy);
            auto owned = std::make_shared<const std::vector<std::byte>>(std::move(bytes));
            payloads.push_back({std::move(entry.id), std::move(entry.locale),
                                std::move(entry.activity), std::move(entry.media_type), entry.size,
                                std::move(entry.sha256), std::move(owned)});
        }
        check_cancelled(stop_token);
        // ResourceSnapshot performs an independent digest pass and then a
        // separate defensive copy. Reserve both linear passes before
        // publication begins; the repository read pass was charged above.
        work.charge(total_bytes);
        work.charge(total_bytes);
        invoke_hook(RuntimeResourceSnapshotLoaderHookPoint::before_snapshot_build);
        snapshot_resources::ResourceSnapshotLimits snapshot_limits;
        snapshot_limits.max_entries = limits.max_entries;
        snapshot_limits.max_total_bytes = limits.max_total_bytes;
        snapshot_limits.max_entry_bytes = limits.max_file_bytes;
        snapshot_limits.max_resource_id_bytes = limits.max_string_bytes;
        snapshot_limits.max_selector_bytes = limits.max_string_bytes;
        snapshot_limits.max_media_type_bytes = limits.max_string_bytes;
        auto snapshot = snapshot_resources::ResourceSnapshot::build(
            std::move(selector), std::move(payloads), snapshot_limits);
        check_cancelled(stop_token);
        return {std::move(snapshot), RuntimeResourceSnapshotLoadError::none};
    } catch (const LoaderFailure& failure) {
        return {{}, failure.error};
    } catch (const snapshot_resources::ResourceError& error) {
        return {{}, map_snapshot_error(error.code())};
    } catch (const nlohmann::json::exception&) {
        return {{}, RuntimeResourceSnapshotLoadError::invalid_manifest};
    } catch (const std::bad_alloc&) {
        return {{}, RuntimeResourceSnapshotLoadError::resource_exhausted};
    } catch (...) {
        return {{}, RuntimeResourceSnapshotLoadError::internal_failure};
    }
}

#ifdef BAAS_RUNTIME_RESOURCE_SNAPSHOT_LOADER_TESTING
void set_runtime_resource_snapshot_loader_hook(
    const RuntimeResourceSnapshotLoaderHook hook) noexcept {
    loader_hook.store(hook, std::memory_order_release);
}
#endif

} // namespace baas::runtime::resources
