#include "service/http/AuthHttpAdapter.h"

#include "service/auth/CanonicalJson.h"
#include "service/auth/Crypto.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace baas::service::http {
namespace {

constexpr std::string_view remember_path = "/auth/remember";
constexpr std::string_view logout_path = "/auth/logout";
constexpr std::string_view cookie_name = "baas_remember";

[[nodiscard]] router::Response json_response(const int status, std::string body)
{
    return {status, {{"Content-Type", "application/json; charset=utf-8"}}, std::move(body)};
}

[[nodiscard]] router::Response error(
    const int status,
    const std::string_view code,
    const std::string_view message
)
{
    return json_response(
        status,
        std::string{"{\"error\":{\"code\":\""} + std::string{code}
            + "\",\"message\":\"" + std::string{message} + "\",\"status\":"
            + std::to_string(status) + "},\"ok\":false}"
    );
}

[[nodiscard]] router::Response method_not_allowed()
{
    auto response = error(405, "method_not_allowed", "request method is not allowed for this path");
    response.headers.push_back({"Allow", "POST"});
    return response;
}

[[nodiscard]] bool cookie_octet(const unsigned char value) noexcept
{
    return value == 0x21U || (value >= 0x23U && value <= 0x2BU)
        || (value >= 0x2DU && value <= 0x3AU)
        || (value >= 0x3CU && value <= 0x5BU)
        || (value >= 0x5DU && value <= 0x7EU);
}

[[nodiscard]] std::string_view trim_cookie_space(std::string_view value) noexcept
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

enum class CookieParse { absent, present, malformed, duplicate };

struct RememberCookie {
    CookieParse result = CookieParse::absent;
    std::string_view value;
};

[[nodiscard]] RememberCookie parse_remember_cookie(const std::string_view header) noexcept
{
    RememberCookie parsed;
    std::size_t offset = 0;
    while (offset <= header.size()) {
        const auto separator = header.find(';', offset);
        const auto end = separator == std::string_view::npos ? header.size() : separator;
        const auto pair = trim_cookie_space(header.substr(offset, end - offset));
        if (pair.empty()) return {CookieParse::malformed, {}};
        const auto equals = pair.find('=');
        if (equals == std::string_view::npos || equals == 0) {
            return {CookieParse::malformed, {}};
        }
        const auto name = trim_cookie_space(pair.substr(0, equals));
        const auto value = trim_cookie_space(pair.substr(equals + 1));
        if (!std::all_of(value.begin(), value.end(), [](const char byte) {
                return cookie_octet(static_cast<unsigned char>(byte));
            })) {
            return {CookieParse::malformed, {}};
        }
        if (name == cookie_name) {
            if (parsed.result == CookieParse::present) {
                return {CookieParse::duplicate, {}};
            }
            parsed = {CookieParse::present, value};
        }
        if (separator == std::string_view::npos) break;
        offset = separator + 1;
    }
    return parsed;
}

[[nodiscard]] std::string cookie_suffix(const bool secure)
{
    std::string result{"; Path=/; HttpOnly; SameSite=Lax"};
    if (secure) result += "; Secure";
    return result;
}

void add_delete_cookie(router::Response& response, const bool secure)
{
    response.headers.push_back({
        "Set-Cookie",
        std::string{cookie_name} +
            "=; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Max-Age=0" + cookie_suffix(secure),
    });
}

[[nodiscard]] bool authentication_failure(const auth::AuthError auth_error) noexcept
{
    using enum auth::AuthError;
    switch (auth_error) {
    case authentication_failed:
    case unknown_session:
    case session_expired:
    case stale_epoch:
    case invalid_remember_proof:
    case invalid_remember_token:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] router::Response auth_owner_error(const auth::AuthError auth_error)
{
    if (authentication_failure(auth_error)) {
        return error(401, "remember_auth_failed", "remember authentication failed");
    }
    if (auth_error == auth::AuthError::invalid_argument) {
        return error(400, "invalid_auth_request", "authentication request is invalid");
    }
    return error(503, "auth_unavailable", "authentication service is unavailable");
}

}  // namespace

AuthHttpAdapter::AuthHttpAdapter(
    std::shared_ptr<auth::AuthOwner> auth_owner,
    const AuthHttpAdapterConfig config
)
    : auth_owner_(std::move(auth_owner)), config_(config)
{
    if (!auth_owner_) throw std::invalid_argument("HTTP auth owner must not be null");
    if (config_.max_remember_body_bytes == 0 || config_.max_cookie_header_bytes == 0
        || config_.remember_cookie_max_age_seconds < 0) {
        throw std::invalid_argument("HTTP auth limits must be positive");
    }
}

const AuthHttpAdapterConfig& AuthHttpAdapter::config() const noexcept
{
    return config_;
}

std::optional<router::Response> AuthHttpAdapter::handle(
    const router::Request& request
) const
{
    if (request.path != remember_path && request.path != logout_path) return std::nullopt;
    if (request.method != "POST") return method_not_allowed();
    return request.path == remember_path ? remember(request) : logout(request);
}

router::Response AuthHttpAdapter::remember(const router::Request& request) const
{
    if (request.body.size() > config_.max_remember_body_bytes) {
        return error(413, "auth_request_too_large", "remember request body is too large");
    }
    auth::CanonicalJsonLimits limits;
    limits.max_input_bytes = config_.max_remember_body_bytes;
    limits.max_output_bytes = config_.max_remember_body_bytes;
    limits.max_depth = 4;
    limits.max_values = 8;
    auto parsed = auth::parse_canonical_json_value(request.body, limits);
    if (!parsed) {
        const auto code = parsed.error == auth::CanonicalJsonError::duplicate_key
            ? "duplicate_json_field" : "invalid_json_body";
        const auto message = parsed.error == auth::CanonicalJsonError::duplicate_key
            ? "request body contains a duplicate JSON field"
            : "request body must be a valid JSON object";
        return error(400, code, message);
    }
    const auto* object = parsed.value->as_object();
    const auto* session = parsed.value->find("session_id");
    const auto* proof_value = parsed.value->find("proof");
    const auto* session_text = session == nullptr ? nullptr : session->as_string();
    const auto* proof_text = proof_value == nullptr ? nullptr : proof_value->as_string();
    if (object == nullptr || object->size() != 2 || session_text == nullptr
        || proof_text == nullptr || session_text->empty()) {
        parsed.value->wipe_strings();
        return error(400, "invalid_remember_request", "session_id and proof strings are required");
    }
    std::string session_id{*session_text};
    auto proof = auth::decode_base64url_canonical(*proof_text, auth::hmac_sha256_bytes);
    parsed.value->wipe_strings();
    if (!proof) {
        return error(400, "invalid_remember_proof", "proof must be canonical base64url SHA-256 bytes");
    }
    auto issued = auth_owner_->issue_remember_token(session_id, *proof.value);
    auth::secure_zero(*proof.value);
    std::fill(session_id.begin(), session_id.end(), '\0');
    if (!issued) return auth_owner_error(issued.error);

    auto response = json_response(
        200,
        std::string{"{\"ok\":true,\"expires_at\":"}
            + std::to_string(issued->expires_at) + "}"
    );
    std::string cookie{cookie_name};
    cookie += '=';
    const auto token = issued->token.bytes();
    cookie.append(reinterpret_cast<const char*>(token.data()), token.size());
    cookie += "; Max-Age=" + std::to_string(config_.remember_cookie_max_age_seconds);
    cookie += cookie_suffix(request.secure_transport || config_.force_secure_cookie);
    issued->token.clear();
    response.headers.push_back({"Set-Cookie", std::move(cookie)});
    return response;
}

router::Response AuthHttpAdapter::logout(const router::Request& request) const
{
    const bool secure = request.secure_transport || config_.force_secure_cookie;
    if (!request.body.empty()) {
        auto response = error(400, "unexpected_logout_body", "logout request body must be empty");
        add_delete_cookie(response, secure);
        return response;
    }
    if (request.malformed_cookie_headers) {
        auto response = error(400, "duplicate_cookie_header", "request contains duplicate Cookie headers");
        add_delete_cookie(response, secure);
        return response;
    }
    if (request.cookie.has_value()
        && request.cookie->size() > config_.max_cookie_header_bytes) {
        auto response = error(431, "cookie_header_too_large", "Cookie header is too large");
        add_delete_cookie(response, secure);
        return response;
    }
    const auto cookie = request.cookie.has_value()
        ? parse_remember_cookie(*request.cookie) : RememberCookie{};
    if (cookie.result == CookieParse::malformed || cookie.result == CookieParse::duplicate) {
        auto response = error(
            400,
            cookie.result == CookieParse::duplicate ? "duplicate_remember_cookie" : "invalid_cookie_header",
            cookie.result == CookieParse::duplicate
                ? "request contains duplicate remember cookies" : "Cookie header is malformed"
        );
        add_delete_cookie(response, secure);
        return response;
    }
    if (cookie.result == CookieParse::present && !cookie.value.empty()) {
        auth::SecretBuffer token{
            std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(cookie.value.data()), cookie.value.size()}};
        const auto revoked = auth_owner_->logout_remember_token(std::move(token));
        if (!revoked) {
            auto response = auth_owner_error(revoked.error);
            add_delete_cookie(response, secure);
            return response;
        }
    }
    auto response = json_response(200, R"({"ok":true})");
    add_delete_cookie(response, secure);
    return response;
}

}  // namespace baas::service::http
