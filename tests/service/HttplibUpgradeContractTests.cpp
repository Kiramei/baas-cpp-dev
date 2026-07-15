#include "http_contract.h"

#include <httplib.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#if !defined(CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH)
#error "BAAS::httplib must publish the WebSocket payload limit to every consumer"
#endif

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_pinned_version_and_process_wide_configuration()
{
    check(std::string_view{CPPHTTPLIB_VERSION} == "0.50.1",
          "the imported package must expose cpp-httplib 0.50.1");
    check(CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH == 67'108'864,
          "all header-only consumers must observe the BAAS 64 MiB WebSocket limit");
    check(BAAS_OCR::http_contract::image_max_length == 67'108'864
              && BAAS_OCR::http_contract::json_max_length == 1'048'576
              && BAAS_OCR::http_contract::multipart_overhead_max_length == 65'536
              && BAAS_OCR::http_contract::payload_max_length == 68'222'976
              && BAAS_OCR::http_contract::worker_count == 8
              && BAAS_OCR::http_contract::max_queued_requests == 16,
          "OCR total payload must retain explicit image, JSON, and framing bounds");
    BAAS_OCR::http_contract::require_size_at_most(
        BAAS_OCR::http_contract::image_max_length,
        BAAS_OCR::http_contract::image_max_length,
        "boundary"
    );
    bool oversized_rejected = false;
    try {
        BAAS_OCR::http_contract::require_size_at_most(
            BAAS_OCR::http_contract::image_max_length + 1,
            BAAS_OCR::http_contract::image_max_length,
            "boundary"
        );
    } catch (const std::length_error&) {
        oversized_rejected = true;
    }
    check(oversized_rejected, "OCR per-part limit must reject maximum plus one");

    httplib::Server configured;
    BAAS_OCR::http_contract::configure_server(configured);
}

void test_v050_multipart_field_and_file_contract()
{
    httplib::Request request;
    request.body = R"({"fallback":true})";
    const auto body_payload = BAAS_OCR::http_contract::request_json_payload(request);
    check(body_payload == request.body && body_payload.data() == request.body.data(),
          "non-multipart OCR requests must continue to use the request body");
    check(BAAS_OCR::http_contract::request_image_file(request) == nullptr,
          "missing multipart image must remain distinguishable from an empty file");

    httplib::FormField data;
    data.name = "data";
    data.content = R"({"image":{"pass_method":1}})";
    request.form.fields.emplace("data", data);

    httplib::FormData image;
    image.name = "image";
    image.filename = "image.png";
    image.content_type = "image/png";
    image.content.assign("\x89PNG\r\n", 6);
    request.form.files.emplace("image", image);
    const auto retained_size = data.content.size() + image.content.size();
    request.set_header("Content-Length", std::to_string(retained_size + 128));

    const auto parsed_data = BAAS_OCR::http_contract::request_json_payload(request);
    check(parsed_data == data.content
              && parsed_data.data()
                  == request.form.fields.find("data")->second.content.data(),
          "requests files=(data=(None,...)) must be read as a form field");
    const auto* const file = BAAS_OCR::http_contract::request_image_file(request);
    check(file != nullptr && file->filename == "image.png"
              && file->content == image.content
              && file == &request.form.files.find("image")->second,
          "the multipart image must be read from the v0.50 file collection");

    request.headers.erase("Content-Length");
    request.set_header(
        "Content-Length",
        std::to_string(
            retained_size + BAAS_OCR::http_contract::multipart_overhead_max_length
        )
    );
    BAAS_OCR::http_contract::validate_multipart_shape_and_overhead(request);
    request.headers.erase("Content-Length");
    request.set_header(
        "Content-Length",
        std::to_string(
            retained_size + BAAS_OCR::http_contract::multipart_overhead_max_length + 1
        )
    );
    bool overhead_rejected = false;
    try {
        BAAS_OCR::http_contract::validate_multipart_shape_and_overhead(request);
    } catch (const std::length_error&) {
        overhead_rejected = true;
    }
    check(overhead_rejected,
          "multipart framing must reject its explicit maximum plus one");
    request.headers.erase("Content-Length");
    request.set_header("Content-Length", std::to_string(retained_size + 128));

    request.form.fields.emplace("data", data);
    bool duplicate_data_rejected = false;
    try {
        static_cast<void>(BAAS_OCR::http_contract::request_json_payload(request));
    } catch (const std::invalid_argument&) {
        duplicate_data_rejected = true;
    }
    check(duplicate_data_rejected,
          "ambiguous duplicate multipart JSON fields must fail closed");

    request.form.fields.erase("data");
    request.form.fields.emplace("data", data);
    request.form.files.emplace("image", image);
    check(BAAS_OCR::http_contract::request_image_file(request) == nullptr,
          "ambiguous duplicate multipart image files must fail closed");
}

void test_fixed_worker_and_waiting_queue_semantics()
{
    std::mutex mutex;
    std::condition_variable changed;
    bool first_started = false;
    bool release_first = false;
    std::atomic<int> completed{0};

    httplib::Server configured;
    BAAS_OCR::http_contract::configure_server(configured, 1, 1);
    std::unique_ptr<httplib::TaskQueue> pool{configured.new_task_queue()};
    check(pool != nullptr, "the production OCR configuration must install a task queue");
    check(pool->enqueue([&] {
        std::unique_lock<std::mutex> lock{mutex};
        first_started = true;
        changed.notify_all();
        changed.wait(lock, [&] { return release_first; });
        completed.fetch_add(1);
    }), "the fixed pool must accept its active task");
    {
        std::unique_lock<std::mutex> lock{mutex};
        changed.wait(lock, [&] { return first_started; });
    }
    check(pool->enqueue([&] { completed.fetch_add(1); }),
          "the fixed pool must accept one waiting task");
    check(!pool->enqueue([&] { completed.fetch_add(100); }),
          "the third task must be rejected at the one-request queue bound");
    {
        std::lock_guard<std::mutex> lock{mutex};
        release_first = true;
    }
    changed.notify_all();
    pool->shutdown();
    check(completed.load() == 2,
          "fixed worker and bounded queue must execute exactly two accepted tasks");
}

}  // namespace

int main()
{
    test_pinned_version_and_process_wide_configuration();
    test_v050_multipart_field_and_file_contract();
    test_fixed_worker_and_waiting_queue_semantics();
    if (failures != 0) {
        std::cerr << failures << " cpp-httplib upgrade contract test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "cpp-httplib upgrade contract tests passed\n";
    return EXIT_SUCCESS;
}
