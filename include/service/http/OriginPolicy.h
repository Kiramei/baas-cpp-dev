#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::http {

inline constexpr std::size_t cors_max_origin_bytes = 512;
inline constexpr std::size_t cors_max_allowed_origins = 32;
inline constexpr std::size_t cors_max_allowlist_bytes = 8'192;
inline constexpr std::size_t cors_max_allowed_methods = 16;
inline constexpr std::size_t cors_max_allowed_headers = 32;
inline constexpr std::size_t cors_max_preflight_headers_bytes = 2'048;
inline constexpr std::size_t cors_max_preflight_header_count = 32;
inline constexpr std::size_t cors_max_method_bytes = 32;

// These are the only browser origins demonstrated by the current Tauri tree:
// desktop/Android dev URLs plus the Android embedded-backend fallback.
struct CorsPolicyConfig {
    std::vector<std::string> allowed_origins{
        "http://localhost:8191",
        "http://127.0.0.1:8191",
        "http://tauri.localhost",
    };
    std::vector<std::string> allowed_methods{"GET", "HEAD", "POST"};
    std::vector<std::string> allowed_headers{"accept", "content-type"};
    // Native loopback clients (including the Tauri health probe) do not send
    // Origin. This preserves that transport behavior; it is not authentication.
    bool allow_requests_without_origin = true;
};

struct CorsRequest {
    std::optional<std::string_view> origin;
    std::string_view method;
    std::optional<std::string_view> requested_method;
    std::optional<std::string_view> requested_headers;
    bool malformed_header_cardinality = false;
};

enum class CorsDecision {
    native_request,
    actual_request,
    preflight,
    reject,
};

struct CorsEvaluation {
    CorsDecision decision = CorsDecision::reject;
    int status = 403;
    std::string code;
    std::string message;
    std::string allow_origin;
    std::string allow_methods;
    std::string allow_headers;

    [[nodiscard]] bool allowed() const noexcept
    {
        return decision != CorsDecision::reject;
    }
};

class OriginPolicy final {
public:
    explicit OriginPolicy(CorsPolicyConfig config = {});

    [[nodiscard]] CorsEvaluation evaluate(const CorsRequest& request) const;
    [[nodiscard]] const std::vector<std::string>& allowed_origins() const noexcept;
    [[nodiscard]] const std::vector<std::string>& allowed_methods() const noexcept;
    [[nodiscard]] const std::vector<std::string>& allowed_headers() const noexcept;
    [[nodiscard]] bool allows_requests_without_origin() const noexcept;

private:
    std::vector<std::string> allowed_origins_;
    std::vector<std::string> allowed_methods_;
    std::vector<std::string> allowed_headers_;
    std::string allow_methods_header_;
    bool allow_requests_without_origin_ = true;
};

}  // namespace baas::service::http
