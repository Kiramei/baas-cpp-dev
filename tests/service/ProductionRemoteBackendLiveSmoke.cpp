#include "service/adb/ServiceAdbTransport.h"
#include "service/adapters/FileResourceStore.h"
#include "service/app/ProductionRemoteBackend.h"
#include "service/auth/Crypto.h"

#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace std::chrono_literals;

constexpr std::string_view server_identity =
    "com.genymobile.scrcpy.Server 1.19-ws7";
constexpr std::string_view owner_lease_glob =
    "/data/local/tmp/baas-ws-scrcpy.owner.*";
constexpr std::string_view global_lease =
    "/data/local/tmp/baas-ws-scrcpy.lease";
constexpr std::string_view scrcpy_initial_magic = "scrcpy_initial";
constexpr std::string_view scrcpy_device_message_magic = "scrcpy_message";

// Mirrors the default Tauri stream request: type 101 followed by a 35-byte
// VideoSettings payload (7 Mi bit/s, 60 fps, 10 s I-frame interval,
// 1280x720, display 0, raw Annex-B H.264 without frame metadata).
constexpr std::array<std::byte, 36> video_start_command{
    std::byte{0x65},
    std::byte{0x00}, std::byte{0x70}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x3c},
    std::byte{0x0a},
    std::byte{0x05}, std::byte{0x00},
    std::byte{0x02}, std::byte{0xd0},
    std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00},
    std::byte{0x00},
    std::byte{0xff},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
};

[[nodiscard]] bool contains_h264_slice(const std::string_view payload) noexcept
{
    for (std::size_t index = 0; index + 3 < payload.size(); ++index) {
        if (payload[index] != '\0' || payload[index + 1] != '\0') continue;
        std::size_t header{};
        if (payload[index + 2] == '\1') header = index + 3;
        else if (index + 4 < payload.size()
                 && payload[index + 2] == '\0'
                 && payload[index + 3] == '\1') {
            header = index + 4;
        }
        else continue;
        const auto nal_type = static_cast<unsigned char>(payload[header]) & 0x1fU;
        if (nal_type == 1U || nal_type == 5U) return true;
    }
    return false;
}

[[nodiscard]] const char* remote_error_name(
    baas::service::channels::RemoteBackendError error) noexcept
{
    using Error = baas::service::channels::RemoteBackendError;
    switch (error) {
    case Error::none:
        return "none";
    case Error::not_found:
        return "not_found";
    case Error::invalid_config:
        return "invalid_config";
    case Error::capacity:
        return "capacity";
    case Error::internal_error:
        return "internal_error";
    }
    return "unknown";
}

[[nodiscard]] std::optional<unsigned int> parse_timeout(std::string_view text)
{
    unsigned int value{};
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || value < 1 || value > 120) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] bool has_remote_forward(
    baas::service::adb::ServiceAdbTransport& transport,
    std::string_view serial,
    std::string& diagnostic)
{
    const auto result = transport.list_forwards();
    if (!result) {
        diagnostic = "ADB forward inspection failed: " + result.message;
        return true;
    }
    for (const auto& item : *result.value) {
        if (item.serial == serial && item.remote == "tcp:8886") {
            diagnostic = "exact serial still owns a tcp:8886 forward";
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool has_server_process(
    baas::service::adb::ServiceAdbTransport& transport,
    std::string_view serial,
    std::string& diagnostic)
{
    const auto result = transport.shell_legacy(serial, "ps -A -o PID,ARGS");
    if (!result) {
        diagnostic = "ADB process inspection failed: " + result.message;
        return true;
    }
    if (result->find(server_identity) != std::string::npos) {
        diagnostic = "exact serial still has a matching ws-scrcpy process";
        return true;
    }
    return false;
}

[[nodiscard]] bool has_owner_artifact(
    baas::service::adb::ServiceAdbTransport& transport,
    std::string_view serial,
    std::string& diagnostic)
{
    const auto result = transport.shell_legacy(
        serial,
        "if [ -e /data/local/tmp/baas-ws-scrcpy.lease ] || "
        "[ -L /data/local/tmp/baas-ws-scrcpy.lease ]; then "
        "echo /data/local/tmp/baas-ws-scrcpy.lease; fi; "
        "ls /data/local/tmp/baas-ws-scrcpy.owner.* 2>/dev/null || true");
    if (!result) {
        diagnostic = "ADB owner-artifact inspection failed: " + result.message;
        return true;
    }
    if (result->find(global_lease) != std::string::npos
        || result->find(owner_lease_glob) != std::string::npos
        || result->find("baas-ws-scrcpy.owner.") != std::string::npos) {
        diagnostic =
            "exact serial still has a ws-scrcpy global lease or owner directory";
        return true;
    }
    return false;
}

[[nodiscard]] bool clean_device_state(
    baas::service::adb::ServiceAdbTransport& transport,
    std::string_view serial,
    std::string& diagnostic)
{
    return !has_remote_forward(transport, serial, diagnostic)
        && !has_server_process(transport, serial, diagnostic)
        && !has_owner_artifact(transport, serial, diagnostic);
}

int run(int argc, char** argv)
{
    if (argc < 5 || argc > 6) {
        std::cerr
            << "usage: BAAS_service_remote_backend_live_smoke "
               "<project-root> <config-id> <scrcpy-server.jar> "
               "<exact-adb-serial> [timeout-seconds]\n";
        return 2;
    }

    const std::filesystem::path project_root(argv[1]);
    const std::string config_id(argv[2]);
    const std::filesystem::path server_jar(argv[3]);
    const std::string serial(argv[4]);
    const auto timeout_seconds = argc == 6
        ? parse_timeout(argv[5])
        : std::optional<unsigned int>(30);
    if (!timeout_seconds) {
        std::cerr << "timeout-seconds must be an integer from 1 through 120\n";
        return 2;
    }
    if (!std::filesystem::is_directory(project_root)) {
        std::cerr << "project root is not a directory: " << project_root << '\n';
        return 2;
    }
    if (!std::filesystem::is_regular_file(server_jar)) {
        std::cerr << "scrcpy server is not a regular file: " << server_jar << '\n';
        return 2;
    }

    auto transport =
        std::make_shared<baas::service::adb::ServiceAdbTransport>();
    const auto state = transport->get_state(serial);
    if (!state || *state.value != "device") {
        std::cerr << "exact ADB serial is not ready: " << serial;
        if (!state.message.empty()) {
            std::cerr << " (" << state.message << ')';
        }
        std::cerr << '\n';
        return 1;
    }

    std::string diagnostic;
    if (!clean_device_state(*transport, serial, diagnostic)) {
        std::cerr << "refusing to run on a dirty device: " << diagnostic << '\n';
        return 1;
    }

    auto resources =
        std::make_shared<baas::service::adapters::FileResourceStore>(project_root);
    const auto resource = resources->pull(
        {baas::service::channels::SyncResource::config, config_id}, {});
    if (!resource) {
        std::cerr << "configuration resource pull failed: "
                  << static_cast<unsigned int>(resource.error) << '\n';
        return 1;
    }
    if (resource->data_json.empty()) {
        std::cerr << "configuration resource is empty\n";
        return 1;
    }
    baas::service::app::ProductionRemoteBackendDependencies dependencies;
    dependencies.resources = resources;
    dependencies.adb_transport = transport;
    dependencies.server_jar = server_jar;
    baas::service::app::ProductionRemoteBackend backend(std::move(dependencies));

    std::mutex mutex;
    std::condition_variable condition;
    std::size_t frames{};
    std::size_t bytes{};
    bool initial_received{};
    bool ended{};
    baas::service::channels::RemoteSessionEnd end_reason{
        baas::service::channels::RemoteSessionEnd::clean};

    baas::service::channels::RemoteSessionCallbacks callbacks;
    callbacks.device_bytes = [&](
        baas::service::channels::RemoteDeviceMessageKind kind,
        std::string payload) {
        {
            std::lock_guard lock(mutex);
            if (kind
                == baas::service::channels::RemoteDeviceMessageKind::binary) {
                if (payload.starts_with(scrcpy_initial_magic)) {
                    initial_received = true;
                }
                else if (!payload.starts_with(scrcpy_device_message_magic)
                         && contains_h264_slice(payload)) {
                    ++frames;
                    bytes += payload.size();
                }
            }
        }
        condition.notify_all();
        return baas::service::channels::RemoteIoStatus::accepted;
    };
    callbacks.ended = [&](baas::service::channels::RemoteSessionEnd reason) {
        {
            std::lock_guard lock(mutex);
            ended = true;
            end_reason = reason;
        }
        condition.notify_all();
    };

    auto opened = backend.open(config_id, std::move(callbacks), {});
    if (!opened) {
        backend.stop();
        auto failure_inspection = baas::service::adb::ServiceAdbTransport();
        std::string cleanup_diagnostic;
        if (!clean_device_state(
                failure_inspection, serial, cleanup_diagnostic)) {
            std::cerr << "failed-open cleanup verification failed: "
                      << cleanup_diagnostic << '\n';
            return 1;
        }
        std::cerr << "production remote open failed: "
                  << remote_error_name(opened.error)
                  << " (resource bytes=" << resource->data_json.size()
                  << ")\n";
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(*timeout_seconds);
    {
        std::unique_lock lock(mutex);
        condition.wait_until(
            lock, deadline, [&] { return initial_received || ended; });
    }

    baas::service::channels::RemoteIoStatus start_status{
        baas::service::channels::RemoteIoStatus::closed};
    bool observed_initial{};
    {
        std::lock_guard lock(mutex);
        observed_initial = initial_received;
    }
    if (observed_initial) {
        start_status = opened.session->send_to_device(
            baas::service::auth::SecretBuffer(std::span<const std::byte>{
                video_start_command.data(), video_start_command.size()}),
            {});
        if (start_status == baas::service::channels::RemoteIoStatus::accepted) {
            std::unique_lock lock(mutex);
            condition.wait_until(
                lock, deadline, [&] { return frames > 0 || ended; });
        }
    }

    opened.session->close();
    backend.stop();
    opened.session.reset();
    transport.reset();

    std::size_t observed_frames{};
    std::size_t observed_bytes{};
    bool observed_end{};
    baas::service::channels::RemoteSessionEnd observed_reason{};
    {
        std::lock_guard lock(mutex);
        observed_frames = frames;
        observed_bytes = bytes;
        observed_end = ended;
        observed_reason = end_reason;
    }

    auto inspection = baas::service::adb::ServiceAdbTransport();
    diagnostic.clear();
    if (!clean_device_state(inspection, serial, diagnostic)) {
        std::cerr << "remote cleanup verification failed: " << diagnostic << '\n';
        return 1;
    }
    if (!observed_initial) {
        std::cerr << "no ws-scrcpy initial metadata arrived before timeout\n";
        return 1;
    }
    if (start_status != baas::service::channels::RemoteIoStatus::accepted) {
        std::cerr << "video stream command was not accepted: "
                  << static_cast<unsigned int>(start_status) << '\n';
        return 1;
    }
    if (observed_frames == 0) {
        std::cerr << "no H.264 slice frame arrived before timeout";
        if (observed_end) {
            std::cerr << " (session ended with reason "
                      << static_cast<unsigned int>(observed_reason) << ')';
        }
        std::cerr << '\n';
        return 1;
    }

    std::cout << "live remote smoke passed: serial=" << serial
              << " frames=" << observed_frames
              << " bytes=" << observed_bytes << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "live remote smoke failed with exception: "
                  << error.what() << '\n';
    } catch (...) {
        std::cerr << "live remote smoke failed with an unknown exception\n";
    }
    return 1;
}
