#pragma once

#include "script/runtime/HostModuleRegistry.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace baas::script::host {

struct HostRuntimeContribution final {
    std::vector<runtime::HostModuleDescriptor> metadata;
    std::vector<runtime::SynchronousNativeBinding> bindings;
    std::vector<std::shared_ptr<void>> lifetime_owners;
    // Declared last so it is destroyed before the explicit adapter owners.
    std::shared_ptr<runtime::HostReleaseDispatcher> handles;
};

struct HostRuntimeCompositionLimits final {
    std::size_t max_contributions{64};
    std::size_t max_lifetime_owners{1'024};
    std::size_t max_link_validation_work{100'000};
    runtime::HostRegistryLimits metadata{};
    runtime::SynchronousHostLimits bindings{};
};

enum class HostRuntimeCompositionErrorCode : std::uint8_t {
    InvalidLimits = 1,
    ContributionLimitExceeded = 2,
    LifetimeOwnerLimitExceeded = 3,
    MissingMetadata = 4,
    MissingBindings = 5,
    MultipleReleaseDispatchers = 6,
    UnboundExport = 7,
    OrphanBinding = 8,
    LinkValidationWorkExceeded = 9,
};

[[nodiscard]] std::string_view host_runtime_composition_error_code_name(
    HostRuntimeCompositionErrorCode code) noexcept;

class HostRuntimeCompositionError final : public std::runtime_error {
public:
    HostRuntimeCompositionError(
        HostRuntimeCompositionErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    [[nodiscard]] HostRuntimeCompositionErrorCode code() const noexcept {
        return code_;
    }

private:
    HostRuntimeCompositionErrorCode code_;
};

class ComposedHostRuntime final {
public:
    // The returned options retain the entire composed state. Production callers
    // must start from this value rather than copying individual adapter pointers.
    [[nodiscard]] runtime::SynchronousHostOptions options() const;
    [[nodiscard]] std::size_t module_version_count() const noexcept;
    [[nodiscard]] std::size_t binding_count() const noexcept;

private:
    struct State;
    explicit ComposedHostRuntime(std::shared_ptr<const State> state)
        : state_(std::move(state)) {}
    std::shared_ptr<const State> state_;

    friend ComposedHostRuntime compose_host_runtime(
        std::vector<HostRuntimeContribution>, HostRuntimeCompositionLimits);
};

[[nodiscard]] HostRuntimeContribution make_host_runtime_contribution(
    std::shared_ptr<const runtime::HostModuleRegistry> metadata,
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings,
    std::shared_ptr<runtime::HostReleaseDispatcher> handles = {},
    std::vector<std::shared_ptr<void>> lifetime_owners = {});

// Concatenates independent adapter contributions and rebuilds both immutable
// registries, so duplicate modules/bindings and every original limit are
// checked at the final production boundary. At most one distinct typed-handle
// dispatcher may belong to one evaluator context.
[[nodiscard]] ComposedHostRuntime compose_host_runtime(
    std::vector<HostRuntimeContribution> contributions,
    HostRuntimeCompositionLimits limits = {});

}  // namespace baas::script::host
