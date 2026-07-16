#include "runtime/repository/RuntimeRepositoryGit2.h"

#include <git2.h>
#include <git2/sys/transport.h>
#include <httplib.h>
#include <miniz.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace baas::runtime::repository {
namespace {

constexpr std::string_view transport_directory_name = ".baas-git-transport";
constexpr std::string_view candidate_reference = "refs/baas-runtime/candidate";
constexpr std::size_t maximum_upload_pack_request_bytes = 1U * 1024U * 1024U;

template <typename Type, void (*Free)(Type*)>
using GitOwner = std::unique_ptr<Type, decltype(Free)>;

[[nodiscard]] std::string git_error_detail(const std::string_view operation) {
    std::string result(operation);
    if (const auto* error = git_error_last(); error != nullptr && error->message != nullptr) {
        result += ": ";
        result += error->message;
    }
    return result;
}

void require_git(const int result, const std::string_view operation) {
    if (result < 0)
        throw std::runtime_error(git_error_detail(operation));
}

[[nodiscard]] bool lowercase_sha1(const std::string_view value) noexcept {
    return value.size() == 40 && std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] bool valid_advertised_reference(const std::string& value) {
    int valid{};
    return (value.starts_with("refs/heads/") || value.starts_with("refs/tags/")) &&
           git_reference_name_is_valid(&valid, value.c_str()) == 0 && valid == 1;
}

[[nodiscard]] bool valid_percent_encoding(const std::string_view value) noexcept {
    constexpr auto hex = [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
    };
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%')
            continue;
        if (index + 2 >= value.size() || !hex(value[index + 1]) || !hex(value[index + 2]))
            return false;
        index += 2;
    }
    return true;
}

[[nodiscard]] bool valid_https_url(const std::string_view value) noexcept {
    if (!value.starts_with("https://") || value.size() <= 8 ||
        value.find('?') != std::string_view::npos || value.find('#') != std::string_view::npos ||
        value.find('\\') != std::string_view::npos || !valid_percent_encoding(value))
        return false;
    const auto authority_end = value.find('/', 8);
    const auto authority = value.substr(8, authority_end == std::string_view::npos
                                               ? value.size() - 8
                                               : authority_end - 8);
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find('%') != std::string_view::npos)
        return false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte <= 0x20U || byte == 0x7fU)
            return false;
    }
    return true;
}

#ifdef BAAS_RUNTIME_REPOSITORY_GIT2_TESTING
thread_local bool file_transport_enabled_for_testing{};

[[nodiscard]] bool valid_test_file_url(const std::string_view value) noexcept {
    return value.starts_with("file://") && value.size() > 7 &&
           value.find('?') == std::string_view::npos &&
           value.find('#') == std::string_view::npos &&
           value.find('\\') == std::string_view::npos;
}
#endif

[[nodiscard]] int bounded_timeout(const std::chrono::milliseconds timeout,
                                  const char* name) {
    if (timeout.count() <= 0 || timeout.count() > INT_MAX)
        throw std::invalid_argument(std::string(name) + " must fit a positive libgit2 timeout");
    return static_cast<int>(timeout.count());
}

void validate_limits(const Libgit2RuntimeRepositoryFetchLimits& limits) {
    static_cast<void>(bounded_timeout(limits.connect_timeout, "connect_timeout"));
    static_cast<void>(bounded_timeout(limits.stall_timeout, "stall_timeout"));
    constexpr auto maximum_absolute_timeout = std::chrono::hours(24);
    if (limits.absolute_timeout.count() <= 0 ||
        limits.absolute_timeout > maximum_absolute_timeout || limits.max_advertised_refs == 0 ||
        limits.max_advertisement_bytes == 0 || limits.max_pack_bytes == 0 ||
        limits.max_upload_pack_rounds == 0 ||
        limits.max_received_objects == 0 || limits.max_odb_objects == 0 ||
        limits.max_odb_bytes == 0 || limits.max_odb_object_bytes == 0 ||
        limits.max_commit_bytes == 0 || limits.max_tag_bytes == 0 ||
        limits.max_tree_bytes == 0 || limits.max_fallback_blob_bytes == 0 ||
        limits.max_delta_instruction_bytes == 0 || limits.max_delta_depth == 0 ||
        limits.max_peel_depth == 0)
        throw std::invalid_argument("libgit2 runtime repository limits must be positive");
    if (limits.connect_timeout > limits.absolute_timeout ||
        limits.stall_timeout > limits.absolute_timeout)
        throw std::invalid_argument("I/O timeouts must not exceed the absolute timeout");
    if (limits.max_pack_bytes > 256ULL * 1024ULL * 1024ULL ||
        limits.max_pack_bytes > std::numeric_limits<mz_uint>::max())
        throw std::invalid_argument("max_pack_bytes exceeds the pack preflight implementation");
    if (limits.max_advertisement_bytes > 64U * 1024U * 1024U ||
        limits.max_advertised_refs > 1'000'000U)
        throw std::invalid_argument("advertisement limits exceed implementation maxima");
    if (limits.max_received_objects > 1'000'000U || limits.max_odb_objects > 1'000'000U ||
        limits.max_odb_object_bytes > 512ULL * 1024ULL * 1024ULL ||
        limits.max_odb_bytes > 16ULL * 1024ULL * 1024ULL * 1024ULL ||
        limits.max_fallback_blob_bytes > 64U * 1024U * 1024U ||
        limits.max_delta_instruction_bytes > 64U * 1024U * 1024U ||
        limits.max_delta_depth > 1'024U ||
        limits.max_upload_pack_rounds > 16U ||
        limits.tree.max_files > 1'000'000U || limits.tree.max_entries > 2'000'000U ||
        limits.tree.max_total_bytes > 16ULL * 1024ULL * 1024ULL * 1024ULL ||
        limits.tree.max_file_bytes > 512ULL * 1024ULL * 1024ULL ||
        limits.tree.max_manifest_bytes > 64U * 1024U * 1024U ||
        limits.tree.max_relative_path_bytes > 4'096U ||
        limits.tree.max_relative_path_depth > 128U)
        throw std::invalid_argument("repository limits exceed implementation hard maxima");
}

void validate_trusted_ca_bundle(const std::filesystem::path& bundle) {
#ifdef __ANDROID__
    if (bundle.empty())
        throw std::invalid_argument(
            "Android HTTPS fetches require an external trusted CA bundle");
#endif
    if (bundle.empty())
        return;
    std::error_code error;
    if (!bundle.is_absolute() || !std::filesystem::is_regular_file(bundle, error) || error)
        throw std::invalid_argument("trusted CA bundle must be an absolute regular file");
}

[[nodiscard]] std::string git_path_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

class GitLifetime final {
  public:
    GitLifetime() { require_git(git_libgit2_init(), "libgit2 initialization failed"); }
    ~GitLifetime() { git_libgit2_shutdown(); }
    GitLifetime(const GitLifetime&) = delete;
    GitLifetime& operator=(const GitLifetime&) = delete;
};

enum class AbortReason {
    None,
    Cancelled,
    Deadline,
    AdvertisementBytes,
    AdvertisementRefs,
    PackBytes,
    PackPreflight,
    PackRefDelta,
    UploadPackRounds,
    Http,
    Allocation,
    ReceivedObjects,
    OdbLimit
};

struct CallbackContext final {
    std::stop_token stop_token;
    std::chrono::steady_clock::time_point deadline;
    const Libgit2RuntimeRepositoryFetchLimits* limits{};
    const std::filesystem::path* trusted_ca_bundle{};
    AbortReason reason{AbortReason::None};
    std::size_t advertisement_bytes{};
    std::size_t upload_pack_bytes{};
    std::size_t upload_pack_rounds{};
    const char* pack_failure{};
    std::array<unsigned char, 256> pack_response_preview{};
    std::size_t pack_response_preview_size{};

    [[nodiscard]] bool interrupted() noexcept {
        if (reason != AbortReason::None)
            return true;
        if (stop_token.stop_requested())
            reason = AbortReason::Cancelled;
        else if (std::chrono::steady_clock::now() >= deadline)
            reason = AbortReason::Deadline;
        return reason != AbortReason::None;
    }
};

struct BoundedHttpSubtransport;

struct BoundedHttpStream final {
    git_smart_subtransport_stream parent{};
    CallbackContext* context{};
    git_smart_service_t action{};
    std::string host;
    int port{443};
    std::string repository_path;
    std::vector<char> upload_pack_request;
    std::vector<char> advertisement;
    std::size_t advertisement_cursor{};
    bool advertisement_buffered{};
    std::vector<char> upload_pack_response;
    std::size_t upload_pack_cursor{};
    bool upload_pack_buffered{};
};

struct BoundedHttpSubtransport final {
    git_smart_subtransport parent{};
    CallbackContext* context{};
};

struct HttpsEndpoint final {
    std::string host;
    int port{443};
    std::string path;
};

[[nodiscard]] int parse_https_port(const std::string_view value) {
    int port{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (value.empty() || error != std::errc{} || end != value.data() + value.size() ||
        port <= 0 || port > 65'535)
        throw std::invalid_argument("HTTPS endpoint port is invalid");
    return port;
}

[[nodiscard]] HttpsEndpoint parse_https_endpoint(const std::string_view url) {
    constexpr std::string_view prefix = "https://";
    if (!url.starts_with(prefix))
        throw std::invalid_argument("bounded transport requires HTTPS");
    const auto path_begin = url.find('/', prefix.size());
    const auto authority = url.substr(prefix.size(), path_begin == std::string_view::npos
                                                         ? url.size() - prefix.size()
                                                         : path_begin - prefix.size());
    HttpsEndpoint endpoint;
    endpoint.path = path_begin == std::string_view::npos ? "/" : std::string(url.substr(path_begin));
    if (authority.starts_with('[')) {
        const auto close = authority.find(']');
        if (close == std::string_view::npos)
            throw std::invalid_argument("HTTPS endpoint IPv6 authority is invalid");
        endpoint.host = std::string(authority.substr(1, close - 1));
        if (close + 1 != authority.size()) {
            if (authority[close + 1] != ':')
                throw std::invalid_argument("HTTPS endpoint authority is invalid");
            endpoint.port = parse_https_port(authority.substr(close + 2));
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos) {
            endpoint.host = std::string(authority.substr(0, colon));
            if (endpoint.host.find(':') != std::string::npos)
                throw std::invalid_argument("HTTPS endpoint IPv6 authority must be bracketed");
            endpoint.port = parse_https_port(authority.substr(colon + 1));
        } else {
            endpoint.host = std::string(authority);
        }
    }
    if (endpoint.host.empty() || endpoint.port <= 0 || endpoint.port > 65'535)
        throw std::invalid_argument("HTTPS endpoint authority is invalid");
    while (endpoint.path.size() > 1 && endpoint.path.ends_with('/'))
        endpoint.path.pop_back();
    return endpoint;
}

[[nodiscard]] std::string repository_endpoint_path(const std::string& repository_path,
                                                   const std::string_view suffix) {
    return repository_path == "/" ? std::string(suffix)
                                  : repository_path + std::string(suffix);
}

[[nodiscard]] int pkt_hex(const char value) noexcept {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool advertisement_ref_count_is_bounded(const std::vector<char>& bytes,
                                         const std::size_t maximum_refs) noexcept {
    enum class State { Start, ServiceFlush, Records, Finished };
    State state{State::Start};
    std::size_t offset{};
    std::size_t references{};
    while (offset != bytes.size()) {
        if (bytes.size() - offset < 4)
            return false;
        std::size_t packet_size{};
        for (std::size_t index = 0; index < 4; ++index) {
            const auto digit = pkt_hex(bytes[offset + index]);
            if (digit < 0)
                return false;
            packet_size = packet_size * 16U + static_cast<std::size_t>(digit);
        }
        if (packet_size == 0) {
            if (state == State::ServiceFlush)
                state = State::Records;
            else if (state == State::Records)
                state = State::Finished;
            else
                return false;
            offset += 4;
            continue;
        }
        if (packet_size == 1 || packet_size == 2)
            return false;
        if (packet_size < 4 || packet_size > bytes.size() - offset)
            return false;
        const std::string_view payload(bytes.data() + offset + 4, packet_size - 4);
        if (state == State::Start) {
            if (payload == "# service=git-upload-pack\n")
                state = State::ServiceFlush;
            else if (payload == "version 2\n")
                state = State::Records;
            else
                return false;
        } else {
            if (state != State::Records || payload.starts_with("# service=") ||
                payload == "version 2\n" || ++references > maximum_refs)
                return false;
        }
        offset += packet_size;
    }
    return state == State::Finished;
}

[[nodiscard]] std::unique_ptr<httplib::SSLClient> make_https_client(BoundedHttpStream& stream) {
    auto& context = *stream.context;
    if (context.interrupted())
        return {};
    auto client = std::make_unique<httplib::SSLClient>(stream.host, stream.port);
    client->enable_server_certificate_verification(true);
    if (context.trusted_ca_bundle != nullptr && !context.trusted_ca_bundle->empty()) {
        client->enable_system_ca(false);
        client->set_ca_cert_path(git_path_utf8(*context.trusted_ca_bundle));
    }
    client->set_follow_location(false);
    client->set_keep_alive(false);
    client->set_decompress(false);
    client->set_path_encode(false);
    client->set_connection_timeout(context.limits->connect_timeout);
    client->set_read_timeout(context.limits->stall_timeout);
    client->set_write_timeout(context.limits->stall_timeout);
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        context.deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
        static_cast<void>(context.interrupted());
        return {};
    }
    client->set_max_timeout(remaining);
    return client;
}

int perform_http_request(BoundedHttpStream& stream, const std::string_view method,
                         const std::string& path, const std::string_view expected_content_type,
                         const std::size_t maximum_bytes, std::vector<char>& destination) {
    auto& context = *stream.context;
    auto client = make_https_client(stream);
    if (!client)
        return GIT_EUSER;
    httplib::Request request;
    request.method = method;
    request.path = path;
    request.set_header("Accept", std::string(expected_content_type));
    if (method == "POST") {
        request.set_header("Content-Type", "application/x-git-upload-pack-request");
        request.body.assign(stream.upload_pack_request.begin(), stream.upload_pack_request.end());
    }
    bool accepted_headers{};
    request.response_handler = [&](const httplib::Response& response) {
        accepted_headers = response.status == 200 &&
                           response.get_header_value("Content-Type") == expected_content_type;
        if (accepted_headers && response.has_header("Content-Length"))
            accepted_headers = response.get_header_value_u64("Content-Length", maximum_bytes + 1U) <=
                               maximum_bytes;
        return accepted_headers;
    };
    request.content_receiver = [&](const char* data, const std::size_t length, std::size_t,
                                   std::size_t) {
        if (context.interrupted())
            return false;
        if (length > maximum_bytes - std::min(maximum_bytes, destination.size())) {
            context.reason = stream.action == GIT_SERVICE_UPLOADPACK_LS
                                 ? AbortReason::AdvertisementBytes
                                 : AbortReason::PackBytes;
            return false;
        }
        destination.insert(destination.end(), data, data + length);
        return !context.interrupted();
    };
    const auto result = client->send(request);
    if (!result || !accepted_headers) {
        if (!context.interrupted() && context.reason == AbortReason::None)
            context.reason = AbortReason::Http;
        return GIT_EUSER;
    }
    return context.interrupted() ? GIT_EUSER : 0;
}

int buffer_advertisement(BoundedHttpStream& stream) {
    auto& context = *stream.context;
    const auto separator = stream.repository_path.find('?') == std::string::npos ? '?' : '&';
    const auto path = repository_endpoint_path(stream.repository_path, "/info/refs") + separator +
                      "service=git-upload-pack";
    if (const auto result = perform_http_request(
            stream, "GET", path, "application/x-git-upload-pack-advertisement",
            context.limits->max_advertisement_bytes, stream.advertisement);
        result < 0)
        return result;
    if (!advertisement_ref_count_is_bounded(stream.advertisement,
                                              context.limits->max_advertised_refs)) {
        context.reason = AbortReason::AdvertisementRefs;
        return GIT_EUSER;
    }
    context.advertisement_bytes = stream.advertisement.size();
    stream.advertisement_buffered = true;
    return 0;
}

[[nodiscard]] std::uint32_t big_endian_u32(const char* bytes) noexcept {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) << 24U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 16U) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 8U) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
}

[[nodiscard]] bool append_pack_from_smart_response(const std::vector<char>& response,
                                                   std::vector<unsigned char>& pack,
                                                   const char** failure = nullptr,
                                                   bool* negotiation_only = nullptr) {
    const auto reject = [&](const char* reason) noexcept {
        if (failure != nullptr)
            *failure = reason;
        return false;
    };
    std::size_t offset{};
    bool saw_channel_one{};
    bool saw_progress{};
    bool saw_nak{};
    bool saw_packfile_marker{};
    bool saw_negotiation_record{};
    bool saw_ack_or_nak{};
    bool shallow_section_ended{};
    bool terminated{};
    while (offset < response.size()) {
        if (response.size() - offset >= 4 &&
            std::memcmp(response.data() + offset, "PACK", 4) == 0) {
            pack.insert(pack.end(), response.begin() + static_cast<std::ptrdiff_t>(offset),
                        response.end());
            break;
        }
        if (response.size() - offset < 4)
            return reject("truncated smart-response pkt-line");
        std::size_t packet_size{};
        for (std::size_t index = 0; index < 4; ++index) {
            const auto digit = pkt_hex(response[offset + index]);
            if (digit < 0)
                return reject("invalid smart-response pkt-line length");
            packet_size = packet_size * 16U + static_cast<std::size_t>(digit);
        }
        if (packet_size <= 2) {
            if (packet_size == 0 || packet_size == 2) {
                if (saw_ack_or_nak)
                    terminated = true;
                else if (saw_negotiation_record)
                    shallow_section_ended = true;
            }
            offset += 4;
            continue;
        }
        if (packet_size < 4 || packet_size > response.size() - offset)
            return reject("out-of-bounds smart-response pkt-line");
        const auto* payload = reinterpret_cast<const unsigned char*>(response.data() + offset + 4);
        const auto payload_size = packet_size - 4;
        const std::string_view payload_text(reinterpret_cast<const char*>(payload), payload_size);
        if (payload_size != 0 && payload[0] == 1) {
            saw_channel_one = true;
            pack.insert(pack.end(), payload + 1, payload + payload_size);
        } else if (payload_size != 0 && payload[0] == 2) {
            saw_progress = true;
        } else if (payload_size != 0 && payload[0] == 3) {
            return reject("remote side-band error");
        } else if (payload_text == "NAK\n") {
            if (terminated)
                return reject("negotiation record followed response termination");
            saw_nak = true;
            saw_negotiation_record = true;
            saw_ack_or_nak = true;
        } else if (payload_text == "packfile\n") {
            saw_packfile_marker = true;
        } else if ((payload_text.starts_with("shallow ") ||
                    payload_text.starts_with("unshallow ")) &&
                   lowercase_sha1(payload_text.substr(payload_text.find(' ') + 1))) {
            if (terminated || shallow_section_ended || saw_ack_or_nak)
                return reject("shallow record followed section termination");
            saw_negotiation_record = true;
        } else if (payload_text.starts_with("ACK ")) {
            if (terminated)
                return reject("negotiation record followed response termination");
            const auto oid = payload_text.substr(4, 40);
            const auto suffix = payload_text.substr(std::min<std::size_t>(44, payload_text.size()));
            if (!lowercase_sha1(oid) ||
                (suffix != "\n" && suffix != " continue\n" && suffix != " common\n" &&
                 suffix != " ready\n"))
                return reject("invalid ACK negotiation record");
            saw_negotiation_record = true;
            saw_ack_or_nak = true;
        } else {
            return reject("unrecognized upload-pack negotiation record");
        }
        offset += packet_size;
    }
    if (pack.size() >= 12 && std::memcmp(pack.data(), "PACK", 4) == 0)
        return true;
    const auto negotiation_terminated =
        terminated || (shallow_section_ended && !saw_ack_or_nak);
    if (saw_negotiation_record && negotiation_terminated && !saw_channel_one &&
        !saw_progress && !saw_packfile_marker) {
        if (negotiation_only != nullptr)
            *negotiation_only = true;
        return true;
    }
    if (saw_channel_one)
        return reject("smart response channel-one pack was invalid");
    if (saw_packfile_marker)
        return reject("smart response had packfile marker but no channel-one data");
    if (saw_progress)
        return reject("smart response had progress but no channel-one data");
    if (saw_nak)
        return reject("smart response had NAK but no pack data");
    return reject("smart response did not contain recognized upload-pack data");
}

struct InflatedObject final {
    std::size_t consumed{};
    std::size_t produced{};
    std::vector<unsigned char> data;
};

[[nodiscard]] bool inflate_pack_object(const unsigned char* input, const std::size_t input_size,
                                       const std::size_t maximum_output,
                                       const bool retain_output, CallbackContext* context,
                                       InflatedObject& result) {
    if (retain_output)
        result.data.reserve(maximum_output);
    mz_stream stream{};
    stream.next_in = input;
    stream.avail_in = static_cast<mz_uint>(input_size);
    if (mz_inflateInit(&stream) != MZ_OK)
        return false;
    std::array<unsigned char, 64U * 1024U> output{};
    bool valid{};
    while (true) {
        if (context != nullptr && context->interrupted())
            break;
        stream.next_out = output.data();
        stream.avail_out = static_cast<mz_uint>(output.size());
        const auto status = mz_inflate(&stream, MZ_NO_FLUSH);
        const auto produced = output.size() - stream.avail_out;
        if (produced > maximum_output - std::min(maximum_output, result.produced))
            break;
        result.produced += produced;
        if (retain_output)
            result.data.insert(result.data.end(), output.begin(),
                               output.begin() + static_cast<std::ptrdiff_t>(produced));
        if (status == MZ_STREAM_END) {
            result.consumed = static_cast<std::size_t>(stream.total_in);
            valid = result.consumed != 0;
            break;
        }
        if (status != MZ_OK || (produced == 0 && stream.avail_in == 0))
            break;
    }
    mz_inflateEnd(&stream);
    return valid;
}

[[nodiscard]] bool delta_varint(const InflatedObject& object, std::size_t& cursor,
                                std::uintmax_t& value) noexcept {
    value = 0;
    unsigned shift{};
    while (cursor < object.data.size() && shift < 64) {
        const auto byte = object.data[cursor++];
        const auto part = static_cast<std::uintmax_t>(byte & 0x7fU);
        if (part > (std::numeric_limits<std::uintmax_t>::max() >> shift))
            return false;
        value |= part << shift;
        if ((byte & 0x80U) == 0)
            return true;
        shift += 7;
    }
    return false;
}

[[nodiscard]] bool delta_instructions_are_valid(const InflatedObject& object,
                                                std::size_t cursor,
                                                const std::uintmax_t base_size,
                                                const std::uintmax_t result_size) noexcept {
    std::uintmax_t produced{};
    while (cursor < object.data.size()) {
        const auto opcode = object.data[cursor++];
        if (opcode == 0)
            return false;
        if ((opcode & 0x80U) == 0) {
            const auto inserted = static_cast<std::size_t>(opcode);
            if (inserted > object.data.size() - cursor ||
                inserted > result_size - std::min(result_size, produced))
                return false;
            cursor += inserted;
            produced += inserted;
            continue;
        }
        std::uintmax_t copy_offset{};
        std::uintmax_t copy_size{};
        for (unsigned index = 0; index < 4; ++index) {
            if ((opcode & (1U << index)) == 0)
                continue;
            if (cursor == object.data.size())
                return false;
            copy_offset |= static_cast<std::uintmax_t>(object.data[cursor++]) << (index * 8U);
        }
        for (unsigned index = 0; index < 3; ++index) {
            if ((opcode & (1U << (index + 4U))) == 0)
                continue;
            if (cursor == object.data.size())
                return false;
            copy_size |= static_cast<std::uintmax_t>(object.data[cursor++]) << (index * 8U);
        }
        if (copy_size == 0)
            copy_size = 0x10000U;
        if (copy_offset > base_size || copy_size > base_size - copy_offset ||
            copy_size > result_size - std::min(result_size, produced))
            return false;
        produced += copy_size;
    }
    return produced == result_size;
}

[[nodiscard]] bool pack_is_safe(const std::vector<unsigned char>& pack,
                                const Libgit2RuntimeRepositoryFetchLimits& limits,
                                CallbackContext* context = nullptr) {
    const auto reject = [&](const char* reason) noexcept {
        if (context != nullptr)
            context->pack_failure = reason;
        return false;
    };
    if (pack.size() < 32 || std::memcmp(pack.data(), "PACK", 4) != 0)
        return reject("invalid header");
    const auto version = big_endian_u32(reinterpret_cast<const char*>(pack.data() + 4));
    const auto object_count = big_endian_u32(reinterpret_cast<const char*>(pack.data() + 8));
    if ((version != 2 && version != 3) || object_count > limits.max_received_objects)
        return reject("version or object count");
    std::size_t offset = 12;
    std::uintmax_t uncompressed_total{};
    std::vector<std::pair<std::size_t, std::size_t>> object_depths;
    object_depths.reserve(object_count);
    for (std::uint32_t object_index = 0; object_index < object_count; ++object_index) {
        if (context != nullptr && context->interrupted())
            return reject("interrupted");
        if (offset >= pack.size() - 20)
            return reject("truncated object header");
        const auto object_offset = offset;
        auto byte = pack[offset++];
        const auto type = static_cast<unsigned>((byte >> 4U) & 7U);
        std::uintmax_t declared_size = byte & 0x0fU;
        unsigned shift = 4;
        std::size_t size_bytes = 1;
        while ((byte & 0x80U) != 0) {
            if (offset >= pack.size() - 20 || shift >= 64 || ++size_bytes > 10)
                return reject("invalid object size varint");
            byte = pack[offset++];
            const auto part = static_cast<std::uintmax_t>(byte & 0x7fU);
            if (part > (std::numeric_limits<std::uintmax_t>::max() >> shift))
                return reject("overflowing object size varint");
            declared_size |= part << shift;
            shift += 7;
        }
        if (declared_size > limits.max_odb_object_bytes)
            return reject("declared object size limit");
        const bool delta = type == 6 || type == 7;
        std::size_t delta_depth{};
        if (type == 6) {
            if (offset >= pack.size() - 20)
                return reject("truncated OFS_DELTA base");
            byte = pack[offset++];
            std::uintmax_t distance = byte & 0x7fU;
            std::size_t offset_bytes{1};
            while ((byte & 0x80U) != 0) {
                if (offset >= pack.size() - 20 || ++offset_bytes > 10 ||
                    distance > (std::numeric_limits<std::uintmax_t>::max() >> 7U) - 1U)
                    return reject("invalid OFS_DELTA distance");
                byte = pack[offset++];
                distance = ((distance + 1U) << 7U) | (byte & 0x7fU);
            }
            if (distance == 0 || distance > object_offset)
                return reject("invalid OFS_DELTA base offset");
            const auto base_offset = object_offset - static_cast<std::size_t>(distance);
            const auto base = std::ranges::lower_bound(
                object_depths, base_offset, {}, &std::pair<std::size_t, std::size_t>::first);
            if (base == object_depths.end() || base->first != base_offset ||
                base->second == limits.max_delta_depth)
                return reject("missing or over-depth OFS_DELTA base");
            delta_depth = base->second + 1U;
        } else if (type == 7) {
            // A new bare ODB has no external bases. Conservatively reject REF_DELTA
            // so every accepted dependency can be proved by an OFS back-reference.
            if (context != nullptr)
                context->reason = AbortReason::PackRefDelta;
            return reject("REF_DELTA is not preflightable");
        } else if (type == 0 || type == 5) {
            return reject("invalid pack object type");
        }
        std::size_t type_limit = static_cast<std::size_t>(limits.max_odb_object_bytes);
        if (type == 1)
            type_limit = std::min(type_limit, limits.max_commit_bytes);
        else if (type == 2)
            type_limit = std::min(type_limit, limits.max_tree_bytes);
        else if (type == 3)
            type_limit = static_cast<std::size_t>(std::min<std::uintmax_t>(
                type_limit, limits.tree.max_file_bytes));
        else if (type == 4)
            type_limit = std::min(type_limit, limits.max_tag_bytes);
        const auto inflate_limit = delta ? limits.max_delta_instruction_bytes : type_limit;
        InflatedObject inflated;
        if (!inflate_pack_object(pack.data() + offset, pack.size() - 20 - offset, inflate_limit,
                                 delta, context, inflated))
            return reject("object inflate or output limit");
        if (inflated.produced != declared_size)
            return reject("inflated size differs from declared size");
        std::uintmax_t effective_size = declared_size;
        if (delta) {
            std::size_t cursor{};
            std::uintmax_t base_size{};
            std::uintmax_t result_size{};
            if (!delta_varint(inflated, cursor, base_size) ||
                !delta_varint(inflated, cursor, result_size) ||
                base_size > limits.max_odb_object_bytes ||
                result_size > limits.max_odb_object_bytes ||
                !delta_instructions_are_valid(inflated, cursor, base_size, result_size))
                return reject("invalid delta instruction stream");
            effective_size = result_size;
        }
        if (effective_size > limits.max_odb_bytes -
                                 std::min(limits.max_odb_bytes, uncompressed_total))
            return reject("cumulative object size limit");
        uncompressed_total += effective_size;
        offset += inflated.consumed;
        object_depths.emplace_back(object_offset, delta_depth);
    }
    return offset + 20 == pack.size() || reject("pack trailer position");
}

int buffer_and_preflight_upload_pack(BoundedHttpStream& stream) {
    auto& context = *stream.context;
    if (context.upload_pack_rounds == context.limits->max_upload_pack_rounds) {
        context.reason = AbortReason::UploadPackRounds;
        return GIT_EUSER;
    }
    ++context.upload_pack_rounds;
    const auto remaining_budget =
        context.limits->max_pack_bytes -
        std::min(context.limits->max_pack_bytes, context.upload_pack_bytes);
    if (remaining_budget == 0) {
        context.reason = AbortReason::PackBytes;
        return GIT_EUSER;
    }
    if (const auto result = perform_http_request(
            stream, "POST", repository_endpoint_path(stream.repository_path, "/git-upload-pack"),
            "application/x-git-upload-pack-result", remaining_budget,
            stream.upload_pack_response);
        result < 0)
        return result;
    std::vector<unsigned char> pack;
    context.pack_response_preview_size = std::min(
        context.pack_response_preview.size(), stream.upload_pack_response.size());
    std::copy_n(stream.upload_pack_response.begin(), context.pack_response_preview_size,
                context.pack_response_preview.begin());
    pack.reserve(stream.upload_pack_response.size());
    bool negotiation_only{};
    if (!append_pack_from_smart_response(stream.upload_pack_response, pack,
                                         &context.pack_failure, &negotiation_only) ||
        (!negotiation_only && !pack_is_safe(pack, *context.limits, &context))) {
        if (context.reason == AbortReason::None)
            context.reason = AbortReason::PackPreflight;
        return GIT_EUSER;
    }
    context.upload_pack_bytes += stream.upload_pack_response.size();
    stream.upload_pack_buffered = true;
    return 0;
}

int bounded_http_stream_read_impl(git_smart_subtransport_stream* raw_stream, char* buffer,
                                  const std::size_t requested, std::size_t* bytes_read) {
    auto& stream = *reinterpret_cast<BoundedHttpStream*>(raw_stream);
    auto& context = *stream.context;
    if (context.interrupted())
        return GIT_EUSER;
    if (stream.action == GIT_SERVICE_UPLOADPACK_LS) {
        if (!stream.advertisement_buffered) {
            const auto result = buffer_advertisement(stream);
            if (result < 0)
                return result;
        }
        const auto available = stream.advertisement.size() - stream.advertisement_cursor;
        *bytes_read = std::min(requested, available);
        if (*bytes_read != 0)
            std::memcpy(buffer, stream.advertisement.data() + stream.advertisement_cursor,
                        *bytes_read);
        stream.advertisement_cursor += *bytes_read;
        return context.interrupted() ? GIT_EUSER : 0;
    }
    if (!stream.upload_pack_buffered) {
        const auto result = buffer_and_preflight_upload_pack(stream);
        if (result < 0)
            return result;
    }
    const auto available = stream.upload_pack_response.size() - stream.upload_pack_cursor;
    *bytes_read = std::min(requested, available);
    if (*bytes_read != 0)
        std::memcpy(buffer, stream.upload_pack_response.data() + stream.upload_pack_cursor,
                    *bytes_read);
    stream.upload_pack_cursor += *bytes_read;
    return context.interrupted() ? GIT_EUSER : 0;
}

int bounded_http_stream_read(git_smart_subtransport_stream* raw_stream, char* buffer,
                             const std::size_t requested, std::size_t* bytes_read) noexcept {
    try {
        return bounded_http_stream_read_impl(raw_stream, buffer, requested, bytes_read);
    } catch (...) {
        auto& stream = *reinterpret_cast<BoundedHttpStream*>(raw_stream);
        stream.context->reason = AbortReason::Allocation;
        return GIT_EUSER;
    }
}

int bounded_http_stream_write(git_smart_subtransport_stream* raw_stream, const char* buffer,
                              const std::size_t length) noexcept {
    auto& stream = *reinterpret_cast<BoundedHttpStream*>(raw_stream);
    if (stream.context->interrupted())
        return GIT_EUSER;
    if (length == 0)
        return 0;
    if (stream.action != GIT_SERVICE_UPLOADPACK ||
        length > maximum_upload_pack_request_bytes -
                     std::min(maximum_upload_pack_request_bytes,
                              stream.upload_pack_request.size())) {
        stream.context->reason = AbortReason::Http;
        return GIT_EUSER;
    }
    try {
        stream.upload_pack_request.insert(stream.upload_pack_request.end(), buffer,
                                          buffer + length);
        return 0;
    } catch (...) {
        stream.context->reason = AbortReason::Allocation;
        return GIT_EUSER;
    }
}

void bounded_http_stream_free(git_smart_subtransport_stream* raw_stream) noexcept {
    delete reinterpret_cast<BoundedHttpStream*>(raw_stream);
}

int bounded_http_action(git_smart_subtransport_stream** out,
                        git_smart_subtransport* raw_transport, const char* url,
                        const git_smart_service_t action) noexcept {
    auto& transport = *reinterpret_cast<BoundedHttpSubtransport*>(raw_transport);
    if (action != GIT_SERVICE_UPLOADPACK_LS && action != GIT_SERVICE_UPLOADPACK)
        return GIT_EAUTH;
    auto* stream = new (std::nothrow) BoundedHttpStream;
    if (stream == nullptr)
        return GIT_ERROR;
    try {
        stream->parent.subtransport = raw_transport;
        stream->parent.read = &bounded_http_stream_read;
        stream->parent.write = &bounded_http_stream_write;
        stream->parent.free = &bounded_http_stream_free;
        stream->context = transport.context;
        stream->action = action;
        if (url == nullptr)
            throw std::invalid_argument("HTTPS action URL is absent");
        const auto endpoint = parse_https_endpoint(url);
        stream->host = endpoint.host;
        stream->port = endpoint.port;
        stream->repository_path = endpoint.path;
    } catch (...) {
        transport.context->reason = AbortReason::Allocation;
        delete stream;
        return GIT_EUSER;
    }
    *out = &stream->parent;
    return 0;
}

int bounded_http_close(git_smart_subtransport*) noexcept { return 0; }

void bounded_http_free(git_smart_subtransport* raw_transport) noexcept {
    delete reinterpret_cast<BoundedHttpSubtransport*>(raw_transport);
}

int bounded_http_create(git_smart_subtransport** out, git_transport* owner,
                        void* payload) noexcept {
    static_cast<void>(owner);
    auto* transport = new (std::nothrow) BoundedHttpSubtransport;
    if (transport == nullptr)
        return GIT_ERROR;
    transport->parent.action = &bounded_http_action;
    transport->parent.close = &bounded_http_close;
    transport->parent.free = &bounded_http_free;
    transport->context = static_cast<CallbackContext*>(payload);
    *out = &transport->parent;
    return 0;
}

int bounded_https_transport(git_transport** out, git_remote* owner, void* payload) noexcept {
    git_smart_subtransport_definition definition{};
    definition.callback = &bounded_http_create;
    definition.rpc = 1;
    definition.param = payload;
    return git_transport_smart(out, owner, &definition);
}

int credentials_disabled(git_credential**, const char*, const char*, unsigned int,
                         void*) noexcept {
    return GIT_EAUTH;
}

int sideband_progress(const char*, const int, void* payload) noexcept {
    return static_cast<CallbackContext*>(payload)->interrupted() ? GIT_EUSER : 0;
}

int transfer_progress(const git_indexer_progress* progress, void* payload) noexcept {
    auto& context = *static_cast<CallbackContext*>(payload);
    if (context.interrupted())
        return GIT_EUSER;
    if (progress->received_bytes > context.limits->max_pack_bytes) {
        context.reason = AbortReason::PackBytes;
        return GIT_EUSER;
    }
    if (progress->total_objects > context.limits->max_received_objects ||
        progress->received_objects > context.limits->max_received_objects ||
        progress->indexed_objects > context.limits->max_received_objects) {
        context.reason = AbortReason::ReceivedObjects;
        return GIT_EUSER;
    }
    return 0;
}

void throw_callback_failure(const CallbackContext& context, const std::string_view operation) {
    if (context.reason == AbortReason::Cancelled)
        throw RuntimeRepositoryFetchCancelled{};
    if (context.reason == AbortReason::Deadline)
        throw std::runtime_error("libgit2 repository operation deadline exceeded");
    if (context.reason == AbortReason::AdvertisementBytes)
        throw std::runtime_error("libgit2 repository advertisement byte limit exceeded");
    if (context.reason == AbortReason::AdvertisementRefs)
        throw std::runtime_error("libgit2 repository advertisement ref limit exceeded");
    if (context.reason == AbortReason::PackBytes)
        throw std::runtime_error("libgit2 repository pack byte limit exceeded");
    if (context.reason == AbortReason::PackPreflight)
    {
        std::string message =
            std::string("libgit2 repository pack preflight rejected unsafe input: ") +
            (context.pack_failure == nullptr ? "unknown reason" : context.pack_failure) +
            "; response prefix=";
        constexpr char hex[] = "0123456789abcdef";
        for (std::size_t index = 0; index < context.pack_response_preview_size; ++index) {
            const auto byte = context.pack_response_preview[index];
            message.push_back(hex[byte >> 4U]);
            message.push_back(hex[byte & 0x0fU]);
        }
        throw std::runtime_error(message);
    }
    if (context.reason == AbortReason::PackRefDelta)
        throw std::runtime_error("libgit2 repository pack preflight rejected REF_DELTA input");
    if (context.reason == AbortReason::UploadPackRounds)
        throw std::runtime_error("libgit2 repository upload-pack round limit exceeded");
    if (context.reason == AbortReason::Http)
        throw std::runtime_error("bounded HTTPS smart transport failed");
    if (context.reason == AbortReason::Allocation)
        throw std::runtime_error("libgit2 repository bounded transport allocation failed");
    if (context.reason == AbortReason::ReceivedObjects)
        throw std::runtime_error("libgit2 repository received-object limit exceeded");
    if (context.reason == AbortReason::OdbLimit)
        throw std::runtime_error("libgit2 repository object database limit exceeded");
    throw std::runtime_error(git_error_detail(operation));
}

void require_git_or_callback(const int result, const CallbackContext& context,
                             const std::string_view operation) {
    if (result >= 0)
        return;
    throw_callback_failure(context, operation);
}

[[nodiscard]] git_remote_callbacks callbacks(CallbackContext& context,
                                             const bool bounded_https) {
    git_remote_callbacks result = GIT_REMOTE_CALLBACKS_INIT;
    result.sideband_progress = &sideband_progress;
    result.credentials = &credentials_disabled;
    result.transfer_progress = &transfer_progress;
    result.transport = bounded_https ? &bounded_https_transport : nullptr;
    result.payload = &context;
    return result;
}

struct OdbScanContext final {
    git_odb* odb{};
    CallbackContext* callback{};
    std::size_t objects{};
    std::uintmax_t bytes{};
};

int scan_odb(const git_oid* id, void* payload) noexcept {
    auto& scan = *static_cast<OdbScanContext*>(payload);
    if (scan.callback->interrupted())
        return GIT_EUSER;
    std::size_t size{};
    git_object_t type{};
    if (git_odb_read_header(&size, &type, scan.odb, id) < 0)
        return GIT_EUSER;
    const auto& limits = *scan.callback->limits;
    if (++scan.objects > limits.max_odb_objects || size > limits.max_odb_object_bytes ||
        size > limits.max_odb_bytes - std::min<std::uintmax_t>(limits.max_odb_bytes, scan.bytes)) {
        scan.callback->reason = AbortReason::OdbLimit;
        return GIT_EUSER;
    }
    scan.bytes += size;
    return 0;
}

[[nodiscard]] std::string portable_path_key(const std::string_view value) {
    if (value.empty() || value.size() > 1'024 || value.front() == '/' || value.back() == '/' ||
        value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos)
        throw std::runtime_error("repository tree entry path is not portable");
    std::string key;
    key.reserve(value.size());
    std::size_t begin{};
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto component =
            value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (component.empty() || component == "." || component == ".." ||
            component.front() == ' ' || component.back() == '.' || component.back() == ' ')
            throw std::runtime_error("repository tree entry path is not canonical");
        std::string folded;
        folded.reserve(component.size());
        for (std::size_t cursor = 0; cursor < component.size();) {
            const auto first = static_cast<unsigned char>(component[cursor]);
            std::uint32_t codepoint{};
            std::size_t width{};
            if (first < 0x80U) {
                codepoint = first;
                width = 1;
            } else if (first >= 0xc2U && first <= 0xdfU) {
                codepoint = first & 0x1fU;
                width = 2;
            } else if (first >= 0xe0U && first <= 0xefU) {
                codepoint = first & 0x0fU;
                width = 3;
            } else if (first >= 0xf0U && first <= 0xf4U) {
                codepoint = first & 0x07U;
                width = 4;
            } else {
                throw std::runtime_error("repository tree entry path is not valid UTF-8");
            }
            if (cursor + width > component.size())
                throw std::runtime_error("repository tree entry path is not valid UTF-8");
            for (std::size_t index = 1; index < width; ++index) {
                const auto continuation = static_cast<unsigned char>(component[cursor + index]);
                if ((continuation & 0xc0U) != 0x80U)
                    throw std::runtime_error("repository tree entry path is not valid UTF-8");
                codepoint = (codepoint << 6U) | (continuation & 0x3fU);
            }
            const auto overlong = (width == 2 && codepoint < 0x80U) ||
                                  (width == 3 && codepoint < 0x800U) ||
                                  (width == 4 && codepoint < 0x10000U);
            const auto combining = (codepoint >= 0x0300U && codepoint <= 0x036fU) ||
                                   (codepoint >= 0x1ab0U && codepoint <= 0x1affU) ||
                                   (codepoint >= 0x1dc0U && codepoint <= 0x1dffU) ||
                                   (codepoint >= 0x20d0U && codepoint <= 0x20ffU) ||
                                   (codepoint >= 0xfe20U && codepoint <= 0xfe2fU) ||
                                   codepoint == 0x3099U || codepoint == 0x309aU ||
                                   (codepoint >= 0x1100U && codepoint <= 0x11ffU) ||
                                   (codepoint >= 0xa960U && codepoint <= 0xa97fU) ||
                                   (codepoint >= 0xd7b0U && codepoint <= 0xd7ffU);
            if (overlong || codepoint > 0x10ffffU ||
                (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint == 0x85U ||
                codepoint == 0x2028U || codepoint == 0x2029U ||
                (codepoint >= 0x7fU && codepoint <= 0x9fU) ||
                (codepoint >= 0xfdd0U && codepoint <= 0xfdefU) ||
                (codepoint & 0xffffU) == 0xfffeU || (codepoint & 0xffffU) == 0xffffU || combining)
                throw std::runtime_error("repository tree entry path is not portable UTF-8");
            if (width == 1) {
                const auto character = static_cast<char>(first);
                if (first < 0x20U || character == '<' || character == '>' || character == '"' ||
                    character == '|' || character == '?' || character == '*')
                    throw std::runtime_error("repository tree entry path contains a reserved character");
                folded.push_back(character >= 'A' && character <= 'Z'
                                     ? static_cast<char>(character + ('a' - 'A'))
                                     : character);
            } else {
                folded.append(component.substr(cursor, width));
            }
            cursor += width;
        }
        const auto dot = folded.find('.');
        const auto stem = folded.substr(0, dot);
        const auto reserved =
            stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem.back() >= '1' && stem.back() <= '9') ||
            (stem.size() == 5 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             (stem.ends_with("\xc2\xb9") || stem.ends_with("\xc2\xb2") ||
              stem.ends_with("\xc2\xb3")));
        if (reserved)
            throw std::runtime_error("repository tree entry path uses a reserved name");
        if (!key.empty())
            key.push_back('/');
        key += folded;
        if (end == std::string_view::npos)
            break;
        begin = end + 1;
    }
    return key;
}

[[nodiscard]] std::filesystem::path utf8_path(const std::string_view value) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

struct FilePlan final {
    std::string relative;
    git_oid oid{};
    std::uintmax_t size{};
};

struct TreePlan final {
    std::vector<std::string> directories;
    std::vector<FilePlan> files;
    std::uintmax_t total_bytes{};
    std::size_t entries{};
    std::set<std::string, std::less<>> portable_paths;
};

[[nodiscard]] git_object_t require_object_header(
    git_odb* odb, const git_oid* oid, const std::size_t maximum_bytes,
    const std::string_view operation) {
    std::size_t size{};
    git_object_t type{};
    require_git(git_odb_read_header(&size, &type, odb, oid), operation);
    if (size > maximum_bytes)
        throw std::runtime_error(std::string(operation) + ": metadata object limit exceeded");
    return type;
}

void preflight_tree(git_repository* repository, git_odb* odb, const git_tree* tree,
                    const std::string& prefix, const std::size_t depth,
                    const RepositoryFetchSpec& spec, CallbackContext& callback, TreePlan& plan) {
    if (callback.interrupted())
        throw_callback_failure(callback, "repository tree preflight interrupted");
    const auto& limits = callback.limits->tree;
    if (depth > limits.max_relative_path_depth)
        throw std::runtime_error("repository tree depth limit exceeded");
    const auto count = git_tree_entrycount(tree);
    if (count == 0 && !prefix.empty())
        throw std::runtime_error("repository tree contains an empty directory");
    for (std::size_t index = 0; index < count; ++index) {
        if (callback.interrupted())
            throw_callback_failure(callback, "repository tree preflight interrupted");
        const auto* entry = git_tree_entry_byindex(tree, index);
        const auto* raw_name = git_tree_entry_name(entry);
        if (raw_name == nullptr)
            throw std::runtime_error("repository tree entry name is absent");
        const std::string_view name(raw_name);
        const auto relative = prefix.empty() ? std::string(name) : prefix + "/" + std::string(name);
        if (relative.size() > limits.max_relative_path_bytes || ++plan.entries > limits.max_entries)
            throw std::runtime_error("repository tree path or entry limit exceeded");
        if (!plan.portable_paths.emplace(portable_path_key(relative)).second)
            throw std::runtime_error("repository tree contains a portable path collision");
        if (prefix.empty() && name == transport_directory_name)
            throw std::runtime_error("repository tree collides with the private transport path");
        const auto mode = git_tree_entry_filemode(entry);
        if (mode == GIT_FILEMODE_TREE) {
            const auto type = require_object_header(odb, git_tree_entry_id(entry),
                                                    callback.limits->max_tree_bytes,
                                                    "repository subtree header read failed");
            if (type != GIT_OBJECT_TREE)
                throw std::runtime_error("repository subtree entry has the wrong object type");
            GitOwner<git_tree, git_tree_free> child(nullptr, &git_tree_free);
            git_tree* raw{};
            require_git(git_tree_lookup(&raw, repository, git_tree_entry_id(entry)),
                        "repository subtree lookup failed");
            child.reset(raw);
            plan.directories.push_back(relative);
            preflight_tree(repository, odb, child.get(), relative, depth + 1, spec, callback, plan);
            continue;
        }
        if (mode != GIT_FILEMODE_BLOB)
            throw std::runtime_error("repository tree contains a non-100644 entry");
        if (plan.files.size() == limits.max_files)
            throw std::runtime_error("repository tree file limit exceeded");
        std::size_t size{};
        git_object_t type{};
        require_git(git_odb_read_header(&size, &type, odb, git_tree_entry_id(entry)),
                    "repository blob header read failed");
        if (type != GIT_OBJECT_BLOB)
            throw std::runtime_error("repository tree blob entry has the wrong object type");
        const auto maximum = relative == spec.manifest
                                 ? std::min<std::uintmax_t>(limits.max_file_bytes,
                                                           limits.max_manifest_bytes)
                                 : limits.max_file_bytes;
        if (size > maximum ||
            size > limits.max_total_bytes -
                       std::min<std::uintmax_t>(limits.max_total_bytes, plan.total_bytes))
            throw std::runtime_error("repository tree blob or total byte limit exceeded");
        plan.total_bytes += size;
        plan.files.push_back({relative, *git_tree_entry_id(entry), size});
    }
}

class ExclusiveOutput final {
  public:
    explicit ExclusiveOutput(const std::filesystem::path& path) : path_(path) {
#ifdef _WIN32
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "exclusive repository file creation failed");
#else
        descriptor_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                             S_IRUSR | S_IWUSR);
        if (descriptor_ < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "exclusive repository file creation failed");
#endif
    }

    ~ExclusiveOutput() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
#else
        if (descriptor_ >= 0)
            ::close(descriptor_);
#endif
        if (!complete_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

    ExclusiveOutput(const ExclusiveOutput&) = delete;
    ExclusiveOutput& operator=(const ExclusiveOutput&) = delete;

    void write_all(const char* bytes, std::size_t size) {
        while (size != 0) {
#ifdef _WIN32
            const auto chunk = static_cast<DWORD>(
                std::min<std::size_t>(size, std::numeric_limits<DWORD>::max()));
            DWORD written{};
            if (WriteFile(handle_, bytes, chunk, &written, nullptr) == 0 || written == 0)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                        "repository file write failed");
            bytes += written;
            size -= written;
#else
            const auto written = ::write(descriptor_, bytes, size);
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                throw std::system_error(errno, std::generic_category(),
                                        "repository file write failed");
            }
            if (written == 0)
                throw std::runtime_error("repository file write made no progress");
            bytes += written;
            size -= static_cast<std::size_t>(written);
#endif
        }
    }

    void complete() noexcept { complete_ = true; }

  private:
    std::filesystem::path path_;
    bool complete_{};
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int descriptor_{-1};
#endif
};

void write_odb_blob(git_odb* odb, const FilePlan& file, ExclusiveOutput& output,
                    CallbackContext& callback) {
    git_odb_stream* raw_stream{};
    std::size_t length{};
    git_object_t type{};
    const auto stream_result = git_odb_open_rstream(&raw_stream, &length, &type, odb, &file.oid);
    if (stream_result == 0) {
        GitOwner<git_odb_stream, git_odb_stream_free> stream(raw_stream, &git_odb_stream_free);
        if (type != GIT_OBJECT_BLOB || length != file.size)
            throw std::runtime_error("repository blob stream metadata changed");
        std::array<char, 64U * 1024U> buffer{};
        std::size_t remaining = length;
        while (remaining != 0) {
            if (callback.interrupted())
                throw_callback_failure(callback, "repository blob stream interrupted");
            const auto requested = std::min(remaining, buffer.size());
            const auto read = git_odb_stream_read(stream.get(), buffer.data(), requested);
            if (read <= 0 || static_cast<std::size_t>(read) > requested)
                throw std::runtime_error("repository blob stream read failed");
            output.write_all(buffer.data(), static_cast<std::size_t>(read));
            remaining -= static_cast<std::size_t>(read);
        }
        return;
    }
    if (file.size > callback.limits->max_fallback_blob_bytes)
        throw std::runtime_error("repository blob cannot be streamed within fallback memory limit");
    git_odb_object* raw_object{};
    require_git(git_odb_read(&raw_object, odb, &file.oid), "repository blob read failed");
    GitOwner<git_odb_object, git_odb_object_free> object(raw_object, &git_odb_object_free);
    if (git_odb_object_type(object.get()) != GIT_OBJECT_BLOB ||
        git_odb_object_size(object.get()) != file.size)
        throw std::runtime_error("repository blob metadata changed during bounded read");
    const auto* bytes = static_cast<const char*>(git_odb_object_data(object.get()));
    if (file.size != 0 && bytes == nullptr)
        throw std::runtime_error("repository blob content is unavailable");
    output.write_all(bytes, static_cast<std::size_t>(file.size));
}

void materialize_tree(git_odb* odb, const std::filesystem::path& staging,
                      const TreePlan& plan, CallbackContext& callback) {
    for (const auto& relative : plan.directories) {
        if (callback.interrupted())
            throw_callback_failure(callback, "repository tree materialization interrupted");
        std::error_code error;
        if (!std::filesystem::create_directory(staging / utf8_path(relative), error) || error)
            throw std::filesystem::filesystem_error("repository directory creation failed",
                                                    staging / utf8_path(relative), error);
    }
    for (const auto& file : plan.files) {
        if (callback.interrupted())
            throw_callback_failure(callback, "repository tree materialization interrupted");
        const auto target = staging / utf8_path(file.relative);
        std::error_code exists_error;
        if (std::filesystem::exists(target, exists_error) || exists_error)
            throw std::runtime_error("repository file path collides during materialization");
        ExclusiveOutput output(target);
        write_odb_blob(odb, file, output, callback);
        output.complete();
    }
}

[[nodiscard]] GitOwner<git_commit, git_commit_free>
peel_commit(git_repository* repository, git_odb* odb, const git_oid& initial,
            const Libgit2RuntimeRepositoryFetchLimits& limits) {
    git_oid current = initial;
    for (std::size_t depth = 0; depth <= limits.max_peel_depth; ++depth) {
        std::size_t size{};
        git_object_t type{};
        require_git(git_odb_read_header(&size, &type, odb, &current),
                    "fetched reference object header read failed");
        if (type == GIT_OBJECT_COMMIT) {
            if (size > limits.max_commit_bytes)
                throw std::runtime_error("fetched commit metadata limit exceeded");
            git_commit* commit{};
            require_git(git_commit_lookup(&commit, repository, &current),
                        "fetched commit lookup failed");
            return GitOwner<git_commit, git_commit_free>(commit, &git_commit_free);
        }
        if (type != GIT_OBJECT_TAG || depth == limits.max_peel_depth)
            throw std::runtime_error("advertised reference does not peel to a bounded commit");
        if (size > limits.max_tag_bytes)
            throw std::runtime_error("fetched tag metadata limit exceeded");
        git_tag* raw_tag{};
        require_git(git_tag_lookup(&raw_tag, repository, &current),
                    "fetched tag lookup failed");
        GitOwner<git_tag, git_tag_free> tag(raw_tag, &git_tag_free);
        const auto* target = git_tag_target_id(tag.get());
        if (target == nullptr)
            throw std::runtime_error("annotated tag target is absent");
        current = *target;
    }
    throw std::runtime_error("advertised reference peel limit exceeded");
}

[[nodiscard]] std::string oid_string(const git_oid* oid) {
    std::array<char, GIT_OID_SHA1_HEXSIZE + 1> buffer{};
    if (git_oid_tostr(buffer.data(), buffer.size(), oid) == nullptr)
        throw std::runtime_error("commit OID formatting failed");
    return buffer.data();
}

class PrivateTransportCleanup final {
  public:
    explicit PrivateTransportCleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ~PrivateTransportCleanup() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

  private:
    std::filesystem::path path_;
};

} // namespace

struct Libgit2RuntimeRepositoryFetchBackend::Impl final {
    explicit Impl(Libgit2RuntimeRepositoryFetchOptions requested)
        : options(std::move(requested)) {
        validate_limits(options.limits);
        validate_trusted_ca_bundle(options.trusted_ca_bundle);
    }

    GitLifetime lifetime;
    Libgit2RuntimeRepositoryFetchOptions options;
};

Libgit2RuntimeRepositoryFetchBackend::Libgit2RuntimeRepositoryFetchBackend(
    Libgit2RuntimeRepositoryFetchOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

Libgit2RuntimeRepositoryFetchBackend::~Libgit2RuntimeRepositoryFetchBackend() = default;

RepositoryStageResult Libgit2RuntimeRepositoryFetchBackend::stage_exact(
    const RepositoryFetchSpec& spec,
    const std::filesystem::path& updater_owned_staging_directory,
    const std::stop_token stop_token) {
    const auto& options = impl_->options;
    const bool https = valid_https_url(spec.remote_url);
#ifdef BAAS_RUNTIME_REPOSITORY_GIT2_TESTING
    const bool test_file = file_transport_enabled_for_testing && valid_test_file_url(spec.remote_url);
#else
    constexpr bool test_file = false;
#endif
    if (!https && !test_file)
        throw std::invalid_argument("runtime repository remote must be credential-free HTTPS");
    if (!valid_advertised_reference(spec.advertised_reference))
        throw std::invalid_argument("runtime repository advertised ref must be a full heads/tags ref");
    if (!lowercase_sha1(spec.exact_commit))
        throw std::invalid_argument("libgit2 backend requires an exact lowercase SHA-1 commit");
    if (stop_token.stop_requested())
        throw RuntimeRepositoryFetchCancelled{};
    std::error_code path_error;
    if (!std::filesystem::is_directory(updater_owned_staging_directory, path_error) || path_error ||
        !std::filesystem::is_empty(updater_owned_staging_directory, path_error) || path_error)
        throw std::invalid_argument("libgit2 backend requires an empty updater-owned staging directory");

    CallbackContext context{stop_token,
                            std::chrono::steady_clock::now() + options.limits.absolute_timeout,
                            &options.limits,
                            &options.trusted_ca_bundle};
    const auto transport_root = updater_owned_staging_directory / transport_directory_name;
    PrivateTransportCleanup failure_cleanup(transport_root);
    std::string resolved;
    {
        git_repository* raw_repository{};
        const auto transport_root_utf8 = git_path_utf8(transport_root);
        require_git(git_repository_init(&raw_repository, transport_root_utf8.c_str(), 1),
                    "private libgit2 repository initialization failed");
        GitOwner<git_repository, git_repository_free> repository(raw_repository,
                                                                 &git_repository_free);

        git_remote_create_options remote_options = GIT_REMOTE_CREATE_OPTIONS_INIT;
        remote_options.repository = repository.get();
        remote_options.flags = GIT_REMOTE_CREATE_SKIP_INSTEADOF |
                               GIT_REMOTE_CREATE_SKIP_DEFAULT_FETCHSPEC;
        git_remote* raw_remote{};
        require_git(git_remote_create_with_opts(&raw_remote, spec.remote_url.c_str(),
                                                &remote_options),
                    "anonymous libgit2 remote creation failed");
        GitOwner<git_remote, git_remote_free> remote(raw_remote, &git_remote_free);

        {
            auto remote_callbacks = callbacks(context, https);
            const auto refspec = "+" + spec.advertised_reference + ":" +
                                 std::string(candidate_reference);
            char* refspec_pointer = const_cast<char*>(refspec.c_str());
            git_strarray refspecs{&refspec_pointer, 1};
            git_fetch_options fetch_options = GIT_FETCH_OPTIONS_INIT;
            fetch_options.callbacks = remote_callbacks;
            fetch_options.prune = GIT_FETCH_NO_PRUNE;
            fetch_options.update_fetchhead = 0;
            fetch_options.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
            fetch_options.proxy_opts.type = GIT_PROXY_NONE;
            fetch_options.depth = https ? 1 : 0;
            fetch_options.follow_redirects = GIT_REMOTE_REDIRECT_NONE;
            require_git_or_callback(
                git_remote_fetch(remote.get(), &refspecs, &fetch_options, nullptr), context,
                "libgit2 exact-ref fetch failed");
        }

        git_odb* raw_odb{};
        require_git(git_repository_odb(&raw_odb, repository.get()),
                    "libgit2 object database lookup failed");
        GitOwner<git_odb, git_odb_free> odb(raw_odb, &git_odb_free);
        OdbScanContext scan{odb.get(), &context};
        const auto scan_result = git_odb_foreach(odb.get(), &scan_odb, &scan);
        if (scan_result < 0)
            throw_callback_failure(context, "libgit2 object database scan failed");

        git_reference* raw_reference{};
        require_git(git_reference_lookup(&raw_reference, repository.get(),
                                         std::string(candidate_reference).c_str()),
                    "fetched candidate reference lookup failed");
        GitOwner<git_reference, git_reference_free> reference(raw_reference, &git_reference_free);
        const auto* target = git_reference_target(reference.get());
        if (target == nullptr)
            throw std::runtime_error("fetched candidate reference has no direct target");
        auto commit = peel_commit(repository.get(), odb.get(), *target, options.limits);
        resolved = oid_string(git_commit_id(commit.get()));
        if (resolved != spec.exact_commit)
            throw std::runtime_error("advertised ref resolved to a different commit");

        const auto* tree_id = git_commit_tree_id(commit.get());
        if (tree_id == nullptr)
            throw std::runtime_error("fetched commit tree id is absent");
        const auto tree_type = require_object_header(odb.get(), tree_id,
                                                     options.limits.max_tree_bytes,
                                                     "fetched root tree header read failed");
        if (tree_type != GIT_OBJECT_TREE)
            throw std::runtime_error("fetched root tree has the wrong object type");
        git_tree* raw_tree{};
        require_git(git_tree_lookup(&raw_tree, repository.get(), tree_id),
                    "fetched commit tree lookup failed");
        GitOwner<git_tree, git_tree_free> tree(raw_tree, &git_tree_free);
        TreePlan plan;
        preflight_tree(repository.get(), odb.get(), tree.get(), {}, 1, spec, context, plan);
        materialize_tree(odb.get(), updater_owned_staging_directory, plan, context);
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(transport_root, cleanup_error);
    if (cleanup_error || std::filesystem::exists(transport_root))
        throw std::runtime_error("private libgit2 transport cleanup failed");
    StrictRuntimeRepositoryTreeValidator validator(options.limits.tree);
    static_cast<void>(
        validator.validate_and_seal(spec, updater_owned_staging_directory, stop_token));
    return {std::move(resolved)};
}

} // namespace baas::runtime::repository

#ifdef BAAS_RUNTIME_REPOSITORY_GIT2_TESTING
namespace baas::runtime::repository::testing {

void set_file_transport_enabled(const bool enabled) noexcept {
    file_transport_enabled_for_testing = enabled;
}

std::string git_path_bytes(const std::filesystem::path& path) {
    return git_path_utf8(path);
}

bool advertisement_is_safe(const std::vector<char>& advertisement,
                           const std::size_t maximum_refs) noexcept {
    return advertisement_ref_count_is_bounded(advertisement, maximum_refs);
}

bool https_endpoint_is_valid(const std::string_view url) noexcept {
    try {
        static_cast<void>(parse_https_endpoint(url));
        return true;
    } catch (...) {
        return false;
    }
}

bool trusted_https_advertisement_is_reachable(
    const std::string_view url,
    const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept {
    try {
        const auto endpoint = parse_https_endpoint(url);
        CallbackContext context{std::stop_token{},
                                std::chrono::steady_clock::now() + limits.absolute_timeout,
                                &limits};
        BoundedHttpStream stream;
        stream.context = &context;
        stream.action = GIT_SERVICE_UPLOADPACK_LS;
        stream.host = endpoint.host;
        stream.port = endpoint.port;
        stream.repository_path = endpoint.path;
        return buffer_advertisement(stream) == 0;
    } catch (...) {
        return false;
    }
}

bool trusted_https_roundtrip_resolves(
    const std::string_view url, const std::string_view advertised_reference,
    const std::string_view exact_commit, const std::filesystem::path& scratch_directory,
    const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept {
    try {
        validate_limits(limits);
        if (!valid_https_url(url) || !valid_advertised_reference(std::string(advertised_reference)) ||
            !lowercase_sha1(exact_commit) || !std::filesystem::is_empty(scratch_directory))
            return false;
        GitLifetime lifetime;
        CallbackContext context{std::stop_token{},
                                std::chrono::steady_clock::now() + limits.absolute_timeout,
                                &limits};
        const auto transport_root = scratch_directory / ".baas-git-roundtrip";
        PrivateTransportCleanup cleanup(transport_root);
        git_repository* raw_repository{};
        const auto transport_root_utf8 = git_path_utf8(transport_root);
        require_git(git_repository_init(&raw_repository, transport_root_utf8.c_str(), 1),
                    "roundtrip repository initialization failed");
        GitOwner<git_repository, git_repository_free> repository(raw_repository,
                                                                 &git_repository_free);

        git_remote_create_options remote_options = GIT_REMOTE_CREATE_OPTIONS_INIT;
        remote_options.repository = repository.get();
        remote_options.flags = GIT_REMOTE_CREATE_SKIP_INSTEADOF |
                               GIT_REMOTE_CREATE_SKIP_DEFAULT_FETCHSPEC;
        git_remote* raw_remote{};
        const std::string remote_url(url);
        require_git(git_remote_create_with_opts(&raw_remote, remote_url.c_str(), &remote_options),
                    "roundtrip remote creation failed");
        GitOwner<git_remote, git_remote_free> remote(raw_remote, &git_remote_free);

        auto remote_callbacks = callbacks(context, true);
        const auto refspec = "+" + std::string(advertised_reference) + ":" +
                             std::string(candidate_reference);
        char* refspec_pointer = const_cast<char*>(refspec.c_str());
        git_strarray refspecs{&refspec_pointer, 1};
        git_fetch_options fetch_options = GIT_FETCH_OPTIONS_INIT;
        fetch_options.callbacks = remote_callbacks;
        fetch_options.prune = GIT_FETCH_NO_PRUNE;
        fetch_options.update_fetchhead = 0;
        fetch_options.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
        fetch_options.proxy_opts.type = GIT_PROXY_NONE;
        fetch_options.depth = 1;
        fetch_options.follow_redirects = GIT_REMOTE_REDIRECT_NONE;
        require_git_or_callback(git_remote_fetch(remote.get(), &refspecs, &fetch_options, nullptr),
                                context, "roundtrip exact-ref fetch failed");

        git_odb* raw_odb{};
        require_git(git_repository_odb(&raw_odb, repository.get()),
                    "roundtrip object database lookup failed");
        GitOwner<git_odb, git_odb_free> odb(raw_odb, &git_odb_free);
        OdbScanContext scan{odb.get(), &context};
        if (git_odb_foreach(odb.get(), &scan_odb, &scan) < 0)
            throw_callback_failure(context, "roundtrip object database scan failed");

        git_reference* raw_reference{};
        require_git(git_reference_lookup(&raw_reference, repository.get(),
                                         std::string(candidate_reference).c_str()),
                    "roundtrip candidate reference lookup failed");
        GitOwner<git_reference, git_reference_free> reference(raw_reference,
                                                               &git_reference_free);
        const auto* target = git_reference_target(reference.get());
        if (target == nullptr)
            return false;
        const auto commit = peel_commit(repository.get(), odb.get(), *target, limits);
        return oid_string(git_commit_id(commit.get())) == exact_commit;
    } catch (...) {
        return false;
    }
}

bool pack_is_safe(const std::vector<unsigned char>& pack,
                  const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept {
    try {
        return ::baas::runtime::repository::pack_is_safe(pack, limits);
    } catch (...) {
        return false;
    }
}

bool smart_response_is_safe(const std::vector<char>& response,
                            const Libgit2RuntimeRepositoryFetchLimits& limits) noexcept {
    try {
        std::vector<unsigned char> pack;
        return append_pack_from_smart_response(response, pack) &&
               ::baas::runtime::repository::pack_is_safe(pack, limits);
    } catch (...) {
        return false;
    }
}

bool negotiation_response_is_safe(const std::vector<char>& response) noexcept {
    try {
        std::vector<unsigned char> pack;
        bool negotiation_only{};
        return append_pack_from_smart_response(response, pack, nullptr, &negotiation_only) &&
               negotiation_only && pack.empty();
    } catch (...) {
        return false;
    }
}

bool upload_pack_budget_accepts(const std::vector<std::size_t>& response_sizes,
                                const std::size_t byte_limit,
                                const std::size_t round_limit) noexcept {
    std::size_t used{};
    std::size_t rounds{};
    for (const auto size : response_sizes) {
        if (rounds == round_limit || size > byte_limit - std::min(byte_limit, used))
            return false;
        ++rounds;
        used += size;
    }
    return true;
}

bool pack_preflight_is_interrupted(const std::vector<unsigned char>& pack,
                                   const Libgit2RuntimeRepositoryFetchLimits& limits,
                                   const bool cancel, const bool expired) noexcept {
    std::stop_source source;
    if (cancel)
        source.request_stop();
    CallbackContext context{source.get_token(),
                            expired ? std::chrono::steady_clock::now() -
                                          std::chrono::milliseconds(1)
                                    : std::chrono::steady_clock::now() + std::chrono::hours(1),
                            &limits};
    try {
        return !::baas::runtime::repository::pack_is_safe(pack, limits, &context) &&
               (context.reason == AbortReason::Cancelled ||
                context.reason == AbortReason::Deadline);
    } catch (...) {
        return false;
    }
}

} // namespace baas::runtime::repository::testing
#endif

extern "C" int baas_runtime_repository_git2_dependency_anchor() noexcept {
    int major = 0;
    int minor = 0;
    int revision = 0;
    git_libgit2_version(&major, &minor, &revision);
    return major * 10000 + minor * 100 + revision;
}
