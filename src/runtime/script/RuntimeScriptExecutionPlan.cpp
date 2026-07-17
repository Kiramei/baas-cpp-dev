#include "runtime/script/RuntimeScriptExecutionPlan.h"

#include "script/runtime/ModuleSpecifier.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

namespace baas::runtime::script {
namespace {

constexpr std::size_t hard_manifest_bytes = 4U * 1'024U * 1'024U;
constexpr std::size_t hard_json_depth = 32;
constexpr std::size_t hard_json_nodes = 500'000;
constexpr std::size_t hard_modules = 16'384;
constexpr std::size_t hard_procedures = 16'384;
constexpr std::size_t hard_host_modules = 1'024;
constexpr std::size_t hard_capabilities = 262'144;
constexpr std::size_t hard_string_bytes = 4'096;
constexpr std::size_t hard_total_string_bytes = 16U * 1'024U * 1'024U;
constexpr std::size_t hard_work = 64U * 1'024U * 1'024U;

struct Failure final { RuntimeScriptExecutionPlanError error; };

[[noreturn]] void fail(const RuntimeScriptExecutionPlanError error)
{
    throw Failure{error};
}

void cancelled(const std::stop_token token)
{
    if (token.stop_requested()) fail(RuntimeScriptExecutionPlanError::cancelled);
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
            trailing = 1; code_point = lead & 0x1fU; minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            trailing = 2; code_point = lead & 0x0fU; minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            trailing = 3; code_point = lead & 0x07U; minimum = 0x10000U;
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
            || (code_point >= 0xd800U && code_point <= 0xdfffU)) return false;
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
    JsonParser(std::string_view input, const RuntimeScriptExecutionPlanLimits& limits,
               std::stop_token token) noexcept
        : input_(input), limits_(limits), token_(token) {}

    [[nodiscard]] Json parse()
    {
        auto result = value(0);
        whitespace();
        if (offset_ != input_.size()) fail(RuntimeScriptExecutionPlanError::invalid_json);
        return result;
    }
    [[nodiscard]] std::size_t work() const noexcept { return work_; }

private:
    void charge(std::size_t amount = 1)
    {
        cancelled(token_);
        if (amount > limits_.max_work - work_)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
        work_ += amount;
    }
    void node()
    {
        charge();
        if (++nodes_ > limits_.max_json_nodes)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
    }
    void whitespace()
    {
        while (offset_ < input_.size()) {
            const auto byte = input_[offset_];
            if (byte != ' ' && byte != '\n' && byte != '\r' && byte != '\t') break;
            ++offset_; charge();
        }
    }
    [[nodiscard]] char take()
    {
        charge();
        if (offset_ == input_.size()) fail(RuntimeScriptExecutionPlanError::invalid_json);
        return input_[offset_++];
    }
    [[nodiscard]] static unsigned hex(char byte)
    {
        if (byte >= '0' && byte <= '9') return static_cast<unsigned>(byte - '0');
        if (byte >= 'a' && byte <= 'f') return static_cast<unsigned>(byte - 'a' + 10);
        if (byte >= 'A' && byte <= 'F') return static_cast<unsigned>(byte - 'A' + 10);
        fail(RuntimeScriptExecutionPlanError::invalid_json);
    }
    [[nodiscard]] std::uint32_t unicode_unit()
    {
        std::uint32_t result{};
        for (int index{}; index < 4; ++index) result = (result << 4U) | hex(take());
        return result;
    }
    static void append_utf8(std::string& output, std::uint32_t code_point)
    {
        if (code_point <= 0x7fU) output.push_back(static_cast<char>(code_point));
        else if (code_point <= 0x7ffU) {
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
            || value.size() > limits_.max_total_string_bytes - string_bytes_)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
    }
    void append_unicode(std::string& output)
    {
        const auto first = unicode_unit();
        if (first >= 0xdc00U && first <= 0xdfffU)
            fail(RuntimeScriptExecutionPlanError::invalid_json);
        if (first < 0xd800U || first > 0xdbffU) {
            append_utf8(output, first); check_string(output); return;
        }
        if (take() != '\\' || take() != 'u')
            fail(RuntimeScriptExecutionPlanError::invalid_json);
        const auto second = unicode_unit();
        if (second < 0xdc00U || second > 0xdfffU)
            fail(RuntimeScriptExecutionPlanError::invalid_json);
        append_utf8(output, 0x10000U + ((first - 0xd800U) << 10U) + second - 0xdc00U);
        check_string(output);
    }
    [[nodiscard]] std::string string()
    {
        if (take() != '"') fail(RuntimeScriptExecutionPlanError::invalid_json);
        std::string result;
        while (true) {
            const auto byte = take();
            if (byte == '"') break;
            if (static_cast<unsigned char>(byte) < 0x20U)
                fail(RuntimeScriptExecutionPlanError::invalid_json);
            if (byte != '\\') result.push_back(byte);
            else {
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
                    default: fail(RuntimeScriptExecutionPlanError::invalid_json);
                }
            }
            check_string(result);
        }
        if (result.size() > limits_.max_total_string_bytes - string_bytes_)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
        string_bytes_ += result.size();
        return result;
    }
    [[nodiscard]] std::uint64_t number()
    {
        const auto begin = offset_;
        if (input_[offset_] == '0') {
            static_cast<void>(take());
            if (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9')
                fail(RuntimeScriptExecutionPlanError::invalid_json);
        } else {
            if (input_[offset_] < '1' || input_[offset_] > '9')
                fail(RuntimeScriptExecutionPlanError::invalid_json);
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') static_cast<void>(take());
        }
        if (offset_ < input_.size() && (input_[offset_] == '.'
            || input_[offset_] == 'e' || input_[offset_] == 'E'))
            fail(RuntimeScriptExecutionPlanError::invalid_json);
        std::uint64_t result{};
        for (std::size_t index = begin; index < offset_; ++index) {
            const auto digit = static_cast<unsigned>(input_[index] - '0');
            if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U)
                fail(RuntimeScriptExecutionPlanError::invalid_value);
            result = result * 10U + digit;
        }
        return result;
    }
    [[nodiscard]] Json array(std::size_t depth)
    {
        if (depth >= limits_.max_json_depth)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
        static_cast<void>(take()); Json::Array result; whitespace();
        if (offset_ < input_.size() && input_[offset_] == ']') {
            static_cast<void>(take()); return Json{std::move(result)};
        }
        while (true) {
            result.push_back(value(depth + 1)); whitespace();
            const auto separator = take();
            if (separator == ']') break;
            if (separator != ',') fail(RuntimeScriptExecutionPlanError::invalid_json);
            whitespace();
        }
        return Json{std::move(result)};
    }
    [[nodiscard]] Json object(std::size_t depth)
    {
        if (depth >= limits_.max_json_depth)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
        static_cast<void>(take()); Json::Object result; whitespace();
        if (offset_ < input_.size() && input_[offset_] == '}') {
            static_cast<void>(take()); return Json{std::move(result)};
        }
        while (true) {
            if (offset_ == input_.size() || input_[offset_] != '"')
                fail(RuntimeScriptExecutionPlanError::invalid_json);
            auto key = string(); whitespace();
            if (take() != ':') fail(RuntimeScriptExecutionPlanError::invalid_json);
            whitespace(); auto item = value(depth + 1);
            if (!result.emplace(std::move(key), std::move(item)).second)
                fail(RuntimeScriptExecutionPlanError::invalid_json);
            whitespace(); const auto separator = take();
            if (separator == '}') break;
            if (separator != ',') fail(RuntimeScriptExecutionPlanError::invalid_json);
            whitespace();
        }
        return Json{std::move(result)};
    }
    [[nodiscard]] Json value(std::size_t depth)
    {
        cancelled(token_); whitespace();
        if (offset_ == input_.size()) fail(RuntimeScriptExecutionPlanError::invalid_json);
        node();
        if (input_[offset_] == '"') return Json{string()};
        if (input_[offset_] == '[') return array(depth);
        if (input_[offset_] == '{') return object(depth);
        if (input_[offset_] >= '0' && input_[offset_] <= '9') return Json{number()};
        fail(RuntimeScriptExecutionPlanError::invalid_json);
    }

    std::string_view input_;
    const RuntimeScriptExecutionPlanLimits& limits_;
    std::stop_token token_;
    std::size_t offset_{};
    std::size_t nodes_{};
    std::size_t string_bytes_{};
    std::size_t work_{};
};

template <std::size_t Size>
[[nodiscard]] bool exact_fields(const Json::Object& value,
                                const std::array<std::string_view, Size>& fields) noexcept
{
    return value.size() == fields.size()
        && std::ranges::all_of(fields, [&value](auto field) { return value.contains(field); });
}

[[nodiscard]] const Json::Object& object(const Json& value)
{
    const auto* result = std::get_if<Json::Object>(&value.value);
    if (!result) fail(RuntimeScriptExecutionPlanError::invalid_value);
    return *result;
}
[[nodiscard]] const Json::Array& array(const Json& value)
{
    const auto* result = std::get_if<Json::Array>(&value.value);
    if (!result) fail(RuntimeScriptExecutionPlanError::invalid_value);
    return *result;
}
[[nodiscard]] const std::string& string(const Json& value)
{
    const auto* result = std::get_if<std::string>(&value.value);
    if (!result) fail(RuntimeScriptExecutionPlanError::invalid_value);
    return *result;
}
[[nodiscard]] const std::string& text(const Json& value)
{
    const auto& result = string(value);
    if (result.empty() || result.find('\0') != std::string::npos)
        fail(RuntimeScriptExecutionPlanError::invalid_value);
    return result;
}
[[nodiscard]] std::uint64_t integer(const Json& value)
{
    const auto* result = std::get_if<std::uint64_t>(&value.value);
    if (!result) fail(RuntimeScriptExecutionPlanError::invalid_value);
    return *result;
}
[[nodiscard]] std::uint32_t version_integer(const Json& value, bool nonzero)
{
    const auto result = integer(value);
    if (result > std::numeric_limits<std::uint32_t>::max() || (nonzero && result == 0))
        fail(RuntimeScriptExecutionPlanError::invalid_value);
    return static_cast<std::uint32_t>(result);
}

[[nodiscard]] bool lower_segment(std::string_view value) noexcept
{
    if (value.empty() || value.front() < 'a' || value.front() > 'z') return false;
    return std::ranges::all_of(value.substr(1), [](char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
            || byte == '_' || byte == '-';
    });
}
[[nodiscard]] bool dotted_id(std::string_view value) noexcept
{
    bool dot{};
    for (std::size_t begin{}; begin < value.size();) {
        const auto end = value.find('.', begin);
        const auto part = value.substr(begin, end == std::string_view::npos
            ? value.size() - begin : end - begin);
        if (!lower_segment(part)) return false;
        if (end == std::string_view::npos) break;
        dot = true; begin = end + 1;
    }
    return dot;
}
[[nodiscard]] bool procedure_id(std::string_view value) noexcept
{
    if (value.empty() || value.front() == '/' || value.back() == '/'
        || value.find("//") != std::string_view::npos
        || value.find('\\') != std::string_view::npos
        || value.find(':') != std::string_view::npos
        || value.find('\0') != std::string_view::npos) return false;
    for (std::size_t begin{}; begin < value.size();) {
        const auto end = value.find('/', begin);
        const auto segment = value.substr(begin, end == std::string_view::npos
            ? value.size() - begin : end - begin);
        if (segment.empty() || segment == "." || segment == ".."
            || segment.front() == '-' || segment.back() == '-'
            || segment.front() == '.' || segment.back() == '.') return false;
        if (!std::ranges::all_of(segment, [](const char byte) {
                return (byte >= 'a' && byte <= 'z')
                    || (byte >= '0' && byte <= '9')
                    || byte == '-' || byte == '_' || byte == '.';
            })) return false;
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}
[[nodiscard]] bool lowercase_sha256(std::string_view value) noexcept
{
    return value.size() == 64 && std::ranges::all_of(value, [](char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
    });
}
[[nodiscard]] RuntimeScriptPackageVersion package_version(std::string_view value)
{
    RuntimeScriptPackageVersion result;
    std::array<std::uint32_t*, 3> outputs{&result.major, &result.minor, &result.patch};
    std::size_t begin{};
    for (std::size_t index{}; index < outputs.size(); ++index) {
        const auto end = value.find('.', begin);
        if ((index != outputs.size() - 1 && end == std::string_view::npos)
            || (index == outputs.size() - 1 && end != std::string_view::npos))
            fail(RuntimeScriptExecutionPlanError::invalid_value);
        const auto part = value.substr(begin, end == std::string_view::npos
            ? value.size() - begin : end - begin);
        if (part.empty() || (part.size() > 1 && part.front() == '0'))
            fail(RuntimeScriptExecutionPlanError::invalid_value);
        std::uint64_t number{};
        for (const char byte : part) {
            if (byte < '0' || byte > '9') fail(RuntimeScriptExecutionPlanError::invalid_value);
            const auto digit = static_cast<unsigned>(byte - '0');
            if (number > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U)
                fail(RuntimeScriptExecutionPlanError::invalid_value);
            number = number * 10U + digit;
        }
        *outputs[index] = static_cast<std::uint32_t>(number);
        begin = end + 1;
    }
    return result;
}

[[nodiscard]] const repository::RuntimeRepositoryReadEntry* manifested(
    const repository::RuntimeRepositoryReadView& scripts, std::string_view path) noexcept
{
    const auto entries = scripts.entries();
    const auto found = std::ranges::lower_bound(
        entries, path, {}, &repository::RuntimeRepositoryReadEntry::path);
    return found == entries.end() || found->path != path ? nullptr : &*found;
}

void validate_limits(const RuntimeScriptExecutionPlanLimits& limits)
{
    if (limits.max_manifest_bytes == 0 || limits.max_manifest_bytes > hard_manifest_bytes
        || limits.max_json_depth == 0 || limits.max_json_depth > hard_json_depth
        || limits.max_json_nodes == 0 || limits.max_json_nodes > hard_json_nodes
        || limits.max_modules == 0 || limits.max_modules > hard_modules
        || limits.max_procedures == 0 || limits.max_procedures > hard_procedures
        || limits.max_host_modules == 0 || limits.max_host_modules > hard_host_modules
        || limits.max_capabilities == 0 || limits.max_capabilities > hard_capabilities
        || limits.max_string_bytes == 0 || limits.max_string_bytes > hard_string_bytes
        || limits.max_total_string_bytes == 0
        || limits.max_total_string_bytes > hard_total_string_bytes
        || limits.max_work == 0 || limits.max_work > hard_work)
        fail(RuntimeScriptExecutionPlanError::invalid_limits);
}

[[nodiscard]] std::string ascii_fold(std::string_view value)
{
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](unsigned char byte) {
        return static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte);
    });
    return result;
}

}  // namespace

struct RuntimeScriptExecutionPlan::Impl final {
    std::string generation;
    std::string commit;
    std::string requested_task;
    bool legacy_alias{};
    RuntimeScriptTaskDescriptor task;
    std::string package_id;
    RuntimeScriptPackageVersion package_version;
    std::string package_build;
    std::vector<std::string> capabilities;
    std::vector<RuntimeScriptExecutionModule> modules;
    std::vector<std::string> procedure_ids;
    RuntimeScriptPackage package;
};

RuntimeScriptExecutionPlan::RuntimeScriptExecutionPlan(
    std::shared_ptr<const Impl> impl) noexcept : impl_(std::move(impl)) {}
RuntimeScriptExecutionPlan::~RuntimeScriptExecutionPlan() = default;
const std::string& RuntimeScriptExecutionPlan::generation() const noexcept { return impl_->generation; }
const std::string& RuntimeScriptExecutionPlan::commit() const noexcept { return impl_->commit; }
const std::string& RuntimeScriptExecutionPlan::requested_task() const noexcept { return impl_->requested_task; }
bool RuntimeScriptExecutionPlan::legacy_alias() const noexcept { return impl_->legacy_alias; }
const RuntimeScriptTaskDescriptor& RuntimeScriptExecutionPlan::task() const noexcept { return impl_->task; }
const std::string& RuntimeScriptExecutionPlan::package_id() const noexcept { return impl_->package_id; }
const RuntimeScriptPackageVersion& RuntimeScriptExecutionPlan::package_version() const noexcept { return impl_->package_version; }
const std::string& RuntimeScriptExecutionPlan::package_build() const noexcept { return impl_->package_build; }
std::span<const std::string> RuntimeScriptExecutionPlan::capabilities() const noexcept { return impl_->capabilities; }
std::span<const RuntimeScriptExecutionModule> RuntimeScriptExecutionPlan::modules() const noexcept { return impl_->modules; }
std::span<const std::string> RuntimeScriptExecutionPlan::procedure_ids() const noexcept { return impl_->procedure_ids; }
const RuntimeScriptPackage& RuntimeScriptExecutionPlan::package() const noexcept { return impl_->package; }

std::string_view runtime_script_execution_plan_error_name(
    const RuntimeScriptExecutionPlanError error) noexcept
{
    using enum RuntimeScriptExecutionPlanError;
    switch (error) {
        case none: return "RSE000_NONE";
        case invalid_limits: return "RSE001_INVALID_LIMITS";
        case invalid_resolution: return "RSE002_INVALID_RESOLUTION";
        case wrong_repository: return "RSE003_WRONG_REPOSITORY";
        case generation_mismatch: return "RSE004_GENERATION_MISMATCH";
        case commit_mismatch: return "RSE005_COMMIT_MISMATCH";
        case manifest_not_found: return "RSE006_MANIFEST_NOT_FOUND";
        case manifest_too_large: return "RSE007_MANIFEST_TOO_LARGE";
        case repository_read_failed: return "RSE008_REPOSITORY_READ_FAILED";
        case invalid_utf8: return "RSE009_INVALID_UTF8";
        case invalid_json: return "RSE010_INVALID_JSON";
        case limit_exceeded: return "RSE011_LIMIT_EXCEEDED";
        case manifest_schema_unsupported: return "RSE012_MANIFEST_SCHEMA_UNSUPPORTED";
        case invalid_field_set: return "RSE013_INVALID_FIELD_SET";
        case invalid_value: return "RSE014_INVALID_VALUE";
        case trust_evidence_required: return "RSE015_TRUST_EVIDENCE_REQUIRED";
        case trust_evidence_mismatch: return "RSE016_TRUST_EVIDENCE_MISMATCH";
        case unsupported_signature: return "RSE017_UNSUPPORTED_SIGNATURE";
        case unsupported_resources: return "RSE018_UNSUPPORTED_RESOURCES";
        case unsupported_profiles: return "RSE019_UNSUPPORTED_PROFILES";
        case language_mismatch: return "RSE020_LANGUAGE_MISMATCH";
        case entry_mismatch: return "RSE021_ENTRY_MISMATCH";
        case host_requirement_mismatch: return "RSE022_HOST_REQUIREMENT_MISMATCH";
        case capability_mismatch: return "RSE023_CAPABILITY_MISMATCH";
        case module_manifest_mismatch: return "RSE024_MODULE_MANIFEST_MISMATCH";
        case package_load_failed: return "RSE025_PACKAGE_LOAD_FAILED";
        case cancelled: return "RSE026_CANCELLED";
        case resource_exhausted: return "RSE027_RESOURCE_EXHAUSTED";
        case procedure_requirements_missing: return "RSE028_PROCEDURE_REQUIREMENTS_MISSING";
    }
    return "RSE999_UNKNOWN";
}

RuntimeScriptExecutionPlanResult build_runtime_script_execution_plan(
    const repository::RuntimeRepositoryReadView& scripts,
    const RuntimeScriptCatalogResolution& resolution,
    const RuntimeScriptRepositoryTrustEvidence* const trust_evidence,
    const RuntimeScriptExecutionPlanLimits& limits,
    const std::stop_token stop_token) noexcept
{
    try {
        validate_limits(limits);
        cancelled(stop_token);
        const auto* const resolved_task = resolution.resolved_task();
        if (resolved_task == nullptr || resolution.resolved_requested_task().empty()
            || resolution.generation().empty() || resolution.commit().empty())
            fail(RuntimeScriptExecutionPlanError::invalid_resolution);
        if (scripts.repository_id() != "scripts")
            fail(RuntimeScriptExecutionPlanError::wrong_repository);
        if (resolution.generation() != scripts.generation())
            fail(RuntimeScriptExecutionPlanError::generation_mismatch);
        if (resolution.commit() != scripts.commit())
            fail(RuntimeScriptExecutionPlanError::commit_mismatch);
        if (trust_evidence == nullptr)
            fail(RuntimeScriptExecutionPlanError::trust_evidence_required);
        if (!trust_evidence->covers(scripts.generation(), scripts.commit()))
            fail(RuntimeScriptExecutionPlanError::trust_evidence_mismatch);

        const auto& descriptor = *resolved_task;
        if (descriptor.package_manifest
            != descriptor.package_root + "/baas.package.json")
            fail(RuntimeScriptExecutionPlanError::invalid_resolution);
        const auto detached_signature = descriptor.package_root + "/baas.package.sig";
        if (manifested(scripts, detached_signature) != nullptr)
            fail(RuntimeScriptExecutionPlanError::unsupported_signature);
        const auto* manifest_entry = manifested(scripts, descriptor.package_manifest);
        if (!manifest_entry) fail(RuntimeScriptExecutionPlanError::manifest_not_found);
        if (manifest_entry->size > limits.max_manifest_bytes)
            fail(RuntimeScriptExecutionPlanError::manifest_too_large);
        std::vector<std::byte> bytes;
        try {
            bytes = scripts.read(descriptor.package_manifest, limits.max_manifest_bytes, stop_token);
        } catch (const repository::RuntimeRepositoryReadError& error) {
            if (error.code() == repository::RuntimeRepositoryReadErrorCode::cancelled)
                fail(RuntimeScriptExecutionPlanError::cancelled);
            if (error.code() == repository::RuntimeRepositoryReadErrorCode::resource_exhausted)
                fail(RuntimeScriptExecutionPlanError::resource_exhausted);
            fail(RuntimeScriptExecutionPlanError::repository_read_failed);
        }
        cancelled(stop_token);
        const std::string_view json_text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        if (!valid_utf8(json_text)) fail(RuntimeScriptExecutionPlanError::invalid_utf8);
        JsonParser parser{json_text, limits, stop_token};
        const auto root_value = parser.parse();
        std::size_t work = parser.work();
        auto charge = [&](std::size_t amount = 1) {
            cancelled(stop_token);
            if (amount > limits.max_work - work)
                fail(RuntimeScriptExecutionPlanError::limit_exceeded);
            work += amount;
        };

        const auto& root = object(root_value);
        if (root.contains("signature") || root.contains("signatures")
            || root.contains("signature_path"))
            fail(RuntimeScriptExecutionPlanError::unsupported_signature);
        constexpr std::array schema_one_root{
            std::string_view{"manifest_schema"}, std::string_view{"package"},
            std::string_view{"language"}, std::string_view{"entrypoint"},
            std::string_view{"host_modules"}, std::string_view{"capabilities"},
            std::string_view{"profiles"}, std::string_view{"modules"},
            std::string_view{"resources"}};
        constexpr std::array schema_two_root{
            std::string_view{"manifest_schema"}, std::string_view{"package"},
            std::string_view{"language"}, std::string_view{"entrypoint"},
            std::string_view{"host_modules"}, std::string_view{"capabilities"},
            std::string_view{"profiles"}, std::string_view{"modules"},
            std::string_view{"resources"}, std::string_view{"procedures"}};
        if (!root.contains("manifest_schema"))
            fail(RuntimeScriptExecutionPlanError::invalid_field_set);
        const auto manifest_schema = integer(root.at("manifest_schema"));
        if (manifest_schema != 1 && manifest_schema != 2)
            fail(RuntimeScriptExecutionPlanError::manifest_schema_unsupported);
        const bool has_limits = root.contains("limits");
        const auto contains_exact = [&root, has_limits](const auto& required) {
            return root.size() == required.size() + (has_limits ? 1U : 0U)
                && std::ranges::all_of(required, [&root](const auto field) {
                    return root.contains(field);
                });
        };
        if ((manifest_schema == 1 && !contains_exact(schema_one_root))
            || (manifest_schema == 2 && !contains_exact(schema_two_root)))
            fail(RuntimeScriptExecutionPlanError::invalid_field_set);

        auto impl = std::make_shared<RuntimeScriptExecutionPlan::Impl>();
        impl->generation = scripts.generation();
        impl->commit = scripts.commit();
        impl->requested_task = resolution.resolved_requested_task();
        impl->legacy_alias = resolution.resolved_legacy_alias();
        impl->task = descriptor;

        const auto& package = object(root.at("package"));
        if (package.size() != 2 && package.size() != 3)
            fail(RuntimeScriptExecutionPlanError::invalid_field_set);
        if (!package.contains("id") || !package.contains("version")
            || (package.size() == 3 && !package.contains("build")))
            fail(RuntimeScriptExecutionPlanError::invalid_field_set);
        impl->package_id = text(package.at("id"));
        if (!dotted_id(impl->package_id)) fail(RuntimeScriptExecutionPlanError::invalid_value);
        impl->package_version = package_version(text(package.at("version")));
        if (package.contains("build")) impl->package_build = text(package.at("build"));

        const auto& language = object(root.at("language"));
        constexpr std::array language_fields{
            std::string_view{"major"}, std::string_view{"min_minor"}};
        if (!exact_fields(language, language_fields))
            fail(RuntimeScriptExecutionPlanError::invalid_field_set);
        const RuntimeScriptLanguageVersion minimum_language{
            version_integer(language.at("major"), true),
            version_integer(language.at("min_minor"), false)};
        if (descriptor.language_version.major != minimum_language.major
            || descriptor.language_version.minor < minimum_language.minor)
            fail(RuntimeScriptExecutionPlanError::language_mismatch);

        const auto entrypoint = text(root.at("entrypoint"));
        ::baas::script::runtime::ModuleSpecifier entry;
        try {
            if (!entrypoint.ends_with(".baas"))
                fail(RuntimeScriptExecutionPlanError::entry_mismatch);
            entry = ::baas::script::runtime::validate_module_specifier(
                entrypoint.substr(0, entrypoint.size() - 5),
                limits.package_loader.is_nfc, limits.package_loader.specifier);
        } catch (const ::baas::script::runtime::ModuleSpecifierError&) {
            fail(RuntimeScriptExecutionPlanError::entry_mismatch);
        }
        if (entry.kind != ::baas::script::runtime::ModuleKind::Package
            || entry.manifest_source_path() != entrypoint
            || entry.canonical_id != descriptor.entry_module)
            fail(RuntimeScriptExecutionPlanError::entry_mismatch);

        std::vector<RuntimeScriptHostRequirement> manifest_hosts;
        const auto& hosts = object(root.at("host_modules"));
        if (hosts.size() > limits.max_host_modules)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
        manifest_hosts.reserve(hosts.size());
        for (const auto& [host_id, host_value] : hosts) {
            charge();
            const auto& requirement = object(host_value);
            constexpr std::array fields{
                std::string_view{"major"}, std::string_view{"min_minor"}};
            if (!exact_fields(requirement, fields))
                fail(RuntimeScriptExecutionPlanError::invalid_field_set);
            try {
                const auto canonical = ::baas::script::runtime::validate_module_specifier(
                    host_id, limits.package_loader.is_nfc,
                    limits.package_loader.specifier);
                if (canonical.kind != ::baas::script::runtime::ModuleKind::Host
                    || canonical.canonical_id != host_id)
                    fail(RuntimeScriptExecutionPlanError::invalid_value);
            } catch (const ::baas::script::runtime::ModuleSpecifierError&) {
                fail(RuntimeScriptExecutionPlanError::invalid_value);
            }
            manifest_hosts.push_back({host_id,
                version_integer(requirement.at("major"), true),
                version_integer(requirement.at("min_minor"), false), {}});
        }
        if (manifest_hosts.size() != descriptor.host_modules.size())
            fail(RuntimeScriptExecutionPlanError::host_requirement_mismatch);
        for (std::size_t index{}; index < manifest_hosts.size(); ++index) {
            if (manifest_hosts[index].canonical_id != descriptor.host_modules[index].canonical_id
                || manifest_hosts[index].major != descriptor.host_modules[index].major
                || manifest_hosts[index].min_minor != descriptor.host_modules[index].min_minor)
                fail(RuntimeScriptExecutionPlanError::host_requirement_mismatch);
        }

        const bool requires_procedure_host = std::ranges::any_of(
            manifest_hosts, [](const RuntimeScriptHostRequirement& requirement) {
                return requirement.canonical_id == "baas/procedure";
            });
        if (manifest_schema == 1) {
            if (requires_procedure_host)
                fail(RuntimeScriptExecutionPlanError::procedure_requirements_missing);
        } else {
            const auto& procedures = array(root.at("procedures"));
            if (procedures.size() > limits.max_procedures)
                fail(RuntimeScriptExecutionPlanError::limit_exceeded);
            impl->procedure_ids.reserve(procedures.size());
            std::set<std::string, std::less<>> exact_ids;
            std::set<std::string, std::less<>> folded_ids;
            for (const auto& procedure_value : procedures) {
                const auto& id = text(procedure_value);
                charge(id.size() + 1U);
                const auto folded = ascii_fold(id);
                if (!exact_ids.insert(id).second || !folded_ids.insert(folded).second
                    || !procedure_id(id))
                    fail(RuntimeScriptExecutionPlanError::invalid_value);
                impl->procedure_ids.push_back(id);
            }
            std::ranges::sort(impl->procedure_ids);
            if (requires_procedure_host && impl->procedure_ids.empty())
                fail(RuntimeScriptExecutionPlanError::procedure_requirements_missing);
            if (!requires_procedure_host && !impl->procedure_ids.empty())
                fail(RuntimeScriptExecutionPlanError::host_requirement_mismatch);
        }

        const auto& capabilities = array(root.at("capabilities"));
        if (capabilities.size() > limits.max_capabilities)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
        impl->capabilities.reserve(capabilities.size());
        for (const auto& capability : capabilities) {
            charge(); impl->capabilities.push_back(text(capability));
        }
        std::ranges::sort(impl->capabilities);
        if (std::ranges::adjacent_find(impl->capabilities) != impl->capabilities.end())
            fail(RuntimeScriptExecutionPlanError::invalid_value);
        std::vector<std::string> catalog_capabilities;
        for (const auto& host : descriptor.host_modules)
            catalog_capabilities.insert(catalog_capabilities.end(),
                host.capabilities.begin(), host.capabilities.end());
        std::ranges::sort(catalog_capabilities);
        catalog_capabilities.erase(
            std::ranges::unique(catalog_capabilities).begin(),
            catalog_capabilities.end());
        if (catalog_capabilities != impl->capabilities)
            fail(RuntimeScriptExecutionPlanError::capability_mismatch);

        if (!array(root.at("profiles")).empty())
            fail(RuntimeScriptExecutionPlanError::unsupported_profiles);
        if (!array(root.at("resources")).empty())
            fail(RuntimeScriptExecutionPlanError::unsupported_resources);

        RuntimeScriptPackageLoaderLimits loader_limits = limits.package_loader;
        if (has_limits) {
            const auto& package_limits = object(root.at("limits"));
            constexpr std::array fields{
                std::string_view{"source_bytes"}, std::string_view{"resource_bytes"},
                std::string_view{"module_count"}, std::string_view{"resource_count"}};
            if (!exact_fields(package_limits, fields))
                fail(RuntimeScriptExecutionPlanError::invalid_field_set);
            const auto source_bytes = integer(package_limits.at("source_bytes"));
            const auto module_count = integer(package_limits.at("module_count"));
            if (integer(package_limits.at("resource_bytes")) != 0
                || integer(package_limits.at("resource_count")) != 0)
                fail(RuntimeScriptExecutionPlanError::unsupported_resources);
            if (source_bytes == 0 || module_count == 0
                || source_bytes > std::numeric_limits<std::size_t>::max()
                || module_count > std::numeric_limits<std::size_t>::max())
                fail(RuntimeScriptExecutionPlanError::invalid_value);
            loader_limits.max_total_source_bytes = std::min(
                loader_limits.max_total_source_bytes, static_cast<std::size_t>(source_bytes));
            loader_limits.max_source_file_bytes = std::min(
                loader_limits.max_source_file_bytes,
                loader_limits.max_total_source_bytes);
            loader_limits.max_modules = std::min(
                loader_limits.max_modules, static_cast<std::size_t>(module_count));
        }

        const auto& modules = array(root.at("modules"));
        if (modules.empty() || modules.size() > limits.max_modules)
            fail(RuntimeScriptExecutionPlanError::limit_exceeded);
        impl->modules.reserve(modules.size());
        std::set<std::string, std::less<>> exact_paths;
        std::set<std::string, std::less<>> folded_paths;
        for (const auto& module_value : modules) {
            charge();
            const auto& module = object(module_value);
            constexpr std::array fields{
                std::string_view{"path"}, std::string_view{"size"},
                std::string_view{"sha256"}};
            if (!exact_fields(module, fields))
                fail(RuntimeScriptExecutionPlanError::invalid_field_set);
            RuntimeScriptExecutionModule item;
            const auto package_path = text(module.at("path"));
            item.logical_path = descriptor.package_root + "/" + package_path;
            item.size = integer(module.at("size"));
            item.sha256 = text(module.at("sha256"));
            if (!lowercase_sha256(item.sha256)
                || !exact_paths.insert(package_path).second
                || !folded_paths.insert(ascii_fold(package_path)).second
                || !package_path.ends_with(".baas"))
                fail(RuntimeScriptExecutionPlanError::invalid_value);
            try {
                const auto specifier = ::baas::script::runtime::validate_module_specifier(
                    std::string_view{package_path}.substr(0, package_path.size() - 5),
                    loader_limits.is_nfc, loader_limits.specifier);
                if (specifier.kind != ::baas::script::runtime::ModuleKind::Package
                    || specifier.manifest_source_path() != package_path)
                    fail(RuntimeScriptExecutionPlanError::invalid_value);
                item.canonical_module = specifier.canonical_id;
            } catch (const ::baas::script::runtime::ModuleSpecifierError&) {
                fail(RuntimeScriptExecutionPlanError::invalid_value);
            }
            impl->modules.push_back(std::move(item));
        }
        std::ranges::sort(impl->modules, {}, &RuntimeScriptExecutionModule::canonical_module);
        if (std::ranges::adjacent_find(impl->modules, {},
                &RuntimeScriptExecutionModule::canonical_module) != impl->modules.end())
            fail(RuntimeScriptExecutionPlanError::invalid_value);

        std::vector<RuntimeScriptPackageModuleManifest> package_manifest;
        package_manifest.reserve(impl->modules.size());
        for (const auto& module : impl->modules)
            package_manifest.push_back({module.canonical_module, module.logical_path,
                                        module.size, module.sha256});
        auto loaded = load_manifested_runtime_script_package(
            scripts, {resolution.generation(), resolution.commit()},
            descriptor.entry_module, package_manifest, loader_limits, stop_token);
        if (!loaded) {
            if (loaded.error == RuntimeScriptPackageLoadError::cancelled)
                fail(RuntimeScriptExecutionPlanError::cancelled);
            if (loaded.error == RuntimeScriptPackageLoadError::resource_exhausted)
                fail(RuntimeScriptExecutionPlanError::resource_exhausted);
            const auto plan_error = loaded.error
                    == RuntimeScriptPackageLoadError::module_manifest_mismatch
                ? RuntimeScriptExecutionPlanError::module_manifest_mismatch
                : RuntimeScriptExecutionPlanError::package_load_failed;
            return {std::nullopt, plan_error,
                    loaded.error, std::move(loaded.module), std::move(loaded.diagnostics)};
        }
        auto loaded_hosts = loaded.package->host_imports;
        std::ranges::sort(loaded_hosts);
        std::vector<std::string> declared_hosts;
        declared_hosts.reserve(descriptor.host_modules.size());
        for (const auto& host : descriptor.host_modules)
            declared_hosts.push_back(host.canonical_id);
        if (loaded_hosts != declared_hosts)
            fail(RuntimeScriptExecutionPlanError::host_requirement_mismatch);
        impl->package = std::move(*loaded.package);

        cancelled(stop_token);
        if (resolution.generation() != scripts.generation())
            fail(RuntimeScriptExecutionPlanError::generation_mismatch);
        if (resolution.commit() != scripts.commit())
            fail(RuntimeScriptExecutionPlanError::commit_mismatch);
        if (!trust_evidence->covers(scripts.generation(), scripts.commit()))
            fail(RuntimeScriptExecutionPlanError::trust_evidence_mismatch);
        return {RuntimeScriptExecutionPlan{std::shared_ptr<const RuntimeScriptExecutionPlan::Impl>{
                    std::move(impl)}}, RuntimeScriptExecutionPlanError::none,
                RuntimeScriptPackageLoadError::none, {}, {}};
    } catch (const Failure& error) {
        return {std::nullopt, error.error, RuntimeScriptPackageLoadError::none, {}, {}};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, RuntimeScriptExecutionPlanError::resource_exhausted,
                RuntimeScriptPackageLoadError::none, {}, {}};
    } catch (...) {
        return {std::nullopt, RuntimeScriptExecutionPlanError::invalid_value,
                RuntimeScriptPackageLoadError::none, {}, {}};
    }
}

}  // namespace baas::runtime::script
