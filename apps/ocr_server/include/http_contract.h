#ifndef BAAS_OCR_SERVER_HTTP_CONTRACT_H_
#define BAAS_OCR_SERVER_HTTP_CONTRACT_H_

#include <httplib.h>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace BAAS_OCR::http_contract {

inline constexpr std::size_t image_max_length = 64U * 1024U * 1024U;
inline constexpr std::size_t json_max_length = 1U * 1024U * 1024U;
inline constexpr std::size_t multipart_overhead_max_length = 64U * 1024U;
inline constexpr std::size_t payload_max_length =
    image_max_length + json_max_length + multipart_overhead_max_length;
inline constexpr std::size_t worker_count = 8U;
inline constexpr std::size_t max_queued_requests = 16U;

inline void require_size_at_most(
    const std::size_t actual,
    const std::size_t maximum,
    const char* const description
)
{
    if (actual > maximum) {
        throw std::length_error{std::string{description} + " exceeds its transport limit"};
    }
}

inline void configure_server(
    httplib::Server& server,
    const std::size_t workers = worker_count,
    const std::size_t queued_requests = max_queued_requests
)
{
    if (workers == 0 || queued_requests == 0) {
        throw std::invalid_argument{"OCR HTTP workers and queued requests must be positive"};
    }
    server.set_payload_max_length(payload_max_length);
    server.new_task_queue = [workers, queued_requests] {
        return new httplib::ThreadPool(workers, workers, queued_requests);
    };
}

inline void validate_multipart_shape_and_overhead(const httplib::Request& request)
{
    if (request.form.fields.empty() && request.form.files.empty()) return;
    if (request.form.fields.size() != 1
        || request.form.get_field_count("data") != 1
        || request.form.files.size() != 1
        || request.form.get_file_count("image") != 1) {
        throw std::invalid_argument{
            "multipart OCR request must contain exactly one 'data' field and one 'image' file"
        };
    }
    if (request.get_header_value_count("Content-Length") != 1) {
        throw std::invalid_argument{
            "multipart OCR request requires exactly one Content-Length"
        };
    }
    const auto length_text = request.get_header_value("Content-Length");
    std::uint64_t declared_length = 0;
    const auto parsed = std::from_chars(
        length_text.data(), length_text.data() + length_text.size(), declared_length
    );
    if (parsed.ec != std::errc{} || parsed.ptr != length_text.data() + length_text.size()) {
        throw std::invalid_argument{"multipart OCR Content-Length is invalid"};
    }

    const auto& data = request.form.fields.find("data")->second.content;
    const auto& image = request.form.files.find("image")->second.content;
    const auto retained_length = static_cast<std::uint64_t>(data.size())
        + static_cast<std::uint64_t>(image.size());
    if (declared_length < retained_length
        || declared_length > static_cast<std::uint64_t>(payload_max_length)
        || declared_length - retained_length
            > static_cast<std::uint64_t>(multipart_overhead_max_length)) {
        throw std::length_error{"multipart OCR framing exceeds its transport limit"};
    }
}

[[nodiscard]] inline std::string_view request_json_payload(const httplib::Request& request)
{
    validate_multipart_shape_and_overhead(request);
    const auto count = request.form.get_field_count("data");
    if (count > 1) {
        throw std::invalid_argument{"multipart request contains duplicate 'data' fields"};
    }
    const std::string* content = &request.body;
    if (count == 1) {
        // v0.50.1 get_field() returns std::string by value. Reference the
        // documented public fields collection to avoid copying attacker-sized
        // input immediately before JSON parsing.
        content = &request.form.fields.find("data")->second.content;
    }
    require_size_at_most(content->size(), json_max_length, "OCR JSON field");
    return *content;
}

[[nodiscard]] inline const httplib::FormData* request_image_file(
    const httplib::Request& request
)
{
    if (request.form.get_file_count("image") != 1) return nullptr;
    // v0.50.1 get_file() returns FormData by value, including its content.
    // Keep a reference to the public files collection so a 64 MiB upload is
    // not duplicated before cv::imdecode consumes it.
    const auto& file = request.form.files.find("image")->second;
    require_size_at_most(file.content.size(), image_max_length, "OCR image file");
    return &file;
}

}  // namespace BAAS_OCR::http_contract

#endif  // BAAS_OCR_SERVER_HTTP_CONTRACT_H_
