#include "script/runtime/HostModuleRegistry.h"

#include <algorithm>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace baas::script::runtime {
namespace {

using enum HostRegistryErrorCode;

struct Budget {
    const HostRegistryLimits& limits;
    std::size_t work{};
    std::size_t string_bytes{};

    void charge_work()
    {
        if (work >= limits.max_validation_work) {
            throw HostRegistryError(
                ValidationWorkLimitExceeded,
                "host registry validation work limit exceeded");
        }
        ++work;
    }

    void charge_string(const std::string_view value)
    {
        charge_work();
        if (value.size() > limits.max_string_bytes
            || value.size() > limits.max_total_string_bytes
            || string_bytes > limits.max_total_string_bytes - value.size()) {
            throw HostRegistryError(
                StringBudgetExceeded,
                "host registry string budget exceeded");
        }
        string_bytes += value.size();
    }
};

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept
{
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index]);
        std::size_t width = 0;
        std::uint32_t code_point = 0;
        if (first <= 0x7F) {
            ++index;
            continue;
        }
        if (first >= 0xC2 && first <= 0xDF) {
            width = 2;
            code_point = first & 0x1F;
        } else if (first >= 0xE0 && first <= 0xEF) {
            width = 3;
            code_point = first & 0x0F;
        } else if (first >= 0xF0 && first <= 0xF4) {
            width = 4;
            code_point = first & 0x07;
        } else {
            return false;
        }
        if (index + width > value.size()) return false;
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto byte = static_cast<unsigned char>(value[index + offset]);
            if ((byte & 0xC0) != 0x80) return false;
            code_point = (code_point << 6) | (byte & 0x3F);
        }
        if ((width == 2 && code_point < 0x80)
            || (width == 3 && code_point < 0x800)
            || (width == 4 && code_point < 0x10000)
            || (code_point >= 0xD800 && code_point <= 0xDFFF)
            || code_point > 0x10FFFF) {
            return false;
        }
        index += width;
    }
    return true;
}

void require_utf8(const std::string_view value, const std::string_view subject)
{
    if (!valid_utf8(value)) {
        throw HostRegistryError(
            InvalidUtf8,
            std::string(subject) + " is not valid UTF-8");
    }
}

[[nodiscard]] bool is_lower_identifier(const std::string_view value) noexcept
{
    if (value.empty() || value.front() < 'a' || value.front() > 'z') return false;
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9') || character == '_';
    });
}

[[nodiscard]] bool is_dotted_identifier(const std::string_view value) noexcept
{
    bool saw_dot = false;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find('.', begin);
        const auto segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (!is_lower_identifier(segment)) return false;
        if (end == std::string_view::npos) break;
        saw_dot = true;
        begin = end + 1;
    }
    return saw_dot;
}

void validate_host_module_id(const std::string_view value, Budget& budget)
{
    budget.charge_string(value);
    require_utf8(value, "host module id");
    try {
        const auto specifier = validate_module_specifier(value);
        const auto suffix = value.starts_with("baas/") ? value.substr(5) : std::string_view{};
        if (specifier.kind != ModuleKind::Host || suffix.find('/') != std::string_view::npos
            || !is_lower_identifier(suffix)) {
            throw HostRegistryError(
                InvalidModuleId,
                "host module id must be canonical baas/<name>",
                std::string(value));
        }
    } catch (const ModuleSpecifierError& error) {
        if (error.code() == ModuleSpecifierErrorCode::InvalidUtf8) {
            throw HostRegistryError(InvalidUtf8, "host module id is not valid UTF-8");
        }
        throw HostRegistryError(
            InvalidModuleId,
            "host module id must be canonical baas/<name>",
            std::string(value));
    }
}

void validate_export_name(
    const std::string_view value,
    Budget& budget,
    const std::string_view module)
{
    budget.charge_string(value);
    require_utf8(value, "host export name");
    if (!is_lower_identifier(value)) {
        throw HostRegistryError(
            InvalidExportName,
            "host export name must be lowercase ASCII",
            std::string(module),
            std::string(value));
    }
}

void validate_capability(
    const std::string_view value,
    Budget& budget,
    const std::string_view module = {},
    const std::string_view export_name = {})
{
    budget.charge_string(value);
    require_utf8(value, "capability id");
    if (!is_dotted_identifier(value)) {
        throw HostRegistryError(
            InvalidCapabilityId,
            "capability id must be dotted lowercase ASCII",
            std::string(module),
            std::string(export_name),
            std::string(value));
    }
}

void validate_binding_id(
    const std::string_view value,
    const std::uint32_t major,
    Budget& budget,
    const std::string_view module,
    const std::string_view export_name)
{
    budget.charge_string(value);
    require_utf8(value, "binding id");
    const auto expected_suffix = ".v" + std::to_string(major);
    const bool shape = value.starts_with("host.") && value.ends_with(expected_suffix);
    const auto middle = shape
        ? value.substr(5, value.size() - 5 - expected_suffix.size())
        : std::string_view{};
    const auto separator = middle.find('.');
    if (!shape || separator == std::string_view::npos
        || middle.find('.', separator + 1) != std::string_view::npos
        || !is_lower_identifier(middle.substr(0, separator))
        || !is_lower_identifier(middle.substr(separator + 1))) {
        throw HostRegistryError(
            InvalidBindingId,
            "binding id must be host.<domain>.<operation>.v<major>",
            std::string(module),
            std::string(export_name));
    }
}

void validate_limits(const HostRegistryLimits& limits)
{
    if (limits.max_module_versions == 0 || limits.max_exports_per_module == 0
        || limits.max_total_exports == 0 || limits.max_capabilities == 0
        || limits.max_imports == 0 || limits.max_string_bytes == 0
        || limits.max_total_string_bytes == 0 || limits.max_validation_work == 0) {
        throw HostRegistryError(InvalidLimits, "host registry limits must be positive");
    }
}

using CapabilitySet = std::set<std::string, std::less<>>;

[[nodiscard]] CapabilitySet make_capability_set(
    const std::vector<std::string>& capabilities,
    const HostRegistryLimits& limits,
    Budget& budget)
{
    if (capabilities.size() > limits.max_capabilities) {
        throw HostRegistryError(CapabilityLimitExceeded, "capability count exceeds limit");
    }
    CapabilitySet result;
    for (const auto& capability : capabilities) {
        validate_capability(capability, budget);
        if (!result.insert(capability).second) {
            throw HostRegistryError(
                DuplicateCapability,
                "duplicate capability declaration",
                {},
                {},
                capability);
        }
    }
    return result;
}

}  // namespace

std::string_view host_registry_error_code_name(const HostRegistryErrorCode code) noexcept
{
    using enum HostRegistryErrorCode;
    switch (code) {
        case InvalidLimits: return "HREG001_INVALID_LIMITS";
        case ModuleVersionLimitExceeded: return "HREG002_MODULE_VERSION_LIMIT_EXCEEDED";
        case ExportLimitExceeded: return "HREG003_EXPORT_LIMIT_EXCEEDED";
        case CapabilityLimitExceeded: return "HREG004_CAPABILITY_LIMIT_EXCEEDED";
        case ImportLimitExceeded: return "HREG005_IMPORT_LIMIT_EXCEEDED";
        case StringBudgetExceeded: return "HREG006_STRING_BUDGET_EXCEEDED";
        case ValidationWorkLimitExceeded: return "HREG007_VALIDATION_WORK_LIMIT_EXCEEDED";
        case InvalidUtf8: return "HREG008_INVALID_UTF8";
        case InvalidModuleId: return "HREG009_INVALID_MODULE_ID";
        case InvalidExportName: return "HREG010_INVALID_EXPORT_NAME";
        case InvalidCapabilityId: return "HREG011_INVALID_CAPABILITY_ID";
        case InvalidBindingId: return "HREG012_INVALID_BINDING_ID";
        case DuplicateModuleVersion: return "HREG013_DUPLICATE_MODULE_VERSION";
        case DuplicateExport: return "HREG014_DUPLICATE_EXPORT";
        case DuplicateBinding: return "HREG015_DUPLICATE_BINDING";
        case IncompatibleMinorContract: return "HREG016_INCOMPATIBLE_MINOR_CONTRACT";
        case DuplicateManifestModule: return "HREG017_DUPLICATE_MANIFEST_MODULE";
        case DuplicateCapability: return "HREG018_DUPLICATE_CAPABILITY";
        case DuplicateImport: return "HREG019_DUPLICATE_IMPORT";
        case UndeclaredModule: return "HREG020_UNDECLARED_MODULE";
        case ModuleUnavailable: return "HREG021_MODULE_UNAVAILABLE";
        case VersionIncompatible: return "HREG022_VERSION_INCOMPATIBLE";
        case UnknownExport: return "HREG023_UNKNOWN_EXPORT";
        case UndeclaredCapability: return "HREG024_UNDECLARED_CAPABILITY";
        case CapabilityDenied: return "HREG025_CAPABILITY_DENIED";
    }
    return "HREG000_UNKNOWN";
}

HostRegistryError::HostRegistryError(
    const HostRegistryErrorCode code,
    std::string message,
    std::string module,
    std::string export_name,
    std::string capability,
    std::string layer)
    : std::runtime_error(std::move(message)), code_(code), module_(std::move(module)),
      export_name_(std::move(export_name)), capability_(std::move(capability)),
      layer_(std::move(layer))
{
}

HostModuleRegistry::HostModuleRegistry(
    std::vector<HostModuleDescriptor> descriptors,
    const HostRegistryLimits limits)
    : limits_(limits)
{
    validate_limits(limits_);
    if (descriptors.size() > limits_.max_module_versions) {
        throw HostRegistryError(
            ModuleVersionLimitExceeded,
            "host module version count exceeds limit");
    }

    Budget budget{limits_};
    std::size_t total_exports = 0;
    using BindingOwner = std::tuple<std::string, std::uint32_t, std::string, std::string>;
    std::map<std::string, BindingOwner, std::less<>> binding_owners;
    for (auto& descriptor : descriptors) {
        budget.charge_work();
        validate_host_module_id(descriptor.canonical_id, budget);
        if (descriptor.exports.size() > limits_.max_exports_per_module
            || descriptor.exports.size() > limits_.max_total_exports
            || total_exports > limits_.max_total_exports - descriptor.exports.size()) {
            throw HostRegistryError(
                ExportLimitExceeded,
                "host export count exceeds limit",
                descriptor.canonical_id);
        }
        total_exports += descriptor.exports.size();

        std::map<std::string, HostExportDescriptor, std::less<>> ordered_exports;
        for (auto& host_export : descriptor.exports) {
            budget.charge_work();
            validate_export_name(host_export.export_name, budget, descriptor.canonical_id);
            validate_capability(
                host_export.capability,
                budget,
                descriptor.canonical_id,
                host_export.export_name);
            validate_binding_id(
                host_export.binding_id,
                descriptor.version.major,
                budget,
                descriptor.canonical_id,
                host_export.export_name);
            const auto [export_iterator, export_inserted] = ordered_exports.emplace(
                host_export.export_name, host_export);
            if (!export_inserted) {
                throw HostRegistryError(
                    DuplicateExport,
                    "duplicate export in host module version",
                    descriptor.canonical_id,
                    export_iterator->first);
            }
            const BindingOwner owner{
                descriptor.canonical_id,
                descriptor.version.major,
                host_export.export_name,
                host_export.capability};
            const auto [binding_iterator, binding_inserted] = binding_owners.emplace(
                host_export.binding_id, owner);
            if (!binding_inserted && binding_iterator->second != owner) {
                throw HostRegistryError(
                    DuplicateBinding,
                    "binding id has conflicting ownership",
                    descriptor.canonical_id,
                    host_export.export_name,
                    host_export.capability);
            }
        }
        descriptor.exports.clear();
        descriptor.exports.reserve(ordered_exports.size());
        for (auto& [name, host_export] : ordered_exports) {
            static_cast<void>(name);
            descriptor.exports.push_back(std::move(host_export));
        }

        auto& versions = modules_[descriptor.canonical_id];
        const auto [iterator, inserted] = versions.emplace(descriptor.version, std::move(descriptor));
        if (!inserted) {
            throw HostRegistryError(
                DuplicateModuleVersion,
                "duplicate host module version",
                iterator->second.canonical_id);
        }
        ++module_version_count_;
    }

    // Within one major, each higher minor must contain every earlier export
    // with identical binding and capability metadata.
    for (const auto& [module_id, versions] : modules_) {
        std::map<std::uint32_t, std::map<std::string, HostExportDescriptor, std::less<>>>
            accumulated;
        for (const auto& [version, descriptor] : versions) {
            budget.charge_work();
            auto& prior = accumulated[version.major];
            std::map<std::string, HostExportDescriptor, std::less<>> current;
            for (const auto& host_export : descriptor.exports) {
                current.emplace(host_export.export_name, host_export);
            }
            for (const auto& [name, previous] : prior) {
                const auto found = current.find(name);
                if (found == current.end() || found->second.binding_id != previous.binding_id
                    || found->second.capability != previous.capability) {
                    throw HostRegistryError(
                        IncompatibleMinorContract,
                        "higher minor removed or changed an existing export",
                        module_id,
                        name,
                        previous.capability);
                }
            }
            prior = std::move(current);
        }
    }
}

HostResolution HostModuleRegistry::resolve(const HostResolutionRequest& request) const
{
    Budget budget{limits_};
    if (request.declared_modules.size() > limits_.max_module_versions) {
        throw HostRegistryError(
            ModuleVersionLimitExceeded,
            "manifest host module count exceeds limit");
    }
    if (request.imports.size() > limits_.max_imports) {
        throw HostRegistryError(
            ImportLimitExceeded,
            "host import count exceeds limit");
    }

    std::map<std::string, HostModuleRequirement, std::less<>> requirements;
    for (const auto& requirement : request.declared_modules) {
        validate_host_module_id(requirement.canonical_id, budget);
        const auto [iterator, inserted] = requirements.emplace(
            requirement.canonical_id, requirement);
        if (!inserted) {
            throw HostRegistryError(
                DuplicateManifestModule,
                "duplicate manifest host module requirement",
                iterator->first);
        }
    }

    const auto declared = make_capability_set(
        request.declared_capabilities, limits_, budget);
    const auto policy = make_capability_set(
        request.policy_capabilities, limits_, budget);
    const auto platform = make_capability_set(
        request.platform_capabilities, limits_, budget);
    const auto task = make_capability_set(request.task_capabilities, limits_, budget);

    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> imports;
    for (const auto& import : request.imports) {
        validate_host_module_id(import.canonical_id, budget);
        if (import.exports.size() > limits_.max_exports_per_module) {
            throw HostRegistryError(
                ExportLimitExceeded,
                "imported export count exceeds limit",
                import.canonical_id);
        }
        auto [iterator, inserted] = imports.emplace(
            import.canonical_id, std::set<std::string, std::less<>>{});
        if (!inserted) {
            throw HostRegistryError(
                DuplicateImport,
                "duplicate host module import request",
                import.canonical_id);
        }
        for (const auto& export_name : import.exports) {
            validate_export_name(export_name, budget, import.canonical_id);
            if (!iterator->second.insert(export_name).second) {
                throw HostRegistryError(
                    DuplicateExport,
                    "duplicate imported host export",
                    import.canonical_id,
                    export_name);
            }
        }
    }

    for (const auto& [module_id, exports] : imports) {
        static_cast<void>(exports);
        budget.charge_work();
        if (!requirements.contains(module_id)) {
            throw HostRegistryError(
                UndeclaredModule,
                "host import is absent from manifest host_modules",
                module_id);
        }
    }

    HostResolution result;
    for (const auto& capability : declared) {
        budget.charge_work();
        if (policy.contains(capability) && platform.contains(capability)
            && task.contains(capability)) {
            result.effective_capabilities.push_back(capability);
        }
    }

    for (const auto& [module_id, requirement] : requirements) {
        budget.charge_work();
        const auto module = modules_.find(module_id);
        if (module == modules_.end()) {
            throw HostRegistryError(
                ModuleUnavailable,
                "manifest host module is not registered",
                module_id);
        }
        const HostModuleDescriptor* selected = nullptr;
        for (auto iterator = module->second.rbegin(); iterator != module->second.rend(); ++iterator) {
            budget.charge_work();
            if (iterator->first.major == requirement.major
                && iterator->first.minor >= requirement.min_minor) {
                selected = &iterator->second;
                break;
            }
        }
        if (selected == nullptr) {
            throw HostRegistryError(
                VersionIncompatible,
                "no registered same-major host module satisfies min_minor",
                module_id);
        }

        ResolvedHostModule resolved{module_id, selected->version, {}};
        const auto requested = imports.find(module_id);
        if (requested != imports.end()) {
            for (const auto& export_name : requested->second) {
                budget.charge_work();
                const auto host_export = std::lower_bound(
                    selected->exports.begin(),
                    selected->exports.end(),
                    export_name,
                    [](const HostExportDescriptor& candidate, const std::string_view name) {
                        return candidate.export_name < name;
                    });
                if (host_export == selected->exports.end()
                    || host_export->export_name != export_name) {
                    throw HostRegistryError(
                        UnknownExport,
                        "requested host export is absent from selected module version",
                        module_id,
                        export_name);
                }
                if (!declared.contains(host_export->capability)) {
                    throw HostRegistryError(
                        UndeclaredCapability,
                        "host export capability is absent from manifest capabilities",
                        module_id,
                        export_name,
                        host_export->capability,
                        "manifest");
                }
                const auto require_layer = [&](const CapabilitySet& layer_set,
                                               const std::string_view layer_name) {
                    if (!layer_set.contains(host_export->capability)) {
                        throw HostRegistryError(
                            CapabilityDenied,
                            "host export capability denied by narrowing layer",
                            module_id,
                            export_name,
                            host_export->capability,
                            std::string(layer_name));
                    }
                };
                require_layer(policy, "policy");
                require_layer(platform, "platform");
                require_layer(task, "task");
                resolved.bindings.push_back({
                    host_export->export_name,
                    host_export->binding_id,
                    host_export->capability});
            }
        }
        result.modules.push_back(std::move(resolved));
    }
    result.validation_work = budget.work;
    return result;
}

std::vector<std::string> HostModuleRegistry::canonical_module_ids() const
{
    std::vector<std::string> result;
    result.reserve(modules_.size());
    for (const auto& [module_id, versions] : modules_) {
        static_cast<void>(versions);
        result.push_back(module_id);
    }
    return result;
}

}  // namespace baas::script::runtime
