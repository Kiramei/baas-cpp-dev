#pragma once

#include "service/adapters/ConfigurationDefaults.h"
#include "service/channels/SyncHandler.h"

#include <memory>

namespace baas::runtime::repository {
class RuntimeRepositoryReadView;
}

namespace baas::service::app {

// Loads the fixed Python-compatible configuration-default contract from the
// already admitted resources view. Missing, oversized, or structurally
// invalid documents fail closed by throwing.
[[nodiscard]] std::shared_ptr<const adapters::ConfigurationDefaults>
load_runtime_configuration_defaults(
    const ::baas::runtime::repository::RuntimeRepositoryReadView& resources,
    const channels::ResourceStoreLimits& consumer_limits);

}  // namespace baas::service::app
