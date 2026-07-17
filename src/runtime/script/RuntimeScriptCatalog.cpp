#include "runtime/script/RuntimeScriptCatalog.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

namespace baas::runtime::script {
namespace {

constexpr std::size_t hard_manifest_bytes = 4U * 1'024U * 1'024U;
constexpr std::size_t hard_json_depth = 32;
constexpr std::size_t hard_json_nodes = 500'000;
constexpr std::size_t hard_tasks = 16'384;
constexpr std::size_t hard_aliases_per_task = 256;
constexpr std::size_t hard_total_aliases = 65'536;
constexpr std::size_t hard_host_modules_per_task = 512;
constexpr std::size_t hard_capabilities_per_module = 512;
constexpr std::size_t hard_total_host_modules = 262'144;
constexpr std::size_t hard_total_capabilities = 1'048'576;
constexpr std::size_t hard_string_bytes = 4'096;
constexpr std::size_t hard_total_string_bytes = 16U * 1'024U * 1'024U;
constexpr std::size_t hard_work = 64U * 1'024U * 1'024U;
constexpr std::size_t hard_module_segments = 256;

struct Failure final {
    RuntimeScriptCatalogError error;
};

[[noreturn]] void fail(const RuntimeScriptCatalogError error)
{
    throw Failure{error};
}

void cancelled(const std::stop_token stop_token)
{
    if (stop_token.stop_requested()) fail(RuntimeScriptCatalogError::cancelled);
}

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t index{};
    while (index < value.size()) {
        const auto lead = bytes[index++];
        if (lead <= 0x7fU) continue;
        std::size_t trailing{};
        std::uint32_t code_point{};
        std::uint32_t minimum{};
        if (lead >= 0xc2U && lead <= 0xdfU) {
            trailing = 1;
            code_point = lead & 0x1fU;
            minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            trailing = 2;
            code_point = lead & 0x0fU;
            minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            trailing = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (trailing > value.size() - index) return false;
        for (std::size_t offset{}; offset < trailing; ++offset) {
            const auto byte = bytes[index++];
            if ((byte & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU
            || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

struct Json final {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    std::variant<std::string, std::uint64_t, Array, Object> value;
};

class JsonParser final {
public:
    JsonParser(
        const std::string_view input,
        const RuntimeScriptCatalogLimits& limits,
        const std::stop_token stop_token) noexcept
        : input_(input), limits_(limits), stop_token_(stop_token)
    {
    }

    [[nodiscard]] Json parse()
    {
        auto result = value(0);
        whitespace();
        if (offset_ != input_.size()) fail(RuntimeScriptCatalogError::invalid_json);
        return result;
    }

    [[nodiscard]] std::size_t work() const noexcept { return work_; }

private:
    void charge(const std::size_t amount = 1)
    {
        cancelled(stop_token_);
        if (amount > limits_.max_work - work_)
            fail(RuntimeScriptCatalogError::limit_exceeded);
        work_ += amount;
    }

    void node()
    {
        charge();
        if (++nodes_ > limits_.max_json_nodes)
            fail(RuntimeScriptCatalogError::limit_exceeded);
    }

    void whitespace()
    {
        while (offset_ < input_.size()) {
            const auto byte = input_[offset_];
            if (byte != ' ' && byte != '\n' && byte != '\r' && byte != '\t') break;
            ++offset_;
            charge();
        }
    }

    [[nodiscard]] char take()
    {
        charge();
        if (offset_ == input_.size()) fail(RuntimeScriptCatalogError::invalid_json);
        return input_[offset_++];
    }

    [[nodiscard]] static unsigned hex(const char byte)
    {
        if (byte >= '0' && byte <= '9') return static_cast<unsigned>(byte - '0');
        if (byte >= 'a' && byte <= 'f') return static_cast<unsigned>(byte - 'a' + 10);
        if (byte >= 'A' && byte <= 'F') return static_cast<unsigned>(byte - 'A' + 10);
        fail(RuntimeScriptCatalogError::invalid_json);
    }

    [[nodiscard]] std::uint32_t unicode_unit()
    {
        std::uint32_t result{};
        for (int index{}; index < 4; ++index) result = (result << 4U) | hex(take());
        return result;
    }

    static void append_utf8(std::string& output, const std::uint32_t code_point)
    {
        if (code_point <= 0x7fU) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7ffU) {
            output.push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else if (code_point <= 0xffffU) {
            output.push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        } else {
            output.push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
        }
    }

    void check_string(const std::string& value)
    {
        if (value.size() > limits_.max_string_bytes
            || value.size() > limits_.max_total_string_bytes - string_bytes_) {
            fail(RuntimeScriptCatalogError::limit_exceeded);
        }
    }

    void append_unicode(std::string& output)
    {
        const auto first = unicode_unit();
        if (first >= 0xdc00U && first <= 0xdfffU)
            fail(RuntimeScriptCatalogError::invalid_json);
        if (first < 0xd800U || first > 0xdbffU) {
            append_utf8(output, first);
            check_string(output);
            return;
        }
        if (take() != '\\' || take() != 'u')
            fail(RuntimeScriptCatalogError::invalid_json);
        const auto second = unicode_unit();
        if (second < 0xdc00U || second > 0xdfffU)
            fail(RuntimeScriptCatalogError::invalid_json);
        append_utf8(output, 0x10000U + ((first - 0xd800U) << 10U) + second - 0xdc00U);
        check_string(output);
    }

    [[nodiscard]] std::string string()
    {
        if (take() != '"') fail(RuntimeScriptCatalogError::invalid_json);
        std::string result;
        while (true) {
            const auto byte = take();
            if (byte == '"') break;
            if (static_cast<unsigned char>(byte) < 0x20U)
                fail(RuntimeScriptCatalogError::invalid_json);
            if (byte != '\\') {
                result.push_back(byte);
            } else {
                switch (take()) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': append_unicode(result); break;
                    default: fail(RuntimeScriptCatalogError::invalid_json);
                }
            }
            check_string(result);
        }
        if (result.size() > limits_.max_total_string_bytes - string_bytes_)
            fail(RuntimeScriptCatalogError::limit_exceeded);
        string_bytes_ += result.size();
        return result;
    }

    [[nodiscard]] std::uint64_t number()
    {
        const auto begin = offset_;
        if (input_[offset_] == '0') {
            static_cast<void>(take());
            if (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9')
                fail(RuntimeScriptCatalogError::invalid_json);
        } else {
            if (input_[offset_] < '1' || input_[offset_] > '9')
                fail(RuntimeScriptCatalogError::invalid_json);
            while (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9')
                static_cast<void>(take());
        }
        if (offset_ < input_.size()
            && (input_[offset_] == '.' || input_[offset_] == 'e' || input_[offset_] == 'E'))
            fail(RuntimeScriptCatalogError::invalid_json);
        std::uint64_t result{};
        for (std::size_t index = begin; index < offset_; ++index) {
            const auto digit = static_cast<unsigned>(input_[index] - '0');
            if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
                fail(RuntimeScriptCatalogError::invalid_value);
            result = result * 10U + digit;
        }
        return result;
    }

    [[nodiscard]] Json array(const std::size_t depth)
    {
        if (depth >= limits_.max_json_depth)
            fail(RuntimeScriptCatalogError::limit_exceeded);
        static_cast<void>(take());
        Json::Array result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == ']') {
            static_cast<void>(take());
            return Json{std::move(result)};
        }
        while (true) {
            result.push_back(value(depth + 1));
            whitespace();
            const auto separator = take();
            if (separator == ']') break;
            if (separator != ',') fail(RuntimeScriptCatalogError::invalid_json);
            whitespace();
        }
        return Json{std::move(result)};
    }

    [[nodiscard]] Json object(const std::size_t depth)
    {
        if (depth >= limits_.max_json_depth)
            fail(RuntimeScriptCatalogError::limit_exceeded);
        static_cast<void>(take());
        Json::Object result;
        whitespace();
        if (offset_ < input_.size() && input_[offset_] == '}') {
            static_cast<void>(take());
            return Json{std::move(result)};
        }
        while (true) {
            if (offset_ == input_.size() || input_[offset_] != '"')
                fail(RuntimeScriptCatalogError::invalid_json);
            auto key = string();
            whitespace();
            if (take() != ':') fail(RuntimeScriptCatalogError::invalid_json);
            whitespace();
            auto item = value(depth + 1);
            if (!result.emplace(std::move(key), std::move(item)).second)
                fail(RuntimeScriptCatalogError::invalid_json);
            whitespace();
            const auto separator = take();
            if (separator == '}') break;
            if (separator != ',') fail(RuntimeScriptCatalogError::invalid_json);
            whitespace();
        }
        return Json{std::move(result)};
    }

    [[nodiscard]] Json value(const std::size_t depth)
    {
        cancelled(stop_token_);
        whitespace();
        if (offset_ == input_.size()) fail(RuntimeScriptCatalogError::invalid_json);
        node();
        if (input_[offset_] == '"') return Json{string()};
        if (input_[offset_] == '[') return array(depth);
        if (input_[offset_] == '{') return object(depth);
        if (input_[offset_] >= '0' && input_[offset_] <= '9') return Json{number()};
        fail(RuntimeScriptCatalogError::invalid_json);
    }

    std::string_view input_;
    const RuntimeScriptCatalogLimits& limits_;
    std::stop_token stop_token_;
    std::size_t offset_{};
    std::size_t nodes_{};
    std::size_t string_bytes_{};
    std::size_t work_{};
};

template <std::size_t Size>
[[nodiscard]] bool exact_fields(
    const Json::Object& value,
    const std::array<std::string_view, Size>& fields) noexcept
{
    if (value.size() != fields.size()) return false;
    return std::ranges::all_of(fields, [&value](const auto field) {
        return value.contains(field);
    });
}

[[nodiscard]] const Json::Object& as_object(const Json& value)
{
    const auto* result = std::get_if<Json::Object>(&value.value);
    if (result == nullptr) fail(RuntimeScriptCatalogError::invalid_value);
    return *result;
}

[[nodiscard]] const Json::Array& as_array(const Json& value)
{
    const auto* result = std::get_if<Json::Array>(&value.value);
    if (result == nullptr) fail(RuntimeScriptCatalogError::invalid_value);
    return *result;
}

[[nodiscard]] const std::string& as_string(const Json& value)
{
    const auto* result = std::get_if<std::string>(&value.value);
    if (result == nullptr) fail(RuntimeScriptCatalogError::invalid_value);
    return *result;
}

[[nodiscard]] std::uint32_t as_version(const Json& value, const bool nonzero)
{
    const auto* result = std::get_if<std::uint64_t>(&value.value);
    if (result == nullptr || *result > std::numeric_limits<std::uint32_t>::max()
        || (nonzero && *result == 0)) {
        fail(RuntimeScriptCatalogError::invalid_value);
    }
    return static_cast<std::uint32_t>(*result);
}

[[nodiscard]] const std::string& required_text(const Json& value)
{
    const auto& result = as_string(value);
    if (result.empty() || result.find('\0') != std::string::npos)
        fail(RuntimeScriptCatalogError::invalid_value);
    return result;
}

[[nodiscard]] bool lower_identifier(const std::string_view value) noexcept
{
    if (value.empty() || value.front() < 'a' || value.front() > 'z') return false;
    return std::ranges::all_of(value.substr(1), [](const char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
            || byte == '_';
    });
}

[[nodiscard]] bool capability_id(const std::string_view value) noexcept
{
    bool dot{};
    std::size_t begin{};
    while (begin < value.size()) {
        const auto end = value.find('.', begin);
        const auto segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (!lower_identifier(segment)) return false;
        if (end == std::string_view::npos) break;
        dot = true;
        begin = end + 1;
    }
    return dot;
}

[[nodiscard]] const repository::RuntimeRepositoryReadEntry* manifested(
    const repository::RuntimeRepositoryReadView& scripts,
    const std::string_view path) noexcept
{
    const auto entries = scripts.entries();
    const auto found = std::ranges::lower_bound(
        entries, path, {}, &repository::RuntimeRepositoryReadEntry::path);
    return found == entries.end() || found->path != path ? nullptr : &*found;
}

void validate_limits(const RuntimeScriptCatalogLimits& limits)
{
    const bool valid = limits.max_manifest_bytes != 0
        && limits.max_manifest_bytes <= hard_manifest_bytes
        && limits.max_json_depth != 0 && limits.max_json_depth <= hard_json_depth
        && limits.max_json_nodes != 0 && limits.max_json_nodes <= hard_json_nodes
        && limits.max_tasks != 0 && limits.max_tasks <= hard_tasks
        && limits.max_aliases_per_task != 0
        && limits.max_aliases_per_task <= hard_aliases_per_task
        && limits.max_total_aliases != 0 && limits.max_total_aliases <= hard_total_aliases
        && limits.max_host_modules_per_task != 0
        && limits.max_host_modules_per_task <= hard_host_modules_per_task
        && limits.max_capabilities_per_module != 0
        && limits.max_capabilities_per_module <= hard_capabilities_per_module
        && limits.max_total_host_modules != 0
        && limits.max_total_host_modules <= hard_total_host_modules
        && limits.max_total_capabilities != 0
        && limits.max_total_capabilities <= hard_total_capabilities
        && limits.max_string_bytes != 0 && limits.max_string_bytes <= hard_string_bytes
        && limits.max_total_string_bytes != 0
        && limits.max_total_string_bytes <= hard_total_string_bytes
        && limits.max_work != 0 && limits.max_work <= hard_work
        && limits.module_specifier.max_bytes != 0
        && limits.module_specifier.max_bytes <= limits.max_string_bytes
        && limits.module_specifier.max_segments != 0
        && limits.module_specifier.max_segments <= hard_module_segments;
    if (!valid) fail(RuntimeScriptCatalogError::invalid_limits);
}

void validate_module(
    const std::string_view value,
    const ::baas::script::runtime::ModuleKind expected,
    const RuntimeScriptCatalogLimits& limits)
{
    try {
        const auto specifier = ::baas::script::runtime::validate_module_specifier(
            value, limits.is_nfc, limits.module_specifier);
        if (specifier.kind != expected) fail(RuntimeScriptCatalogError::invalid_value);
        if (expected == ::baas::script::runtime::ModuleKind::Host) {
            const auto suffix = value.starts_with("baas/") ? value.substr(5) : std::string_view{};
            if (suffix.find('/') != std::string_view::npos || !lower_identifier(suffix))
                fail(RuntimeScriptCatalogError::invalid_value);
        }
    } catch (const ::baas::script::runtime::ModuleSpecifierError&) {
        fail(RuntimeScriptCatalogError::invalid_value);
    } catch (const std::invalid_argument&) {
        fail(RuntimeScriptCatalogError::invalid_limits);
    }
}

struct Route final {
    std::string run_mode;
    std::string requested_task;
    std::size_t task_index{};
    bool legacy_alias{};
};

}  // namespace

struct RuntimeScriptCatalog::Impl final {
    std::string generation;
    std::string commit;
    std::vector<RuntimeScriptTaskDescriptor> tasks;
    std::vector<Route> routes;
};

RuntimeScriptCatalogResolution::RuntimeScriptCatalogResolution(
    std::shared_ptr<const void> owner,
    const RuntimeScriptTaskDescriptor* const task_value,
    const std::string_view requested_task_value,
    const bool legacy_alias_value,
    const std::string_view generation_value,
    const std::string_view commit_value) noexcept
    : task(task_value),
      requested_task(requested_task_value),
      legacy_alias(legacy_alias_value),
      owner_(std::move(owner)),
      generation_(generation_value),
      commit_(commit_value),
      resolved_task_(task_value),
      resolved_requested_task_(requested_task_value),
      resolved_legacy_alias_(legacy_alias_value)
{
}

RuntimeScriptCatalogResolution::RuntimeScriptCatalogResolution(
    RuntimeScriptCatalogResolution&& other) noexcept
    : task(other.task),
      requested_task(other.requested_task),
      legacy_alias(other.legacy_alias),
      owner_(other.owner_),
      generation_(other.generation_),
      commit_(other.commit_),
      resolved_task_(other.resolved_task_),
      resolved_requested_task_(other.resolved_requested_task_),
      resolved_legacy_alias_(other.resolved_legacy_alias_)
{
}

RuntimeScriptCatalogResolution& RuntimeScriptCatalogResolution::operator=(
    RuntimeScriptCatalogResolution&& other) noexcept
{
    if (this == &other) return *this;
    task = other.task;
    requested_task = other.requested_task;
    legacy_alias = other.legacy_alias;
    owner_ = other.owner_;
    generation_ = other.generation_;
    commit_ = other.commit_;
    resolved_task_ = other.resolved_task_;
    resolved_requested_task_ = other.resolved_requested_task_;
    resolved_legacy_alias_ = other.resolved_legacy_alias_;
    return *this;
}

std::string_view runtime_script_catalog_error_name(
    const RuntimeScriptCatalogError error) noexcept
{
    using enum RuntimeScriptCatalogError;
    switch (error) {
        case none: return "RSC000_NONE";
        case invalid_limits: return "RSC001_INVALID_LIMITS";
        case wrong_repository: return "RSC002_WRONG_REPOSITORY";
        case manifest_not_found: return "RSC003_MANIFEST_NOT_FOUND";
        case manifest_too_large: return "RSC004_MANIFEST_TOO_LARGE";
        case repository_read_failed: return "RSC005_REPOSITORY_READ_FAILED";
        case invalid_utf8: return "RSC006_INVALID_UTF8";
        case invalid_json: return "RSC007_INVALID_JSON";
        case limit_exceeded: return "RSC008_LIMIT_EXCEEDED";
        case invalid_schema: return "RSC009_INVALID_SCHEMA";
        case invalid_field_set: return "RSC010_INVALID_FIELD_SET";
        case generation_mismatch: return "RSC011_GENERATION_MISMATCH";
        case commit_mismatch: return "RSC012_COMMIT_MISMATCH";
        case invalid_value: return "RSC013_INVALID_VALUE";
        case duplicate_route: return "RSC014_DUPLICATE_ROUTE";
        case missing_package_manifest: return "RSC015_MISSING_PACKAGE_MANIFEST";
        case missing_entry_module: return "RSC016_MISSING_ENTRY_MODULE";
        case cancelled: return "RSC017_CANCELLED";
        case resource_exhausted: return "RSC018_RESOURCE_EXHAUSTED";
    }
    return "RSC999_UNKNOWN";
}

RuntimeScriptCatalog::RuntimeScriptCatalog(std::shared_ptr<const Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

RuntimeScriptCatalog::~RuntimeScriptCatalog() = default;

const std::string& RuntimeScriptCatalog::generation() const noexcept
{
    return impl_->generation;
}

const std::string& RuntimeScriptCatalog::commit() const noexcept
{
    return impl_->commit;
}

std::span<const RuntimeScriptTaskDescriptor> RuntimeScriptCatalog::tasks() const noexcept
{
    return impl_->tasks;
}

std::optional<RuntimeScriptCatalogResolution> RuntimeScriptCatalog::resolve(
    const std::string_view run_mode,
    const std::string_view requested_task) const noexcept
{
    const auto key = std::pair{run_mode, requested_task};
    const auto found = std::ranges::lower_bound(
        impl_->routes, key, {}, [](const Route& route) {
            return std::pair<std::string_view, std::string_view>{
                route.run_mode, route.requested_task};
        });
    if (found == impl_->routes.end() || found->run_mode != run_mode
        || found->requested_task != requested_task) {
        return std::nullopt;
    }
    return RuntimeScriptCatalogResolution{
        impl_,
        &impl_->tasks[found->task_index],
        found->requested_task,
        found->legacy_alias,
        impl_->generation,
        impl_->commit};
}

RuntimeScriptCatalogLoadResult load_runtime_script_catalog(
    const repository::RuntimeRepositoryReadView& scripts,
    const RuntimeScriptCatalogPin expected,
    const RuntimeScriptCatalogLimits& limits,
    const std::stop_token stop_token) noexcept
{
    try {
        validate_limits(limits);
        cancelled(stop_token);
        if (scripts.repository_id() != "scripts") fail(RuntimeScriptCatalogError::wrong_repository);
        if (expected.generation.empty() || expected.generation != scripts.generation())
            fail(RuntimeScriptCatalogError::generation_mismatch);
        if (expected.commit.empty() || expected.commit != scripts.commit())
            fail(RuntimeScriptCatalogError::commit_mismatch);
        const auto* catalog_entry = manifested(scripts, runtime_script_catalog_manifest);
        if (catalog_entry == nullptr) fail(RuntimeScriptCatalogError::manifest_not_found);
        if (catalog_entry->size > limits.max_manifest_bytes)
            fail(RuntimeScriptCatalogError::manifest_too_large);

        std::vector<std::byte> bytes;
        try {
            bytes = scripts.read(
                runtime_script_catalog_manifest, limits.max_manifest_bytes, stop_token);
        } catch (const repository::RuntimeRepositoryReadError& error) {
            if (error.code() == repository::RuntimeRepositoryReadErrorCode::cancelled)
                fail(RuntimeScriptCatalogError::cancelled);
            if (error.code() == repository::RuntimeRepositoryReadErrorCode::resource_exhausted)
                fail(RuntimeScriptCatalogError::resource_exhausted);
            fail(RuntimeScriptCatalogError::repository_read_failed);
        }
        cancelled(stop_token);
        const std::string_view text{
            reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        if (!valid_utf8(text)) fail(RuntimeScriptCatalogError::invalid_utf8);
        JsonParser parser{text, limits, stop_token};
        const auto root_json = parser.parse();
        std::size_t work = parser.work();
        auto charge = [&](const std::size_t amount = 1) {
            cancelled(stop_token);
            if (amount > limits.max_work - work)
                fail(RuntimeScriptCatalogError::limit_exceeded);
            work += amount;
        };

        const auto& root = as_object(root_json);
        constexpr std::array root_fields{
            std::string_view{"schema"}, std::string_view{"tasks"}};
        if (!exact_fields(root, root_fields))
            fail(RuntimeScriptCatalogError::invalid_field_set);
        if (as_string(root.at("schema")) != runtime_script_catalog_schema)
            fail(RuntimeScriptCatalogError::invalid_schema);

        const auto& task_values = as_array(root.at("tasks"));
        if (task_values.empty() || task_values.size() > limits.max_tasks)
            fail(RuntimeScriptCatalogError::limit_exceeded);

        auto impl = std::make_shared<RuntimeScriptCatalog::Impl>();
        impl->generation = scripts.generation();
        impl->commit = scripts.commit();
        impl->tasks.reserve(task_values.size());
        std::size_t total_aliases{};
        std::size_t total_host_modules{};
        std::size_t total_capabilities{};

        for (const auto& task_value : task_values) {
            charge();
            const auto& task = as_object(task_value);
            constexpr std::array task_fields{
                std::string_view{"run_mode"}, std::string_view{"task"},
                std::string_view{"package_root"},
                std::string_view{"package_manifest"}, std::string_view{"entry_module"},
                std::string_view{"entry_export"}, std::string_view{"language_version"},
                std::string_view{"host_modules"}, std::string_view{"legacy_aliases"}};
            if (!exact_fields(task, task_fields))
                fail(RuntimeScriptCatalogError::invalid_field_set);

            RuntimeScriptTaskDescriptor descriptor;
            descriptor.run_mode = required_text(task.at("run_mode"));
            descriptor.canonical_task = required_text(task.at("task"));
            descriptor.package_root = required_text(task.at("package_root"));
            descriptor.package_manifest = required_text(task.at("package_manifest"));
            descriptor.entry_module = required_text(task.at("entry_module"));
            descriptor.entry_export = required_text(task.at("entry_export"));
            validate_module(
                descriptor.package_root,
                ::baas::script::runtime::ModuleKind::Package,
                limits);
            validate_module(
                descriptor.entry_module,
                ::baas::script::runtime::ModuleKind::Package,
                limits);
            const auto expected_manifest = descriptor.package_root + "/baas.package.json";
            if (descriptor.package_manifest != expected_manifest
                || manifested(scripts, descriptor.package_manifest) == nullptr) {
                fail(RuntimeScriptCatalogError::missing_package_manifest);
            }
            const auto entry_source = descriptor.package_root + "/"
                +
                ::baas::script::runtime::ModuleSpecifier{
                    ::baas::script::runtime::ModuleKind::Package,
                    descriptor.entry_module}.manifest_source_path();
            if (manifested(scripts, entry_source) == nullptr)
                fail(RuntimeScriptCatalogError::missing_entry_module);

            const auto& language = as_object(task.at("language_version"));
            constexpr std::array language_fields{
                std::string_view{"major"}, std::string_view{"minor"}};
            if (!exact_fields(language, language_fields))
                fail(RuntimeScriptCatalogError::invalid_field_set);
            descriptor.language_version = {
                as_version(language.at("major"), true),
                as_version(language.at("minor"), false)};

            const auto& host_values = as_array(task.at("host_modules"));
            if (host_values.size() > limits.max_host_modules_per_task
                || host_values.size() > limits.max_total_host_modules - total_host_modules) {
                fail(RuntimeScriptCatalogError::limit_exceeded);
            }
            total_host_modules += host_values.size();
            descriptor.host_modules.reserve(host_values.size());
            for (const auto& host_value : host_values) {
                charge();
                const auto& host = as_object(host_value);
                constexpr std::array host_fields{
                    std::string_view{"module"}, std::string_view{"major"},
                    std::string_view{"min_minor"}, std::string_view{"capabilities"}};
                if (!exact_fields(host, host_fields))
                    fail(RuntimeScriptCatalogError::invalid_field_set);
                RuntimeScriptHostRequirement requirement;
                requirement.canonical_id = required_text(host.at("module"));
                validate_module(
                    requirement.canonical_id,
                    ::baas::script::runtime::ModuleKind::Host,
                    limits);
                requirement.major = as_version(host.at("major"), true);
                requirement.min_minor = as_version(host.at("min_minor"), false);
                const auto& capability_values = as_array(host.at("capabilities"));
                if (capability_values.size() > limits.max_capabilities_per_module
                    || capability_values.size()
                        > limits.max_total_capabilities - total_capabilities) {
                    fail(RuntimeScriptCatalogError::limit_exceeded);
                }
                total_capabilities += capability_values.size();
                requirement.capabilities.reserve(capability_values.size());
                for (const auto& capability_value : capability_values) {
                    charge();
                    const auto& capability = required_text(capability_value);
                    if (!capability_id(capability))
                        fail(RuntimeScriptCatalogError::invalid_value);
                    requirement.capabilities.push_back(capability);
                }
                std::ranges::sort(requirement.capabilities);
                if (std::ranges::adjacent_find(requirement.capabilities)
                    != requirement.capabilities.end()) {
                    fail(RuntimeScriptCatalogError::invalid_value);
                }
                descriptor.host_modules.push_back(std::move(requirement));
            }
            std::ranges::sort(descriptor.host_modules, {},
                              &RuntimeScriptHostRequirement::canonical_id);
            if (std::ranges::adjacent_find(
                    descriptor.host_modules, {},
                    &RuntimeScriptHostRequirement::canonical_id)
                != descriptor.host_modules.end()) {
                fail(RuntimeScriptCatalogError::invalid_value);
            }

            const auto& aliases = as_array(task.at("legacy_aliases"));
            if (aliases.size() > limits.max_aliases_per_task
                || aliases.size() > limits.max_total_aliases - total_aliases) {
                fail(RuntimeScriptCatalogError::limit_exceeded);
            }
            total_aliases += aliases.size();
            descriptor.legacy_aliases.reserve(aliases.size());
            for (const auto& alias_value : aliases) {
                charge();
                const auto& alias = required_text(alias_value);
                if (alias == descriptor.canonical_task)
                    fail(RuntimeScriptCatalogError::invalid_value);
                descriptor.legacy_aliases.push_back(alias);
            }
            std::ranges::sort(descriptor.legacy_aliases);
            if (std::ranges::adjacent_find(descriptor.legacy_aliases)
                != descriptor.legacy_aliases.end()) {
                fail(RuntimeScriptCatalogError::invalid_value);
            }
            impl->tasks.push_back(std::move(descriptor));
        }

        std::ranges::sort(impl->tasks, [](const auto& left, const auto& right) {
            return std::tie(left.run_mode, left.canonical_task)
                < std::tie(right.run_mode, right.canonical_task);
        });
        impl->routes.reserve(impl->tasks.size() + total_aliases);
        for (std::size_t index{}; index < impl->tasks.size(); ++index) {
            charge();
            const auto& task = impl->tasks[index];
            impl->routes.push_back({task.run_mode, task.canonical_task, index, false});
            for (const auto& alias : task.legacy_aliases) {
                charge();
                impl->routes.push_back({task.run_mode, alias, index, true});
            }
        }
        std::ranges::sort(impl->routes, [](const Route& left, const Route& right) {
            return std::tie(left.run_mode, left.requested_task)
                < std::tie(right.run_mode, right.requested_task);
        });
        if (std::ranges::adjacent_find(
                impl->routes,
                [](const Route& left, const Route& right) {
                    return left.run_mode == right.run_mode
                        && left.requested_task == right.requested_task;
                }) != impl->routes.end()) {
            fail(RuntimeScriptCatalogError::duplicate_route);
        }
        cancelled(stop_token);
        if (expected.generation != scripts.generation())
            fail(RuntimeScriptCatalogError::generation_mismatch);
        if (expected.commit != scripts.commit())
            fail(RuntimeScriptCatalogError::commit_mismatch);
        return {
            RuntimeScriptCatalog{std::shared_ptr<const RuntimeScriptCatalog::Impl>{
                std::move(impl)}},
            RuntimeScriptCatalogError::none};
    } catch (const Failure& error) {
        return {std::nullopt, error.error};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, RuntimeScriptCatalogError::resource_exhausted};
    } catch (...) {
        return {std::nullopt, RuntimeScriptCatalogError::invalid_value};
    }
}

}  // namespace baas::runtime::script
