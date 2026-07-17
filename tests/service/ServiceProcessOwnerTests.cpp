#include "service/supervisor/ServiceProcessOwner.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

namespace supervisor = baas::service::supervisor;
using namespace std::chrono_literals;

constexpr std::string_view arguments_file =
    "service-process-test-arguments.bin";
constexpr std::string_view exit_file = "service-process-test-exit-code.txt";

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path path_from_utf8(
    const std::u8string_view value)
{
#if defined(_WIN32)
    return std::filesystem::path{value};
#else
    return std::filesystem::path{std::string{
        reinterpret_cast<const char*>(value.data()), value.size()}};
#endif
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& value)
{
#if defined(_WIN32)
    const auto encoded = value.u8string();
    return {
        reinterpret_cast<const char*>(encoded.data()), encoded.size()};
#else
    return value.native();
#endif
}

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        static std::atomic<std::uint64_t> sequence{};
        auto name = path_from_utf8(u8"baas owner 空格 ");
        name += std::to_string(sequence.fetch_add(1));
        path_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(path_);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    TemporaryDirectory(TemporaryDirectory&& other) noexcept
        : path_(std::move(other.path_))
    {}

    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    ~TemporaryDirectory()
    {
        std::error_code error;
        if (!path_.empty()) std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

struct Fixture {
    TemporaryDirectory temporary;
    std::filesystem::path executable;
    std::filesystem::path project_root;
};

[[nodiscard]] Fixture fixture(const std::filesystem::path& source_executable)
{
    Fixture result;
    const auto child_directory =
        result.temporary.path() / path_from_utf8(u8"child folder 子");
    result.project_root =
        result.temporary.path() / path_from_utf8(u8"project root 项目");
    std::filesystem::create_directories(child_directory);
    std::filesystem::create_directories(result.project_root);
    result.executable = child_directory / source_executable.filename();
    std::filesystem::copy_file(
        source_executable, result.executable,
        std::filesystem::copy_options::overwrite_existing);
#if !defined(_WIN32)
    std::filesystem::permissions(
        result.executable,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write
            | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);
#endif
    return result;
}

[[nodiscard]] supervisor::ServiceProcessConfig config(const Fixture& value)
{
    return {
        value.executable,
        value.project_root,
        std::string{supervisor::service_process_loopback_host},
        43891,
        std::string(64, 'a'),
    };
}

[[nodiscard]] bool wait_for_file(
    const std::filesystem::path& path,
    const std::chrono::milliseconds timeout = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error) && !error) return true;
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

[[nodiscard]] std::vector<std::string> read_arguments(
    const std::filesystem::path& project_root)
{
    std::ifstream input(
        project_root / arguments_file,
        std::ios::binary);
    std::uint32_t count = 0;
    input.read(
        reinterpret_cast<char*>(&count),
        static_cast<std::streamsize>(sizeof(count)));
    if (!input || count > 32) return {};
    std::vector<std::string> arguments;
    arguments.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint32_t size = 0;
        input.read(
            reinterpret_cast<char*>(&size),
            static_cast<std::streamsize>(sizeof(size)));
        if (!input || size > 4'096) return {};
        std::string argument(size, '\0');
        input.read(argument.data(), static_cast<std::streamsize>(argument.size()));
        if (!input) return {};
        arguments.push_back(std::move(argument));
    }
    return arguments;
}

[[nodiscard]] bool process_exists(const std::uint64_t process_id)
{
#if defined(_WIN32)
    const auto process = OpenProcess(
        SYNCHRONIZE, FALSE, static_cast<DWORD>(process_id));
    if (process == nullptr) return false;
    const auto wait = WaitForSingleObject(process, 0);
    static_cast<void>(CloseHandle(process));
    return wait == WAIT_TIMEOUT;
#else
    if (process_id > static_cast<std::uint64_t>(
            std::numeric_limits<pid_t>::max())) {
        return false;
    }
    const auto process = static_cast<pid_t>(process_id);
    if (::kill(process, 0) == 0) return true;
    return errno == EPERM;
#endif
}

void check_exact_arguments(const Fixture& value)
{
    const auto arguments = read_arguments(value.project_root);
    const std::vector<std::string> expected{
        "--project-root",
        path_to_utf8(std::filesystem::canonical(value.project_root)),
        "--host",
        "127.0.0.1",
        "--port",
        "43891",
        "--runtime-repository-generation",
        std::string(64, 'a'),
    };
    check(arguments == expected,
          "child must receive the exact fixed BAAS_service argv vector");
}

void test_invalid_configuration(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    supervisor::ServiceProcessOwner owner;

    auto invalid = config(value);
    invalid.service_executable = "relative-service";
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "relative executable must be rejected");

    invalid = config(value);
    invalid.service_executable = value.temporary.path() / "missing-service";
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "missing executable must be rejected");

    invalid = config(value);
    invalid.project_root = "relative-project";
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "relative project root must be rejected");

    invalid = config(value);
    invalid.host = "localhost";
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "noncanonical loopback host must be rejected");

    invalid = config(value);
    invalid.port = 0;
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "zero port must be rejected");

    invalid = config(value);
    invalid.runtime_repository_generation = std::string(64, 'A');
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "noncanonical generation must be rejected");

    const auto bad_executable = value.temporary.path() / "bad executable";
    {
        std::ofstream output(bad_executable, std::ios::binary);
        output << "not an executable";
    }
#if !defined(_WIN32)
    std::filesystem::permissions(
        bad_executable,
        std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write
            | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);
#endif
    invalid = config(value);
    invalid.service_executable = bad_executable;
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::launch_failed,
          "invalid executable image must report launch failure");
    check(owner.state() == supervisor::ServiceProcessState::stopped,
          "failed launches must preserve stopped state");
}

void test_normal_exit_and_exact_argv(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    {
        std::ofstream exit(value.project_root / exit_file);
        exit << 23;
    }
    supervisor::ServiceProcessOwner owner;
    const auto started = owner.start(config(value));
    check(static_cast<bool>(started), "normal-exit child must start");
    const auto waited = owner.wait_for(5s);
    check(waited.disposition
                  == supervisor::ServiceProcessWaitDisposition::exited
              && waited.exit.has_value() && !waited.exit->signaled
              && waited.exit->exit_code == 23,
          "normal child exit must be reaped with its exact code");
    check(owner.state() == supervisor::ServiceProcessState::exited,
          "waited child must enter exited state");
    check_exact_arguments(value);
    check(owner.stop() == supervisor::ServiceProcessError::none
              && owner.stop() == supervisor::ServiceProcessError::none,
          "stop after normal exit must be idempotent");
}

void test_duplicate_start_timeout_and_stop(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    supervisor::ServiceProcessOwner owner;
    const auto started = owner.start(config(value));
    check(static_cast<bool>(started), "blocking child must start");
    check(wait_for_file(value.project_root / arguments_file),
          "blocking child must publish its argv fixture");
    check(owner.start(config(value)).error
              == supervisor::ServiceProcessError::already_active,
          "duplicate start must be rejected by the single-owner state machine");
    check(owner.wait_for(20ms).disposition
              == supervisor::ServiceProcessWaitDisposition::timed_out,
          "bounded wait must report timeout without losing ownership");
    check(owner.stop(5s) == supervisor::ServiceProcessError::none,
          "stop must terminate and reap the process group/job");
    check(owner.state() == supervisor::ServiceProcessState::stopped
              && !process_exists(started.process_id),
          "stopped owner must retain no live child");
    check(owner.stop(5s) == supervisor::ServiceProcessError::none,
          "repeated stop must be a successful no-op");
}

void test_destruction_owns_child(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    std::uint64_t process_id = 0;
    {
        supervisor::ServiceProcessOwner owner;
        const auto started = owner.start(config(value));
        check(static_cast<bool>(started), "destructor fixture child must start");
        process_id = started.process_id;
        check(wait_for_file(value.project_root / arguments_file),
              "destructor fixture child must reach its blocking phase");
    }
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (process_exists(process_id)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    check(!process_exists(process_id),
          "owner destruction must terminate and reap its child");
}

}  // namespace

int main(const int argc, char* argv[])
{
    if (argc != 2 || argv == nullptr || argv[1] == nullptr) {
        std::cerr << "ServiceProcessOwnerTests requires the helper executable\n";
        return 2;
    }
    try {
        const std::filesystem::path helper{argv[1]};
        test_invalid_configuration(helper);
        test_normal_exit_and_exact_argv(helper);
        test_duplicate_start_timeout_and_stop(helper);
        test_destruction_owns_child(helper);
    } catch (const std::exception& error) {
        std::cerr << "FAILED: unexpected exception: " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAILED: unexpected non-standard exception\n";
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " service process owner test(s) failed\n";
        return 1;
    }
    std::cout << "Service process owner tests passed\n";
    return 0;
}
