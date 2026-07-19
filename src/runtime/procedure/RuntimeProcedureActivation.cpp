#include "runtime/procedure/RuntimeProcedureActivation.h"

#include "runtime/procedure/CoDetectPythonCompatDefinition.h"

#include "resources/ResourceSnapshot.h"
#include "runtime/json/StrictJson.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace baas::runtime::procedure {
namespace {

using Json = nlohmann::json;
namespace host = ::baas::script::host;
namespace repository = ::baas::runtime::repository;
namespace runtime_resources = ::baas::runtime::resources;
namespace snapshot_resources = ::baas::resources;
namespace runtime_script = ::baas::runtime::script;

struct LoaderFailure final {
    RuntimeProcedureActivationError error;
    std::string procedure_id;
};

[[noreturn]] void fail(const RuntimeProcedureActivationError error,
                       std::string procedure_id = {}) {
    throw LoaderFailure{error, std::move(procedure_id)};
}

void check_cancelled(const std::stop_token stop) {
    if (stop.stop_requested())
        fail(RuntimeProcedureActivationError::cancelled);
}

class WorkBudget final {
public:
    explicit WorkBudget(const std::size_t limit) noexcept : remaining_(limit) {}
    void charge(const std::size_t amount) {
        if (amount > remaining_)
            fail(RuntimeProcedureActivationError::work_limit_exceeded);
        remaining_ -= amount;
    }
private:
    std::size_t remaining_;
};

[[nodiscard]] bool valid_limits(const RuntimeProcedureActivationLimits& limits) noexcept {
    return limits.max_manifest_bytes != 0 && limits.max_entries != 0 &&
           limits.max_definition_bytes != 0 && limits.max_total_definition_bytes != 0 &&
           limits.max_definition_bytes <= limits.max_total_definition_bytes &&
           limits.max_terminals_per_procedure != 0 &&
           limits.max_effects_per_procedure != 0 && limits.max_effects_per_procedure <= 5 &&
           limits.max_resources_per_procedure != 0 && limits.max_string_bytes != 0 &&
           limits.max_result_schema_nodes_per_procedure != 0 &&
           limits.max_result_schema_depth != 0 &&
           limits.max_result_schema_depth <= 256 &&
           limits.max_total_string_bytes != 0 && limits.max_json_depth != 0 &&
           limits.max_json_nodes != 0 && limits.max_work != 0;
}

[[nodiscard]] bool exact_fields(
    const Json& object, const std::initializer_list<std::string_view> required) {
    if (!object.is_object() || object.size() != required.size())
        return false;
    for (const auto field : required)
        if (!object.contains(field))
            return false;
    for (const auto& [key, unused] : object.items()) {
        static_cast<void>(unused);
        if (std::find(required.begin(), required.end(), key) == required.end())
            return false;
    }
    return true;
}

[[nodiscard]] Json parse_strict_json(
    const std::string& text, const RuntimeProcedureActivationLimits& limits) {
    const auto result = ::baas::runtime::json::parse_strict_json(
        text, {limits.max_json_depth, limits.max_json_nodes,
               limits.max_string_bytes});
    if (result) return *result.document;
    using enum ::baas::runtime::json::StrictJsonError;
    if (result.error == invalid_utf8)
        fail(RuntimeProcedureActivationError::invalid_utf8);
    if (result.error == string_limit_exceeded)
        fail(RuntimeProcedureActivationError::string_limit_exceeded);
    if (result.error == resource_exhausted)
        fail(RuntimeProcedureActivationError::resource_exhausted);
    fail(RuntimeProcedureActivationError::invalid_json);
}

[[nodiscard]] const repository::RuntimeRepositoryReadEntry* manifested(
    const repository::RuntimeRepositoryReadView& scripts,
    const std::string_view path) noexcept {
    const auto entries = scripts.entries();
    const auto found = std::ranges::lower_bound(
        entries, path, {}, &repository::RuntimeRepositoryReadEntry::path);
    return found != entries.end() && found->path == path ? &*found : nullptr;
}

[[nodiscard]] std::vector<std::byte> read_verified(
    const repository::RuntimeRepositoryReadView& scripts, const std::string_view path,
    const std::uintmax_t limit, const RuntimeProcedureActivationError file_limit_error,
    const std::stop_token stop) {
    try {
        return scripts.read(path, limit, stop);
    } catch (const repository::RuntimeRepositoryReadError& error) {
        using enum repository::RuntimeRepositoryReadErrorCode;
        if (error.code() == cancelled)
            fail(RuntimeProcedureActivationError::cancelled);
        if (error.code() == resource_exhausted)
            fail(RuntimeProcedureActivationError::resource_exhausted);
        if (error.code() == file_limit_exceeded)
            fail(file_limit_error);
        fail(RuntimeProcedureActivationError::repository_read_failed);
    }
}

[[nodiscard]] std::string bytes_as_string(const std::vector<std::byte>& bytes) {
    std::string result(bytes.size(), '\0');
    if (!bytes.empty())
        std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

[[nodiscard]] std::string ascii_fold(const std::string_view value) {
    std::string result{value};
    std::ranges::transform(result, result.begin(), [](const unsigned char byte) {
        return static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte);
    });
    return result;
}

[[nodiscard]] bool lower_sha256(const std::string_view value) noexcept {
    return value.size() == 64 && std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

[[nodiscard]] bool canonical_definition_path(
    const std::string_view value, const std::size_t max_bytes) noexcept {
    if (value.empty() || value.size() > max_bytes || value.front() == '/' ||
        value.back() == '/' || value.find("//") != std::string_view::npos ||
        value.find('\\') != std::string_view::npos ||
        value.find(':') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos || !value.ends_with(".json") ||
        !value.starts_with("procedures/"))
        return false;
    std::size_t begin{};
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.front() == '.' || segment.back() == '.' ||
            segment.front() == '-' || segment.back() == '-')
            return false;
        for (const auto character : segment) {
            if (!(character >= 'a' && character <= 'z') &&
                !(character >= '0' && character <= '9') && character != '-' &&
                character != '_' && character != '.')
                return false;
        }
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return true;
}

void add_length_prefixed(std::string& target, const std::string_view value) {
    target += std::to_string(value.size());
    target.push_back(':');
    target.append(value);
    target.push_back(';');
}

[[nodiscard]] host::ProcedureEffect parse_effect(const std::string_view value,
                                                  const std::string_view procedure_id) {
    if (value == "capture") return host::ProcedureEffect::Capture;
    if (value == "vision") return host::ProcedureEffect::Vision;
    if (value == "input") return host::ProcedureEffect::Input;
    if (value == "wait") return host::ProcedureEffect::Wait;
    if (value == "foreground_check") return host::ProcedureEffect::ForegroundCheck;
    fail(RuntimeProcedureActivationError::invalid_manifest, std::string{procedure_id});
}

struct ParsedEntry final {
    std::string id;
    std::string path;
    std::size_t size{};
    std::string sha256;
    std::vector<RuntimeProcedureTerminalBinding> terminals;
    std::vector<host::ProcedureEffect> effects;
    std::vector<std::string> resources;
    std::vector<host::ProcedureResultFieldSchema> result_schema;
};

void charge_string(const std::string_view value,
                   const RuntimeProcedureActivationLimits& limits,
                   std::size_t& total_strings, WorkBudget& work,
                   const std::string_view procedure_id = {}) {
    if (value.empty() || value.size() > limits.max_string_bytes ||
        value.size() > limits.max_total_string_bytes -
            std::min(total_strings, limits.max_total_string_bytes))
        fail(RuntimeProcedureActivationError::string_limit_exceeded,
             std::string{procedure_id});
    total_strings += value.size();
    work.charge(value.size());
}

[[nodiscard]] host::ProcedureResultJsonType parse_result_type(
    const std::string_view value, const std::string_view procedure_id) {
    if (value == "null") return host::ProcedureResultJsonType::Null;
    if (value == "boolean") return host::ProcedureResultJsonType::Boolean;
    if (value == "integer") return host::ProcedureResultJsonType::Integer;
    if (value == "float") return host::ProcedureResultJsonType::Float;
    if (value == "string") return host::ProcedureResultJsonType::String;
    if (value == "array") return host::ProcedureResultJsonType::Array;
    if (value == "object") return host::ProcedureResultJsonType::Object;
    fail(RuntimeProcedureActivationError::invalid_manifest,
         std::string{procedure_id});
}

[[nodiscard]] bool canonical_result_name(
    const std::string_view value, const std::size_t maximum) noexcept {
    if (value.empty() || value.size() > maximum || value == "end" ||
        !(value.front() >= 'a' && value.front() <= 'z')) return false;
    return std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '_';
    });
}

[[nodiscard]] host::ProcedureResultFieldSchema parse_result_value_schema(
    const Json& value, const bool named,
    const RuntimeProcedureActivationLimits& limits,
    const std::string_view procedure_id, const std::size_t depth,
    std::size_t& nodes, std::size_t& total_strings, WorkBudget& work);

[[nodiscard]] std::vector<host::ProcedureResultFieldSchema> parse_result_fields(
    const Json& fields, const RuntimeProcedureActivationLimits& limits,
    const std::string_view procedure_id, const std::size_t depth,
    std::size_t& nodes, std::size_t& total_strings, WorkBudget& work) {
    if (!fields.is_array())
        fail(RuntimeProcedureActivationError::invalid_manifest,
             std::string{procedure_id});
    if (nodes > limits.max_result_schema_nodes_per_procedure ||
        fields.size() > limits.max_result_schema_nodes_per_procedure - nodes)
        fail(RuntimeProcedureActivationError::result_schema_limit_exceeded,
             std::string{procedure_id});
    std::vector<host::ProcedureResultFieldSchema> result;
    result.reserve(fields.size());
    for (const auto& field : fields) {
        auto parsed = parse_result_value_schema(
            field, true, limits, procedure_id, depth,
            nodes, total_strings, work);
        result.push_back(std::move(parsed));
    }

    // Duplicate detection must not turn a large, attacker-controlled sibling
    // list with common string prefixes into quadratic validation work. Use a
    // deterministic bottom-up merge sort and charge an upper bound for every
    // lexicographic comparison (the shared prefix plus its discriminator).
    std::vector<std::string_view> names;
    names.reserve(result.size());
    for (const auto& field : result) names.emplace_back(field.name);
    std::vector<std::string_view> scratch(names.size());
    for (std::size_t width = 1; width < names.size();) {
        const auto run = width > names.size() - width
            ? names.size() : width * 2;
        for (std::size_t begin = 0; begin < names.size(); begin += run) {
            const auto middle = std::min(names.size(), begin + width);
            const auto end = std::min(names.size(), begin + run);
            auto left = begin;
            auto right = middle;
            auto output = begin;
            while (left < middle && right < end) {
                work.charge(std::min(names[left].size(), names[right].size()) + 1);
                const auto comparison = names[left].compare(names[right]);
                if (comparison == 0)
                    fail(RuntimeProcedureActivationError::invalid_manifest,
                         std::string{procedure_id});
                scratch[output++] = comparison < 0
                    ? names[left++] : names[right++];
            }
            while (left < middle) scratch[output++] = names[left++];
            while (right < end) scratch[output++] = names[right++];
        }
        names.swap(scratch);
        if (width > names.size() / 2) break;
        width *= 2;
    }
    return result;
}

[[nodiscard]] host::ProcedureResultFieldSchema parse_result_value_schema(
    const Json& value, const bool named,
    const RuntimeProcedureActivationLimits& limits,
    const std::string_view procedure_id, const std::size_t depth,
    std::size_t& nodes, std::size_t& total_strings, WorkBudget& work) {
    if (depth > limits.max_result_schema_depth ||
        ++nodes > limits.max_result_schema_nodes_per_procedure)
        fail(RuntimeProcedureActivationError::result_schema_limit_exceeded,
             std::string{procedure_id});
    work.charge(1);
    if (!value.is_object() || !value.contains("type") ||
        !value.at("type").is_string())
        fail(RuntimeProcedureActivationError::invalid_manifest,
             std::string{procedure_id});
    const auto type_text = value.at("type").get<std::string>();
    charge_string(type_text, limits, total_strings, work, procedure_id);
    host::ProcedureResultFieldSchema result;
    result.type = parse_result_type(type_text, procedure_id);
    if (named) {
        if (!value.contains("name") || !value.at("name").is_string() ||
            !value.contains("required") || !value.at("required").is_boolean())
            fail(RuntimeProcedureActivationError::invalid_manifest,
                 std::string{procedure_id});
        result.name = value.at("name").get<std::string>();
        result.required = value.at("required").get<bool>();
        charge_string(result.name, limits, total_strings, work, procedure_id);
        if (!canonical_result_name(result.name, limits.max_string_bytes))
            fail(RuntimeProcedureActivationError::invalid_manifest,
                 std::string{procedure_id});
    } else {
        result.required = true;
    }
    switch (result.type) {
        case host::ProcedureResultJsonType::Object: {
            if (!(named
                    ? exact_fields(value, {"name", "required", "type", "fields"})
                    : exact_fields(value, {"type", "fields"})))
                fail(RuntimeProcedureActivationError::invalid_manifest,
                     std::string{procedure_id});
            result.children = parse_result_fields(
                value.at("fields"), limits, procedure_id, depth + 1,
                nodes, total_strings, work);
            break;
        }
        case host::ProcedureResultJsonType::Array: {
            if (!(named
                    ? exact_fields(value, {"name", "required", "type", "items"})
                    : exact_fields(value, {"type", "items"})))
                fail(RuntimeProcedureActivationError::invalid_manifest,
                     std::string{procedure_id});
            result.children.push_back(parse_result_value_schema(
                value.at("items"), false, limits, procedure_id, depth + 1,
                nodes, total_strings, work));
            break;
        }
        default:
            if (!(named
                    ? exact_fields(value, {"name", "required", "type"})
                    : exact_fields(value, {"type"})))
                fail(RuntimeProcedureActivationError::invalid_manifest,
                     std::string{procedure_id});
            break;
    }
    return result;
}

[[nodiscard]] ParsedEntry parse_entry(
    const Json& value, const RuntimeProcedureActivationLimits& limits,
    std::size_t& total_strings, WorkBudget& work) {
    const bool has_result = value.is_object() && value.contains("result");
    if (!(has_result
              ? exact_fields(value, {"id", "definition", "terminals", "effects",
                                     "resources", "result"})
              : exact_fields(value, {"id", "definition", "terminals", "effects",
                                     "resources"})) ||
        !value.at("id").is_string() || !value.at("definition").is_object() ||
        !value.at("terminals").is_array() || !value.at("effects").is_array() ||
        !value.at("resources").is_array())
        fail(RuntimeProcedureActivationError::invalid_manifest);

    ParsedEntry result;
    result.id = value.at("id").get<std::string>();
    charge_string(result.id, limits, total_strings, work);
    if (!host::valid_procedure_id(result.id, limits.max_string_bytes))
        fail(RuntimeProcedureActivationError::invalid_manifest, result.id);

    const auto& definition = value.at("definition");
    if (!exact_fields(definition, {"path", "size", "sha256"}) ||
        !definition.at("path").is_string() ||
        !definition.at("size").is_number_unsigned() ||
        !definition.at("sha256").is_string())
        fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
    result.path = definition.at("path").get<std::string>();
    result.sha256 = definition.at("sha256").get<std::string>();
    charge_string(result.path, limits, total_strings, work, result.id);
    charge_string(result.sha256, limits, total_strings, work, result.id);
    if (!canonical_definition_path(result.path, limits.max_string_bytes))
        fail(RuntimeProcedureActivationError::invalid_definition_path, result.id);
    if (!lower_sha256(result.sha256))
        fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
    const auto declared_size = definition.at("size").get<std::uint64_t>();
    if (declared_size > std::numeric_limits<std::size_t>::max() ||
        declared_size > limits.max_definition_bytes)
        fail(RuntimeProcedureActivationError::definition_too_large, result.id);
    result.size = static_cast<std::size_t>(declared_size);

    const auto& terminals = value.at("terminals");
    if (terminals.empty() || terminals.size() > limits.max_terminals_per_procedure)
        fail(RuntimeProcedureActivationError::terminal_limit_exceeded, result.id);
    std::set<std::string, std::less<>> sources;
    std::set<std::string, std::less<>> terminal_ids;
    result.terminals.reserve(terminals.size());
    for (const auto& terminal : terminals) {
        if (!exact_fields(terminal, {"source", "id"}) ||
            !terminal.at("source").is_string() || !terminal.at("id").is_string())
            fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
        RuntimeProcedureTerminalBinding binding{
            terminal.at("source").get<std::string>(), terminal.at("id").get<std::string>()};
        charge_string(binding.source, limits, total_strings, work, result.id);
        charge_string(binding.id, limits, total_strings, work, result.id);
        if (binding.source.find('\0') != std::string::npos ||
            !host::valid_procedure_terminal_id(binding.id, limits.max_string_bytes) ||
            !sources.insert(binding.source).second || !terminal_ids.insert(binding.id).second)
            fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
        result.terminals.push_back(std::move(binding));
    }

    const auto& effects = value.at("effects");
    if (effects.size() > limits.max_effects_per_procedure)
        fail(RuntimeProcedureActivationError::effect_limit_exceeded, result.id);
    std::set<host::ProcedureEffect> unique_effects;
    result.effects.reserve(effects.size());
    for (const auto& effect : effects) {
        if (!effect.is_string())
            fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
        const auto text = effect.get<std::string>();
        charge_string(text, limits, total_strings, work, result.id);
        const auto parsed = parse_effect(text, result.id);
        if (!unique_effects.insert(parsed).second)
            fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
        result.effects.push_back(parsed);
    }

    const auto& resource_ids = value.at("resources");
    if (resource_ids.size() > limits.max_resources_per_procedure)
        fail(RuntimeProcedureActivationError::resource_limit_exceeded, result.id);
    std::set<std::string, std::less<>> unique_resources;
    result.resources.reserve(resource_ids.size());
    for (const auto& resource : resource_ids) {
        if (!resource.is_string())
            fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
        auto resource_id = resource.get<std::string>();
        charge_string(resource_id, limits, total_strings, work, result.id);
        if (!snapshot_resources::valid_resource_id(resource_id, limits.max_string_bytes) ||
            !unique_resources.insert(resource_id).second)
            fail(RuntimeProcedureActivationError::invalid_manifest, result.id);
        result.resources.push_back(std::move(resource_id));
    }
    if (has_result) {
        std::size_t result_nodes{};
        result.result_schema = parse_result_fields(
            value.at("result"), limits, result.id, 1,
            result_nodes, total_strings, work);
    }
    return result;
}

[[nodiscard]] std::string implementation_sha256(const ParsedEntry& entry,
                                                 const std::string_view engine) {
    std::string material;
    add_length_prefixed(material, "baas.procedure.implementation/v2");
    add_length_prefixed(material, engine);
    add_length_prefixed(material, entry.sha256);
    add_length_prefixed(material, std::to_string(entry.terminals.size()));
    for (const auto& terminal : entry.terminals) {
        add_length_prefixed(material, terminal.source);
        add_length_prefixed(material, terminal.id);
    }
    const auto add_schema = [&](const auto& self,
                                const host::ProcedureResultFieldSchema& field) -> void {
        add_length_prefixed(material, field.name);
        add_length_prefixed(material, field.required ? "required" : "optional");
        add_length_prefixed(material, host::procedure_result_json_type_name(field.type));
        add_length_prefixed(material, std::to_string(field.children.size()));
        for (const auto& child : field.children) self(self, child);
    };
    add_length_prefixed(material, std::to_string(entry.result_schema.size()));
    for (const auto& field : entry.result_schema) add_schema(add_schema, field);
    return snapshot_resources::sha256_hex(std::as_bytes(std::span(material)));
}

#ifdef BAAS_RUNTIME_PROCEDURE_ACTIVATION_TESTING
std::atomic<RuntimeProcedureActivationHook> loader_hook{};
void invoke_hook(const RuntimeProcedureActivationHookPoint point) {
    if (const auto hook = loader_hook.load(std::memory_order_acquire))
        hook(point);
}
#else
void invoke_hook(const RuntimeProcedureActivationHookPoint) noexcept {}
#endif

}  // namespace

struct RuntimeProcedureActivation::Impl final {
    std::string generation;
    std::string scripts_commit;
    std::string resources_commit;
    std::string activation_id;
    std::shared_ptr<const runtime_resources::RuntimeResourceSnapshotActivation> resources;
    std::shared_ptr<const host::ProcedureSnapshot> snapshot;
    std::map<std::string, std::shared_ptr<const RuntimeProcedureDefinition>, std::less<>> definitions;
};

RuntimeProcedureDefinition::RuntimeProcedureDefinition(
    std::string procedure_id, std::string engine, std::string sha256,
    std::string implementation_sha256,
    std::vector<RuntimeProcedureTerminalBinding> terminals,
    std::vector<host::ProcedureResultFieldSchema> result_schema,
    std::shared_ptr<const std::vector<std::byte>> bytes) noexcept
    : procedure_id_(std::move(procedure_id)), engine_(std::move(engine)),
      sha256_(std::move(sha256)), implementation_sha256_(std::move(implementation_sha256)),
      terminals_(std::move(terminals)), result_schema_(std::move(result_schema)),
      bytes_(std::move(bytes)) {}

const std::string& RuntimeProcedureDefinition::procedure_id() const noexcept {
    return procedure_id_;
}
const std::string& RuntimeProcedureDefinition::engine() const noexcept { return engine_; }
const std::string& RuntimeProcedureDefinition::sha256() const noexcept { return sha256_; }
const std::string& RuntimeProcedureDefinition::implementation_sha256() const noexcept {
    return implementation_sha256_;
}
std::span<const RuntimeProcedureTerminalBinding>
RuntimeProcedureDefinition::terminals() const noexcept { return terminals_; }
std::span<const host::ProcedureResultFieldSchema>
RuntimeProcedureDefinition::result_schema() const noexcept { return result_schema_; }
std::span<const std::byte> RuntimeProcedureDefinition::bytes() const noexcept {
    return *bytes_;
}

RuntimeProcedureActivation::RuntimeProcedureActivation(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

RuntimeProcedureActivation::~RuntimeProcedureActivation() = default;

const std::string& RuntimeProcedureActivation::generation() const noexcept {
    return impl_->generation;
}
const std::string& RuntimeProcedureActivation::scripts_commit() const noexcept {
    return impl_->scripts_commit;
}
const std::string& RuntimeProcedureActivation::resources_commit() const noexcept {
    return impl_->resources_commit;
}
const std::string& RuntimeProcedureActivation::activation_id() const noexcept {
    return impl_->activation_id;
}
const std::shared_ptr<const host::ProcedureSnapshot>&
RuntimeProcedureActivation::snapshot() const noexcept { return impl_->snapshot; }
std::shared_ptr<const RuntimeProcedureDefinition>
RuntimeProcedureActivation::resolve_definition(const std::string_view procedure_id) const noexcept {
    const auto found = impl_->definitions.find(procedure_id);
    return found == impl_->definitions.end() ? nullptr : found->second;
}
std::size_t RuntimeProcedureActivation::procedure_count() const noexcept {
    return impl_->definitions.size();
}

std::string_view runtime_procedure_activation_error_name(
    const RuntimeProcedureActivationError error) noexcept {
    using enum RuntimeProcedureActivationError;
    switch (error) {
    case none: return "RPA000_NONE";
    case invalid_limits: return "RPA001_INVALID_LIMITS";
    case wrong_repository: return "RPA002_WRONG_REPOSITORY";
    case plan_mismatch: return "RPA003_PLAN_MISMATCH";
    case resource_activation_required: return "RPA004_RESOURCE_ACTIVATION_REQUIRED";
    case generation_mismatch: return "RPA005_GENERATION_MISMATCH";
    case procedure_requirements_empty: return "RPA006_PROCEDURE_REQUIREMENTS_EMPTY";
    case manifest_not_found: return "RPA007_MANIFEST_NOT_FOUND";
    case manifest_too_large: return "RPA008_MANIFEST_TOO_LARGE";
    case repository_read_failed: return "RPA009_REPOSITORY_READ_FAILED";
    case invalid_utf8: return "RPA010_INVALID_UTF8";
    case invalid_json: return "RPA011_INVALID_JSON";
    case invalid_manifest: return "RPA012_INVALID_MANIFEST";
    case entry_limit_exceeded: return "RPA013_ENTRY_LIMIT_EXCEEDED";
    case terminal_limit_exceeded: return "RPA014_TERMINAL_LIMIT_EXCEEDED";
    case effect_limit_exceeded: return "RPA015_EFFECT_LIMIT_EXCEEDED";
    case resource_limit_exceeded: return "RPA016_RESOURCE_LIMIT_EXCEEDED";
    case string_limit_exceeded: return "RPA017_STRING_LIMIT_EXCEEDED";
    case work_limit_exceeded: return "RPA018_WORK_LIMIT_EXCEEDED";
    case duplicate_entry: return "RPA019_DUPLICATE_ENTRY";
    case procedure_id_case_collision: return "RPA020_PROCEDURE_ID_CASE_COLLISION";
    case required_procedure_missing: return "RPA021_REQUIRED_PROCEDURE_MISSING";
    case invalid_definition_path: return "RPA022_INVALID_DEFINITION_PATH";
    case path_not_manifested: return "RPA023_PATH_NOT_MANIFESTED";
    case manifest_entry_mismatch: return "RPA024_MANIFEST_ENTRY_MISMATCH";
    case definition_too_large: return "RPA025_DEFINITION_TOO_LARGE";
    case total_definition_bytes_exceeded: return "RPA026_TOTAL_DEFINITION_BYTES_EXCEEDED";
    case definition_digest_mismatch: return "RPA027_DEFINITION_DIGEST_MISMATCH";
    case invalid_definition: return "RPA028_INVALID_DEFINITION";
    case unsupported_engine: return "RPA029_UNSUPPORTED_ENGINE";
    case resource_not_found: return "RPA030_RESOURCE_NOT_FOUND";
    case snapshot_validation_failed: return "RPA031_SNAPSHOT_VALIDATION_FAILED";
    case cancelled: return "RPA032_CANCELLED";
    case resource_exhausted: return "RPA033_RESOURCE_EXHAUSTED";
    case internal_failure: return "RPA034_INTERNAL_FAILURE";
    case result_schema_limit_exceeded: return "RPA035_RESULT_SCHEMA_LIMIT_EXCEEDED";
    }
    return "RPA999_UNKNOWN";
}

RuntimeProcedureActivationLoadResult load_runtime_procedure_activation(
    const repository::RuntimeRepositoryReadView& scripts,
    const runtime_script::RuntimeScriptExecutionPlan& plan,
    std::shared_ptr<const runtime_resources::RuntimeResourceSnapshotActivation> resources,
    const RuntimeProcedureActivationLimits& limits,
    const std::stop_token stop_token) noexcept {
    try {
        if (!valid_limits(limits))
            fail(RuntimeProcedureActivationError::invalid_limits);
        if (scripts.repository_id() != "scripts")
            fail(RuntimeProcedureActivationError::wrong_repository);
        if (plan.generation() != scripts.generation() || plan.commit() != scripts.commit())
            fail(RuntimeProcedureActivationError::plan_mismatch);
        if (!resources || !resources->snapshot())
            fail(RuntimeProcedureActivationError::resource_activation_required);
        if (resources->generation() != scripts.generation() ||
            resources->generation() != plan.generation())
            fail(RuntimeProcedureActivationError::generation_mismatch);
        if (plan.procedure_ids().empty())
            fail(RuntimeProcedureActivationError::procedure_requirements_empty);
        if (plan.procedure_ids().size() > limits.max_entries)
            fail(RuntimeProcedureActivationError::entry_limit_exceeded);
        check_cancelled(stop_token);

        const auto* manifest = manifested(scripts, runtime_procedure_manifest_path);
        if (!manifest)
            fail(RuntimeProcedureActivationError::manifest_not_found);
        if (manifest->size > limits.max_manifest_bytes)
            fail(RuntimeProcedureActivationError::manifest_too_large);
        WorkBudget work{limits.max_work};
        work.charge(static_cast<std::size_t>(manifest->size));
        work.charge(static_cast<std::size_t>(manifest->size));
        work.charge(static_cast<std::size_t>(manifest->size));
        invoke_hook(RuntimeProcedureActivationHookPoint::before_manifest_read);
        auto manifest_bytes = read_verified(
            scripts, runtime_procedure_manifest_path, limits.max_manifest_bytes,
            RuntimeProcedureActivationError::manifest_too_large, stop_token);
        check_cancelled(stop_token);
        auto document = parse_strict_json(bytes_as_string(manifest_bytes), limits);
        invoke_hook(RuntimeProcedureActivationHookPoint::after_manifest_parse);
        check_cancelled(stop_token);
        if (!exact_fields(document, {"schema", "entries"}) ||
            !document.at("schema").is_string() ||
            document.at("schema").get<std::string_view>() != runtime_procedure_manifest_schema ||
            !document.at("entries").is_array())
            fail(RuntimeProcedureActivationError::invalid_manifest);
        const auto& entry_documents = document.at("entries");
        if (entry_documents.size() > limits.max_entries)
            fail(RuntimeProcedureActivationError::entry_limit_exceeded);

        std::map<std::string, ParsedEntry, std::less<>> entries;
        std::map<std::string, std::string, std::less<>> folded_ids;
        std::set<std::string, std::less<>> paths;
        std::set<std::string, std::less<>> folded_paths;
        std::size_t total_strings{};
        // Preflight the complete identity namespace before validating any one
        // canonical entry. Collision results are therefore independent of
        // attacker-controlled manifest order.
        for (const auto& entry_document : entry_documents) {
            check_cancelled(stop_token);
            if (!entry_document.is_object() || !entry_document.contains("id") ||
                !entry_document.at("id").is_string())
                continue;
            const auto raw_id = entry_document.at("id").get<std::string>();
            work.charge(raw_id.size() + 1);
            const auto [folded, folded_inserted] =
                folded_ids.emplace(ascii_fold(raw_id), raw_id);
            if (!folded_inserted) {
                if (folded->second != raw_id)
                    fail(RuntimeProcedureActivationError::procedure_id_case_collision, raw_id);
                fail(RuntimeProcedureActivationError::duplicate_entry, raw_id);
            }
            if (!entry_document.contains("definition") ||
                !entry_document.at("definition").is_object() ||
                !entry_document.at("definition").contains("path") ||
                !entry_document.at("definition").at("path").is_string())
                continue;
            const auto raw_path =
                entry_document.at("definition").at("path").get<std::string>();
            work.charge(raw_path.size() + 1);
            if (!folded_paths.insert(ascii_fold(raw_path)).second)
                fail(RuntimeProcedureActivationError::duplicate_entry, raw_id);
        }
        folded_paths.clear();
        for (const auto& entry_document : entry_documents) {
            check_cancelled(stop_token);
            work.charge(1);
            auto entry = parse_entry(entry_document, limits, total_strings, work);
            if (!paths.insert(entry.path).second ||
                !folded_paths.insert(ascii_fold(entry.path)).second)
                fail(RuntimeProcedureActivationError::duplicate_entry, entry.id);
            const auto* repository_entry = manifested(scripts, entry.path);
            if (!repository_entry)
                fail(RuntimeProcedureActivationError::path_not_manifested, entry.id);
            if (repository_entry->size != entry.size || repository_entry->sha256 != entry.sha256)
                fail(RuntimeProcedureActivationError::manifest_entry_mismatch, entry.id);
            const auto id = entry.id;
            entries.emplace(id, std::move(entry));
        }

        std::vector<host::ProcedureDescriptorInput> descriptors;
        descriptors.reserve(plan.procedure_ids().size());
        auto impl = std::make_unique<RuntimeProcedureActivation::Impl>();
        impl->generation = scripts.generation();
        impl->scripts_commit = scripts.commit();
        impl->resources_commit = resources->commit();
        impl->resources = std::move(resources);
        std::size_t total_definition_bytes{};
        for (const auto& required_id : plan.procedure_ids()) {
            check_cancelled(stop_token);
            const auto found = entries.find(required_id);
            if (found == entries.end())
                fail(RuntimeProcedureActivationError::required_procedure_missing, required_id);
            auto& entry = found->second;
            if (entry.size > limits.max_total_definition_bytes - total_definition_bytes)
                fail(RuntimeProcedureActivationError::total_definition_bytes_exceeded, entry.id);
            total_definition_bytes += entry.size;
            work.charge(entry.size);
            work.charge(entry.size);
            work.charge(entry.size);
            work.charge(entry.size);
            invoke_hook(RuntimeProcedureActivationHookPoint::before_definition_read);
            auto definition_bytes = read_verified(
                scripts, entry.path, limits.max_definition_bytes,
                RuntimeProcedureActivationError::definition_too_large, stop_token);
            check_cancelled(stop_token);
            if (definition_bytes.size() != entry.size ||
                snapshot_resources::sha256_hex(definition_bytes) != entry.sha256)
                fail(RuntimeProcedureActivationError::definition_digest_mismatch, entry.id);
            auto definition_document = parse_strict_json(bytes_as_string(definition_bytes), limits);
            if (!exact_fields(definition_document, {"schema", "engine", "payload"}) ||
                !definition_document.at("schema").is_string() ||
                !definition_document.at("engine").is_string() ||
                !definition_document.at("payload").is_object() ||
                definition_document.at("schema").get<std::string_view>() !=
                    runtime_procedure_definition_schema)
                fail(RuntimeProcedureActivationError::invalid_definition, entry.id);
            auto engine = definition_document.at("engine").get<std::string>();
            charge_string(engine, limits, total_strings, work, entry.id);
            if (engine != runtime_procedure_legacy_engine
                && engine != co_detect_python_compat_engine)
                fail(RuntimeProcedureActivationError::unsupported_engine, entry.id);
            for (const auto& resource_id : entry.resources)
                if (!impl->resources->snapshot()->resolve(resource_id))
                    fail(RuntimeProcedureActivationError::resource_not_found, entry.id);

            const auto implementation = implementation_sha256(entry, engine);
            host::ProcedureDescriptorInput descriptor;
            descriptor.procedure_id = entry.id;
            descriptor.declared_effects = entry.effects;
            descriptor.resource_ids = entry.resources;
            descriptor.result_schema = entry.result_schema;
            descriptor.implementation_sha256 = implementation;
            descriptor.terminal_ids.reserve(entry.terminals.size());
            for (const auto& terminal : entry.terminals)
                descriptor.terminal_ids.push_back(terminal.id);
            host::ProcedureSnapshotLimits snapshot_limits;
            snapshot_limits.max_procedures = limits.max_entries;
            snapshot_limits.max_terminals_per_procedure = limits.max_terminals_per_procedure;
            snapshot_limits.max_effects_per_procedure = limits.max_effects_per_procedure;
            snapshot_limits.max_resources_per_procedure = limits.max_resources_per_procedure;
            snapshot_limits.max_result_schema_nodes_per_procedure =
                limits.max_result_schema_nodes_per_procedure;
            snapshot_limits.max_result_schema_depth = limits.max_result_schema_depth;
            snapshot_limits.max_string_bytes = limits.max_string_bytes;
            snapshot_limits.max_total_string_bytes = limits.max_total_string_bytes;
            snapshot_limits.max_validation_work = limits.max_work;
            descriptor.sha256 = host::procedure_descriptor_sha256(descriptor, snapshot_limits);
            descriptors.push_back(std::move(descriptor));

            auto owned_bytes = std::make_shared<const std::vector<std::byte>>(
                std::move(definition_bytes));
            auto definition = std::shared_ptr<const RuntimeProcedureDefinition>(
                new RuntimeProcedureDefinition{
                    entry.id, std::move(engine), entry.sha256, implementation,
                    entry.terminals, entry.result_schema, std::move(owned_bytes)});
            impl->definitions.emplace(entry.id, std::move(definition));
        }
        check_cancelled(stop_token);
        invoke_hook(RuntimeProcedureActivationHookPoint::before_snapshot_build);
        host::ProcedureSnapshotLimits snapshot_limits;
        snapshot_limits.max_procedures = limits.max_entries;
        snapshot_limits.max_terminals_per_procedure = limits.max_terminals_per_procedure;
        snapshot_limits.max_effects_per_procedure = limits.max_effects_per_procedure;
        snapshot_limits.max_resources_per_procedure = limits.max_resources_per_procedure;
        snapshot_limits.max_result_schema_nodes_per_procedure =
            limits.max_result_schema_nodes_per_procedure;
        snapshot_limits.max_result_schema_depth = limits.max_result_schema_depth;
        snapshot_limits.max_string_bytes = limits.max_string_bytes;
        snapshot_limits.max_total_string_bytes = limits.max_total_string_bytes;
        snapshot_limits.max_validation_work = limits.max_work;
        impl->snapshot = host::ProcedureSnapshot::build(
            std::move(descriptors), impl->resources->snapshot(), snapshot_limits);
        std::string activation_material;
        add_length_prefixed(activation_material, "baas.runtime-procedure.activation/v1");
        add_length_prefixed(activation_material, impl->generation);
        add_length_prefixed(activation_material, impl->scripts_commit);
        add_length_prefixed(activation_material, impl->resources_commit);
        add_length_prefixed(activation_material, impl->snapshot->snapshot_id());
        impl->activation_id = snapshot_resources::sha256_hex(
            std::as_bytes(std::span(activation_material)));
        check_cancelled(stop_token);
        invoke_hook(RuntimeProcedureActivationHookPoint::before_publication);
        check_cancelled(stop_token);
        auto activation = std::shared_ptr<const RuntimeProcedureActivation>(
            new RuntimeProcedureActivation{std::move(impl)});
        check_cancelled(stop_token);
        return {std::move(activation), RuntimeProcedureActivationError::none, {}};
    } catch (const LoaderFailure& failure) {
        return {{}, failure.error, failure.procedure_id};
    } catch (const host::ProcedureSnapshotError&) {
        return {{}, RuntimeProcedureActivationError::snapshot_validation_failed, {}};
    } catch (const nlohmann::json::parse_error&) {
        return {{}, RuntimeProcedureActivationError::invalid_json, {}};
    } catch (const nlohmann::json::exception&) {
        return {{}, RuntimeProcedureActivationError::invalid_manifest, {}};
    } catch (const std::bad_alloc&) {
        return {{}, RuntimeProcedureActivationError::resource_exhausted, {}};
    } catch (...) {
        return {{}, RuntimeProcedureActivationError::internal_failure, {}};
    }
}

#ifdef BAAS_RUNTIME_PROCEDURE_ACTIVATION_TESTING
void set_runtime_procedure_activation_hook(
    const RuntimeProcedureActivationHook hook) noexcept {
    loader_hook.store(hook, std::memory_order_release);
}
#endif

}  // namespace baas::runtime::procedure
