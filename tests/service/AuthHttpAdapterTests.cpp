#include "service/auth/AuthOwner.h"
#include "service/auth/CanonicalJson.h"
#include "service/http/AuthHttpAdapter.h"
#include "service/router/Router.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace auth = baas::service::auth;
namespace http = baas::service::http;
namespace router = baas::service::router;
namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view text)
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

class MemoryStorage final : public auth::AuthStorage {
public:
    auth::StorageReadResult read(
        const auth::AuthFile file, const std::size_t maximum) noexcept override
    {
        const auto found = files.find(file);
        if (found == files.end()) return {};
        if (found->second.size() > maximum) return {auth::StorageReadStatus::failure, {}};
        return {auth::StorageReadStatus::value, auth::SecretBuffer{found->second}};
    }

    bool write_atomic(
        const auth::AuthFile file, const std::span<const std::byte> value) noexcept override
    {
        files[file] = auth::PublicBytes{value.begin(), value.end()};
        return true;
    }

    std::unordered_map<auth::AuthFile, auth::PublicBytes> files;
};

class Clock final : public auth::AuthClock {
public:
    std::int64_t now_unix_seconds() noexcept override { return 1'700'000'000; }
};

class Random final : public auth::AuthRandom {
public:
    bool fill(const std::span<std::byte> output) noexcept override
    {
        for (auto& value : output) value = static_cast<std::byte>(next++);
        return true;
    }
    unsigned int next = 1;
};

class Deriver final : public auth::PasswordDeriver {
public:
    auth::SecretBytesResult derive(
        const std::span<const std::byte> password,
        const std::span<const std::byte> salt) noexcept override
    {
        auth::SecretBuffer input{password.size() + salt.size()};
        std::copy(password.begin(), password.end(), input.mutable_bytes().begin());
        std::copy(salt.begin(), salt.end(), input.mutable_bytes().begin() + password.size());
        auto digest = auth::sha256(input.bytes());
        if (!digest) return {std::nullopt, digest.error};
        return {auth::SecretBuffer{*digest.value}, auth::CryptoError::none};
    }
};

struct AuthFixture {
    std::shared_ptr<auth::AuthOwner> owner;
    std::string session_id;
    std::string proof_b64;
};

[[nodiscard]] AuthFixture authenticated_fixture()
{
    auto opened = auth::AuthOwner::open(
        {},
        {
            std::make_shared<MemoryStorage>(),
            std::make_shared<Clock>(),
            std::make_shared<Random>(),
            std::make_shared<Deriver>(),
            {},
            {},
        }
    );
    if (!opened) throw std::runtime_error("AuthOwner fixture open failed");
    std::shared_ptr<auth::AuthOwner> owner{std::move(*opened.value)};
    if (!owner->initialize_password(auth::SecretBuffer{bytes("pw")})) {
        throw std::runtime_error("AuthOwner fixture initialize failed");
    }
    const auto state = owner->password_state();
    Deriver deriver;
    auto password_hash = deriver.derive(bytes("pw"), state.pwd_salt);
    auth::PublicBytes shared(auth::auth_key_bytes, std::byte{0x31});
    auto transcript = auth::sha256(bytes("auth-http-test"));
    const auto info = std::string{"auth-proof:"} + std::to_string(state.pwd_epoch);
    auto context = auth::hkdf_sha256(
        shared, *transcript.value, bytes(info), auth::auth_key_bytes);
    auto password_proof = auth::hmac_sha256(
        password_hash.value->bytes(), context.value->bytes());
    auto session = owner->authenticate_control(
        {auth::SecretBuffer{shared}, *transcript.value}, *password_proof.value);
    if (!session) throw std::runtime_error("AuthOwner fixture authentication failed");

    auth::SecretBuffer combined{shared.size() + password_hash.value->size()};
    std::copy(shared.begin(), shared.end(), combined.mutable_bytes().begin());
    std::copy(password_hash.value->bytes().begin(), password_hash.value->bytes().end(),
              combined.mutable_bytes().begin() + shared.size());
    auto master = auth::hkdf_sha256(
        combined.bytes(), *transcript.value, bytes("master-secret"), auth::auth_key_bytes);
    auto resume = auth::hkdf_sha256(
        master.value->bytes(), *transcript.value, bytes("resume-secret"), auth::auth_key_bytes);
    auto payload = auth::encode_canonical_json_value(auth::CanonicalJsonValue{
        auth::CanonicalJsonValue::Object{
            {"type", auth::CanonicalJsonValue{std::string{"remember_session"}}},
            {"session_id", auth::CanonicalJsonValue{session->session_id}},
            {"pwd_epoch", auth::CanonicalJsonValue{static_cast<std::int64_t>(state.pwd_epoch)}},
        }});
    auto proof = auth::hmac_sha256(resume.value->bytes(), bytes(payload.text));
    auto encoded = auth::encode_base64url_padded(*proof.value);
    return {std::move(owner), session->session_id, std::move(*encoded.value)};
}

[[nodiscard]] std::string_view header(
    const router::Response& response, const std::string_view name)
{
    for (const auto& field : response.headers) if (field.name == name) return field.value;
    return {};
}

[[nodiscard]] std::string token_from_set_cookie(const std::string_view set_cookie)
{
    const auto equals = set_cookie.find('=');
    const auto semicolon = set_cookie.find(';', equals + 1);
    return std::string{set_cookie.substr(equals + 1, semicolon - equals - 1)};
}

void test_remember_and_logout_revoke_server_state()
{
    auto fixture = authenticated_fixture();
    auto extension = std::make_shared<http::AuthHttpAdapter>(fixture.owner);
    auto service = router::Router{{"BAAS", "test"}, {}, nullptr, extension};
    const std::string body = std::string{"{\"proof\":\""} + fixture.proof_b64
        + "\",\"session_id\":\"" + fixture.session_id + "\"}";
    auto remembered = service.handle({"POST", "/auth/remember", body, {}, false, true});
    check(remembered.status == 200 && remembered.body.find("\"ok\":true") != std::string::npos,
          "remember route must return v1 success JSON");
    const auto set_cookie = header(remembered, "Set-Cookie");
    check(set_cookie.find("baas_remember=v1.") == 0
              && set_cookie.find("HttpOnly") != std::string_view::npos
              && set_cookie.find("SameSite=Lax") != std::string_view::npos
              && set_cookie.find("Path=/") != std::string_view::npos
              && set_cookie.find("Secure") != std::string_view::npos,
          "remember cookie must carry the required secure attributes");
    check(fixture.owner->remembered_login_count() == 1,
          "remember route must persist one server-side token");

    const auto cookie = std::string{"other=x; baas_remember="} + token_from_set_cookie(set_cookie);
    auto logged_out = service.handle({"POST", "/auth/logout", {}, cookie, false, true});
    check(logged_out.status == 200 && logged_out.body == R"({"ok":true})",
          "logout route must return v1 success JSON");
    check(fixture.owner->remembered_login_count() == 0,
          "logout route must revoke the server-side token");
    const auto deleted = header(logged_out, "Set-Cookie");
    check(deleted.find("Max-Age=0") != std::string_view::npos
              && deleted.find("HttpOnly") != std::string_view::npos
              && deleted.find("Secure") != std::string_view::npos,
          "logout must expire the cookie with matching security attributes");
}

void test_stable_rejections_and_builtin_routes()
{
    auto fixture = authenticated_fixture();
    http::AuthHttpAdapterConfig config;
    config.max_remember_body_bytes = 64;
    config.max_cookie_header_bytes = 32;
    config.force_secure_cookie = true;
    auto extension = std::make_shared<http::AuthHttpAdapter>(fixture.owner, config);
    const auto service = router::Router::with_health_snapshot(
        {"BAAS", "test"}, {{}, {false, 0, "key"}}, {}, nullptr, extension);

    const auto duplicate = service.handle({
        "POST", "/auth/remember", R"({"session_id":"a","proof":"b","proof":"c"})"});
    check(duplicate.status == 400 && duplicate.body.find("duplicate_json_field") != std::string::npos,
          "duplicate remember fields must fail stably");
    const auto unknown = service.handle({"POST", "/auth/remember", R"({"session_id":"a","proof":"AA==","extra":1})"});
    check(unknown.status == 400 && unknown.body.find("invalid_remember_request") != std::string::npos,
          "unknown remember fields must be rejected");
    const auto oversized = service.handle({"POST", "/auth/remember", std::string(65, 'x')});
    check(oversized.status == 413 && oversized.body.find("auth_request_too_large") != std::string::npos,
          "auth route must enforce its stricter body budget");
    const auto method = service.handle({"GET", "/auth/logout", {}});
    check(method.status == 405 && header(method, "Allow") == "POST",
          "auth route method rejection must expose stable Allow");
    const auto duplicate_cookie = service.handle({
        "POST", "/auth/logout", {}, "baas_remember=a; baas_remember=b"});
    check(duplicate_cookie.status == 400
              && duplicate_cookie.body.find("duplicate_remember_cookie") != std::string::npos,
          "duplicate remember cookies must fail closed");
    const auto duplicate_header = service.handle({
        "POST", "/auth/logout", {}, {}, true});
    check(duplicate_header.status == 400
              && duplicate_header.body.find("duplicate_cookie_header") != std::string::npos,
          "duplicate Cookie fields must fail closed");
    const auto large_cookie = service.handle({
        "POST", "/auth/logout", {}, std::string(33, 'x')});
    check(large_cookie.status == 431 && large_cookie.body.find("cookie_header_too_large") != std::string::npos,
          "oversized Cookie header must fail before parsing");
    const auto health = service.handle({"GET", "/health", {}});
    const auto version = service.handle({"GET", "/version", {}});
    const auto missing = service.handle({"GET", "/auth/missing", {}});
    check(health.status == 200 && version.status == 200 && missing.status == 404,
          "auth extension must preserve builtin and unknown path behavior");
}

}  // namespace

int main()
{
    try {
        test_remember_and_logout_revoke_server_state();
        test_stable_rejections_and_builtin_routes();
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: unexpected exception: " << exception.what() << '\n';
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
