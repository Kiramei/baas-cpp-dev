#pragma once

#include <string_view>

namespace baas::runtime::script {

// Native composition capability supplied only after the service's signed
// repository-update verifier and owner publish the generation. Browser/script
// input cannot construct or serialize this interface. A permissive native
// implementation is outside the trusted boundary and must never be wired by a
// production composition root. A true result attests that the signed update
// plan covered this exact generation and scripts commit; the read view then
// verifies every tree entry digest.
class RuntimeScriptRepositoryTrustEvidence {
public:
    virtual ~RuntimeScriptRepositoryTrustEvidence() = default;
    [[nodiscard]] virtual bool covers(
        std::string_view generation,
        std::string_view scripts_commit) const noexcept = 0;
};

}  // namespace baas::runtime::script
