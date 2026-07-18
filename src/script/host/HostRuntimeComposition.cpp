#include "script/host/HostRuntimeComposition.h"

#include <set>
#include <utility>

namespace baas::script::host {
namespace {

static_assert(
    static_cast<std::uint8_t>(
        HostRuntimeCompositionErrorCode::LinkValidationWorkExceeded) == 9);

[[noreturn]] void fail(
    const HostRuntimeCompositionErrorCode code, const char* message)
{
    throw HostRuntimeCompositionError(code, message);
}

[[nodiscard]] bool valid_limits(
    const HostRuntimeCompositionLimits& limits) noexcept
{
    const auto& metadata = limits.metadata;
    const auto& bindings = limits.bindings;
    const auto& json = bindings.json_limits;
    return limits.max_contributions != 0 && limits.max_lifetime_owners != 0
        && limits.max_link_validation_work != 0
        && metadata.max_module_versions != 0
        && metadata.max_exports_per_module != 0
        && metadata.max_total_exports != 0
        && metadata.max_capabilities != 0 && metadata.max_imports != 0
        && metadata.max_string_bytes != 0
        && metadata.max_total_string_bytes != 0
        && metadata.max_validation_work != 0
        && bindings.max_bindings != 0
        && bindings.max_parameters_per_binding != 0
        && bindings.max_total_parameters != 0
        && bindings.max_string_bytes != 0
        && bindings.max_total_string_bytes != 0
        && bindings.max_validation_work != 0
        && bindings.max_safe_message_bytes != 0 && json.max_depth != 0
        && json.max_nodes != 0 && json.max_string_bytes != 0
        && json.max_total_bytes != 0 && json.max_work != 0;
}

void checked_add(
    std::size_t& total, const std::size_t amount, const std::size_t maximum,
    const HostRuntimeCompositionErrorCode code, const char* message)
{
    if (amount > maximum || total > maximum - amount) fail(code, message);
    total += amount;
}

}  // namespace

struct ComposedHostRuntime::State final {
    std::shared_ptr<const runtime::HostModuleRegistry> metadata;
    // Owners are declared before the native objects they support so reverse
    // destruction tears down handles and callbacks first.
    std::vector<std::shared_ptr<void>> lifetime_owners;
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings;
    std::shared_ptr<runtime::HostReleaseDispatcher> handles;
};

std::string_view host_runtime_composition_error_code_name(
    const HostRuntimeCompositionErrorCode code) noexcept
{
    using enum HostRuntimeCompositionErrorCode;
    switch (code) {
        case InvalidLimits: return "HCOMP001_INVALID_LIMITS";
        case ContributionLimitExceeded:
            return "HCOMP002_CONTRIBUTION_LIMIT_EXCEEDED";
        case LifetimeOwnerLimitExceeded:
            return "HCOMP003_LIFETIME_OWNER_LIMIT_EXCEEDED";
        case MissingMetadata: return "HCOMP004_MISSING_METADATA";
        case MissingBindings: return "HCOMP005_MISSING_BINDINGS";
        case MultipleReleaseDispatchers:
            return "HCOMP006_MULTIPLE_RELEASE_DISPATCHERS";
        case UnboundExport: return "HCOMP007_UNBOUND_EXPORT";
        case OrphanBinding: return "HCOMP008_ORPHAN_BINDING";
        case LinkValidationWorkExceeded:
            return "HCOMP009_LINK_VALIDATION_WORK_EXCEEDED";
    }
    return "HCOMP000_UNKNOWN";
}

runtime::SynchronousHostOptions ComposedHostRuntime::options() const
{
    runtime::SynchronousHostOptions result;
    if (!state_) return result;
    result.metadata = state_->metadata;
    result.bindings = state_->bindings;
    result.handles = state_->handles;
    result.lifetime_owner = state_;
    return result;
}

std::size_t ComposedHostRuntime::module_version_count() const noexcept
{
    return state_ && state_->metadata
        ? state_->metadata->module_version_count() : 0;
}

std::size_t ComposedHostRuntime::binding_count() const noexcept
{
    return state_ && state_->bindings ? state_->bindings->size() : 0;
}

HostRuntimeContribution make_host_runtime_contribution(
    std::shared_ptr<const runtime::HostModuleRegistry> metadata,
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings,
    std::shared_ptr<runtime::HostReleaseDispatcher> handles,
    std::vector<std::shared_ptr<void>> lifetime_owners)
{
    if (!metadata)
        fail(HostRuntimeCompositionErrorCode::MissingMetadata,
             "Host runtime contribution metadata is absent");
    if (!bindings)
        fail(HostRuntimeCompositionErrorCode::MissingBindings,
             "Host runtime contribution bindings are absent");
    return {metadata->descriptors(), bindings->bindings(),
            std::move(lifetime_owners), std::move(handles)};
}

ComposedHostRuntime compose_host_runtime(
    std::vector<HostRuntimeContribution> contributions,
    const HostRuntimeCompositionLimits limits)
{
    if (!valid_limits(limits))
        fail(HostRuntimeCompositionErrorCode::InvalidLimits,
             "Host runtime composition limits must be non-zero");
    if (contributions.size() > limits.max_contributions)
        fail(HostRuntimeCompositionErrorCode::ContributionLimitExceeded,
             "Host runtime contribution limit exceeded");

    std::size_t metadata_count{};
    std::size_t binding_count{};
    std::size_t owner_count{};
    std::size_t link_validation_work{};
    for (const auto& contribution : contributions) {
        checked_add(metadata_count, contribution.metadata.size(),
                    limits.metadata.max_module_versions,
                    HostRuntimeCompositionErrorCode::ContributionLimitExceeded,
                    "Host runtime metadata contribution limit exceeded");
        checked_add(binding_count, contribution.bindings.size(),
                    limits.bindings.max_bindings,
                    HostRuntimeCompositionErrorCode::ContributionLimitExceeded,
                    "Host runtime binding contribution limit exceeded");
        checked_add(owner_count, contribution.lifetime_owners.size(),
                    limits.max_lifetime_owners,
                    HostRuntimeCompositionErrorCode::LifetimeOwnerLimitExceeded,
                    "Host runtime lifetime owner limit exceeded");

        std::set<std::string_view> native_binding_ids;
        for (const auto& binding : contribution.bindings) {
            checked_add(
                link_validation_work, 1, limits.max_link_validation_work,
                HostRuntimeCompositionErrorCode::LinkValidationWorkExceeded,
                "Host runtime link validation work limit exceeded");
            native_binding_ids.insert(binding.binding_id);
        }
        std::set<std::string_view> exported_binding_ids;
        for (const auto& descriptor : contribution.metadata) {
            for (const auto& exported : descriptor.exports) {
                checked_add(
                    link_validation_work, 1, limits.max_link_validation_work,
                    HostRuntimeCompositionErrorCode::LinkValidationWorkExceeded,
                    "Host runtime link validation work limit exceeded");
                if (!native_binding_ids.contains(exported.binding_id))
                    fail(HostRuntimeCompositionErrorCode::UnboundExport,
                         "Host export binding is absent from its contribution");
                exported_binding_ids.insert(exported.binding_id);
            }
        }
        for (const auto binding_id : native_binding_ids) {
            checked_add(
                link_validation_work, 1, limits.max_link_validation_work,
                HostRuntimeCompositionErrorCode::LinkValidationWorkExceeded,
                "Host runtime link validation work limit exceeded");
            if (!exported_binding_ids.contains(binding_id))
                fail(HostRuntimeCompositionErrorCode::OrphanBinding,
                     "native Host binding has no export in its contribution");
        }
    }

    std::vector<runtime::HostModuleDescriptor> metadata;
    std::vector<runtime::SynchronousNativeBinding> bindings;
    std::vector<std::shared_ptr<void>> owners;
    metadata.reserve(metadata_count);
    bindings.reserve(binding_count);
    owners.reserve(owner_count);
    std::shared_ptr<runtime::HostReleaseDispatcher> handles;

    for (auto& contribution : contributions) {
        if (contribution.handles) {
            if (handles && handles != contribution.handles)
                fail(HostRuntimeCompositionErrorCode::MultipleReleaseDispatchers,
                     "one evaluator cannot own multiple release dispatchers");
            handles = std::move(contribution.handles);
        }
        for (auto& descriptor : contribution.metadata)
            metadata.push_back(std::move(descriptor));
        for (auto& binding : contribution.bindings)
            bindings.push_back(std::move(binding));
        for (auto& owner : contribution.lifetime_owners)
            owners.push_back(std::move(owner));
    }

    auto composed_metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::move(metadata), limits.metadata);
    auto composed_bindings =
        std::make_shared<const runtime::SynchronousNativeBindingSet>(
            std::move(bindings), limits.bindings);
    auto state = std::make_shared<const ComposedHostRuntime::State>(
        ComposedHostRuntime::State{
            std::move(composed_metadata), std::move(owners),
            std::move(composed_bindings), std::move(handles)});
    return ComposedHostRuntime(std::move(state));
}

}  // namespace baas::script::host
