#include "service/http/OriginPolicy.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <unordered_set>

namespace baas::service::http {
namespace {

[[nodiscard]] bool is_ascii_visible(const std::string_view value)
{
    return std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return ch >= 0x21 && ch <= 0x7e;
    });
}

[[nodiscard]] bool is_token_char(const unsigned char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')
        || (ch >= 'a' && ch <= 'z')
        || std::string_view{"!#$%&'*+-.^_`|~"}.find(ch)
        != std::string_view::npos;
}

[[nodiscard]] bool is_token(const std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), is_token_char);
}

[[nodiscard]] std::string ascii_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
    });
    return value;
}

[[nodiscard]] std::string ascii_upper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch);
    });
    return value;
}

[[nodiscard]] bool valid_ipv4(const std::string_view host)
{
    std::size_t start = 0;
    int components = 0;
    while (start <= host.size()) {
        const auto end = host.find('.', start);
        const auto part = host.substr(start, end == std::string_view::npos ? host.size() - start
                                                                          : end - start);
        if (part.empty() || part.size() > 3 || (part.size() > 1 && part.front() == '0')
            || !std::all_of(part.begin(), part.end(), [](const unsigned char ch) {
                   return ch >= '0' && ch <= '9';
               })) {
            return false;
        }
        unsigned value = 0;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || value > 255) {
            return false;
        }
        ++components;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return components == 4;
}

[[nodiscard]] bool valid_dns_host(const std::string_view host)
{
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') {
        return false;
    }
    bool saw_dot = false;
    std::size_t start = 0;
    while (start < host.size()) {
        const auto end = host.find('.', start);
        const auto label = host.substr(start, end == std::string_view::npos ? host.size() - start
                                                                           : end - start);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-'
            || !std::all_of(label.begin(), label.end(), [](const unsigned char ch) {
                   return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')
                       || (ch >= 'a' && ch <= 'z') || ch == '-';
               })) {
            return false;
        }
        if (end == std::string_view::npos) break;
        saw_dot = true;
        start = end + 1;
    }
    // A numeric-looking dotted host must be a valid IPv4 address. This avoids
    // platform-dependent legacy IPv4 spellings and resolver interpretation.
    if (saw_dot && std::all_of(host.begin(), host.end(), [](const unsigned char ch) {
            return (ch >= '0' && ch <= '9') || ch == '.';
        })) {
        return valid_ipv4(host);
    }
    return true;
}

[[nodiscard]] std::string canonical_origin(const std::string_view input)
{
    if (input.empty() || input.size() > cors_max_origin_bytes || !is_ascii_visible(input)
        || input == "null") {
        throw std::invalid_argument("invalid serialized Origin");
    }
    const auto separator = input.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        throw std::invalid_argument("Origin must contain scheme and authority");
    }
    auto scheme = ascii_lower(std::string{input.substr(0, separator)});
    if (scheme != "http" && scheme != "https" && scheme != "tauri") {
        throw std::invalid_argument("Origin scheme is not permitted");
    }
    auto authority = input.substr(separator + 3);
    if (authority.empty() || authority.find_first_of("/@?#\\%,") != std::string_view::npos) {
        throw std::invalid_argument("Origin authority is invalid");
    }

    std::string host;
    std::optional<unsigned> port;
    bool explicit_port = false;
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos || authority.substr(0, close + 1) != "[::1]") {
            throw std::invalid_argument("only canonical IPv6 loopback is permitted");
        }
        host = "[::1]";
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') throw std::invalid_argument("invalid Origin authority");
            explicit_port = true;
            authority.remove_prefix(close + 2);
        } else {
            authority = {};
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            if (authority.find(':') != colon) throw std::invalid_argument("invalid unbracketed host");
            host = ascii_lower(std::string{authority.substr(0, colon)});
            explicit_port = true;
            authority.remove_prefix(colon + 1);
        } else {
            host = ascii_lower(std::string{authority});
            authority = {};
        }
        if (!valid_dns_host(host)) throw std::invalid_argument("invalid Origin host");
    }

    if (explicit_port && authority.empty()) throw std::invalid_argument("Origin port is missing");
    if (!authority.empty()) {
        if (authority.size() > 5 || (authority.size() > 1 && authority.front() == '0')
            || !std::all_of(authority.begin(), authority.end(), [](const unsigned char ch) {
                   return ch >= '0' && ch <= '9';
               })) {
            throw std::invalid_argument("invalid Origin port");
        }
        unsigned value = 0;
        const auto parsed = std::from_chars(
            authority.data(), authority.data() + authority.size(), value
        );
        if (parsed.ec != std::errc{} || value == 0 || value > 65'535) {
            throw std::invalid_argument("invalid Origin port");
        }
        port = value;
    }
    if (scheme == "tauri" && (host != "localhost" || port.has_value())) {
        throw std::invalid_argument("tauri Origin must be tauri://localhost");
    }
    if ((scheme == "http" && port == 80U) || (scheme == "https" && port == 443U)) {
        port.reset();
    }
    auto result = scheme + "://" + host;
    if (port) result += ':' + std::to_string(*port);
    return result;
}

template <typename Normalize>
[[nodiscard]] std::vector<std::string> normalized_allowlist(
    const std::vector<std::string>& input,
    const std::size_t maximum,
    Normalize normalize,
    const char* description
)
{
    if (input.empty() || input.size() > maximum) {
        throw std::invalid_argument(std::string{description} + " allowlist has invalid size");
    }
    std::vector<std::string> output;
    output.reserve(input.size());
    std::size_t total = 0;
    for (const auto& value : input) {
        auto item = normalize(value);
        total += item.size();
        if (total > cors_max_allowlist_bytes) {
            throw std::invalid_argument("CORS allowlist byte budget exceeded");
        }
        output.push_back(std::move(item));
    }
    std::sort(output.begin(), output.end());
    output.erase(std::unique(output.begin(), output.end()), output.end());
    return output;
}

[[nodiscard]] std::string join(const std::vector<std::string>& values, const std::string_view separator)
{
    std::string result;
    for (const auto& value : values) {
        if (!result.empty()) result += separator;
        result += value;
    }
    return result;
}

[[nodiscard]] CorsEvaluation reject(std::string code, std::string message)
{
    return {CorsDecision::reject, 403, std::move(code), std::move(message), {}, {}, {}};
}

[[nodiscard]] std::optional<std::vector<std::string>> parse_requested_headers(
    const std::string_view input
)
{
    if (input.empty() || input.size() > cors_max_preflight_headers_bytes
        || !std::all_of(input.begin(), input.end(), [](const unsigned char ch) {
               return is_token_char(ch) || ch == ',' || ch == ' ' || ch == '\t';
           })) {
        return std::nullopt;
    }
    std::vector<std::string> output;
    std::size_t start = 0;
    while (start <= input.size()) {
        const auto comma = input.find(',', start);
        auto part = input.substr(start, comma == std::string_view::npos ? input.size() - start
                                                                       : comma - start);
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) part.remove_prefix(1);
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) part.remove_suffix(1);
        if (!is_token(part) || output.size() == cors_max_preflight_header_count) return std::nullopt;
        output.push_back(ascii_lower(std::string{part}));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    std::sort(output.begin(), output.end());
    output.erase(std::unique(output.begin(), output.end()), output.end());
    return output;
}

}  // namespace

OriginPolicy::OriginPolicy(CorsPolicyConfig config)
    : allowed_origins_(normalized_allowlist(
          config.allowed_origins,
          cors_max_allowed_origins,
          [](const std::string& value) { return canonical_origin(value); },
          "Origin"
      )),
      allowed_methods_(normalized_allowlist(
          config.allowed_methods,
          cors_max_allowed_methods,
          [](const std::string& value) {
              if (!is_token(value)) throw std::invalid_argument("invalid CORS method token");
              return ascii_upper(value);
          },
          "method"
      )),
      allowed_headers_(normalized_allowlist(
          config.allowed_headers,
          cors_max_allowed_headers,
          [](const std::string& value) {
              if (!is_token(value)) throw std::invalid_argument("invalid CORS header token");
              return ascii_lower(value);
          },
          "header"
      )),
      allow_methods_header_(join(allowed_methods_, ", ")),
      allow_requests_without_origin_(config.allow_requests_without_origin)
{}

CorsEvaluation OriginPolicy::evaluate(const CorsRequest& request) const
{
    if (request.malformed_header_cardinality) {
        return reject("cors_invalid_request", "CORS request headers are malformed");
    }
    if (!request.origin) {
        if (allow_requests_without_origin_) return {CorsDecision::native_request, 0, {}, {}, {}, {}, {}};
        return reject("origin_required", "request Origin is required");
    }
    std::string origin;
    try {
        origin = canonical_origin(*request.origin);
    } catch (const std::invalid_argument&) {
        return reject("invalid_origin", "request Origin is not a valid serialized origin");
    }
    if (!std::binary_search(allowed_origins_.begin(), allowed_origins_.end(), origin)) {
        return reject("origin_not_allowed", "request Origin is not allowed");
    }

    const bool preflight = request.method == "OPTIONS" && request.requested_method.has_value();
    const auto candidate_method_view = preflight ? *request.requested_method : request.method;
    if (candidate_method_view.size() > cors_max_method_bytes || !is_token(candidate_method_view)) {
        return reject("cors_invalid_method", "CORS request method is malformed");
    }
    auto candidate_method = ascii_upper(std::string{candidate_method_view});
    if (!std::binary_search(allowed_methods_.begin(), allowed_methods_.end(), candidate_method)) {
        return reject("cors_method_not_allowed", "CORS request method is not allowed");
    }
    if (!preflight) {
        return {CorsDecision::actual_request, 0, {}, {}, std::move(origin), {}, {}};
    }

    std::string requested_header_value;
    if (request.requested_headers) {
        const auto parsed = parse_requested_headers(*request.requested_headers);
        if (!parsed) return reject("cors_invalid_headers", "CORS requested headers are malformed");
        for (const auto& header : *parsed) {
            if (!std::binary_search(allowed_headers_.begin(), allowed_headers_.end(), header)) {
                return reject("cors_headers_not_allowed", "CORS requested headers are not allowed");
            }
        }
        requested_header_value = join(*parsed, ", ");
    }
    return {
        CorsDecision::preflight,
        0,
        {},
        {},
        std::move(origin),
        allow_methods_header_,
        std::move(requested_header_value),
    };
}

const std::vector<std::string>& OriginPolicy::allowed_origins() const noexcept { return allowed_origins_; }
const std::vector<std::string>& OriginPolicy::allowed_methods() const noexcept { return allowed_methods_; }
const std::vector<std::string>& OriginPolicy::allowed_headers() const noexcept { return allowed_headers_; }
bool OriginPolicy::allows_requests_without_origin() const noexcept { return allow_requests_without_origin_; }

}  // namespace baas::service::http
