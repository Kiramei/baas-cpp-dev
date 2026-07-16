#pragma once

#include <string>

namespace baas::service::adapters {

// Immutable business defaults supplied by the generation-bound external
// resources repository. They are intentionally plain owned bytes: the file
// resource store never receives a repository path or update capability.
struct ConfigurationDefaults final {
    std::string user_json;
    std::string event_json;
    std::string switch_json;
    std::string static_json;
};

}  // namespace baas::service::adapters
