#pragma once

#include "service/auth/AuthOwner.h"
#include "service/router/Router.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace baas::service::http {

struct AuthHttpAdapterConfig {
    std::size_t max_remember_body_bytes = 4'096;
    std::size_t max_cookie_header_bytes = 4'096;
    std::int64_t remember_cookie_max_age_seconds = auth::default_remember_ttl_seconds;
    bool force_secure_cookie = false;
};

// Owns only the HTTP projection of AuthOwner. AuthOwner supplies all session,
// proof, token, persistence, and concurrency semantics.
class AuthHttpAdapter final : public router::RouteExtension {
public:
    explicit AuthHttpAdapter(
        std::shared_ptr<auth::AuthOwner> auth_owner,
        AuthHttpAdapterConfig config = {}
    );

    [[nodiscard]] std::optional<router::Response> handle(
        const router::Request& request) const override;
    [[nodiscard]] const AuthHttpAdapterConfig& config() const noexcept;

private:
    [[nodiscard]] router::Response remember(const router::Request& request) const;
    [[nodiscard]] router::Response logout(const router::Request& request) const;

    std::shared_ptr<auth::AuthOwner> auth_owner_;
    AuthHttpAdapterConfig config_;
};

}  // namespace baas::service::http
