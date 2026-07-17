#include "service/supervisor/ServiceProcessOwner.h"
#include "service/app/ServiceCommandLine.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
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
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

namespace supervisor = baas::service::supervisor;
namespace service_app = baas::service::app;
using namespace std::chrono_literals;

constexpr std::string_view arguments_file =
    "service-process-test-arguments.bin";
constexpr std::string_view exit_file = "service-process-test-exit-code.txt";
constexpr std::string_view ready_file = "service-process-test-ready.txt";
constexpr std::string_view spawn_grandchild_file =
    "service-process-test-spawn-grandchild.txt";
constexpr std::string_view grandchild_pid_file =
    "service-process-test-grandchild-pid.txt";
constexpr std::string_view environment_file =
    "service-process-test-environment.txt";
constexpr std::string_view sentinel_file =
    "service-process-test-inheritance-sentinel.txt";
constexpr std::string_view sentinel_result_file =
    "service-process-test-inheritance-result.txt";

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
        auto name = path_from_utf8(u8"baas owner & (空格) %! ");
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
    std::filesystem::path application_root;
    std::filesystem::path working_directory;
    std::filesystem::path project_root;
};

[[nodiscard]] Fixture fixture(const std::filesystem::path& source_executable)
{
    Fixture result;
    result.application_root = result.temporary.path()
        / path_from_utf8(u8"application root & (可信) %!");
    const auto child_directory = result.application_root
        / path_from_utf8(u8"child & folder (子) ^!");
    result.working_directory = result.application_root
        / path_from_utf8(u8"safe cwd & (目录) %!");
    result.project_root =
        result.temporary.path() / path_from_utf8(u8"project root & (项目) %!");
    std::filesystem::create_directories(child_directory);
    std::filesystem::create_directories(result.working_directory);
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
        value.application_root,
        value.working_directory,
        value.project_root,
        std::string{supervisor::service_process_loopback_host},
        43891,
        std::string(64, 'a'),
    };
}

void prepare_start(const Fixture& value)
{
    std::error_code error;
    for (const auto name : {
             arguments_file, ready_file, grandchild_pid_file,
             environment_file, sentinel_result_file}) {
        std::filesystem::remove(value.project_root / name, error);
        error.clear();
    }
}

[[nodiscard]] bool wait_for_file(
    const std::filesystem::path& path,
    std::chrono::milliseconds timeout = 5s);
[[nodiscard]] std::string read_text(const std::filesystem::path& path);

void test_no_inherited_handles(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    prepare_start(value);
    std::uint64_t sentinel_value = 0;
#if defined(_WIN32)
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    const HANDLE sentinel = CreateEventW(&attributes, TRUE, FALSE, nullptr);
    check(sentinel != nullptr, "inheritable Windows sentinel must be created");
    if (sentinel == nullptr) return;
    sentinel_value = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(sentinel));
#else
    const int source = ::open(
        (value.temporary.path() / "sentinel source").c_str(),
        O_CREAT | O_RDWR, 0600);
    check(source >= 0, "POSIX sentinel source fd must be created");
    if (source < 0) return;
    const int sentinel = ::fcntl(source, F_DUPFD, 198);
    ::close(source);
    check(sentinel >= 198, "high inheritable POSIX sentinel fd must be created");
    if (sentinel < 0) return;
    static_cast<void>(::fcntl(sentinel, F_SETFD, 0));
    sentinel_value = static_cast<std::uint64_t>(sentinel);
#endif
    {
        std::ofstream marker(value.project_root / sentinel_file);
        marker << sentinel_value;
    }
    supervisor::ServiceProcessOwner owner;
    check(static_cast<bool>(owner.start(config(value))),
          "inheritance-boundary fixture child must start");
    check(wait_for_file(value.project_root / ready_file),
          "inheritance-boundary fixture must become ready");
    check(read_text(value.project_root / sentinel_result_file) == "closed",
          "child must not inherit the explicitly inheritable handle/fd");
    check(owner.stop(5s) == supervisor::ServiceProcessError::none,
          "inheritance-boundary fixture must stop cleanly");
#if defined(_WIN32)
    CloseHandle(sentinel);
#else
    ::close(sentinel);
#endif
}

[[nodiscard]] bool wait_for_file(
    const std::filesystem::path& path,
    const std::chrono::milliseconds timeout)
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
        path_to_utf8(std::filesystem::canonical(value.executable)),
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
    std::vector<std::string_view> parser_arguments;
    if (!arguments.empty()) {
        parser_arguments.reserve(arguments.size() - 1);
        for (auto argument = std::next(arguments.begin());
             argument != arguments.end(); ++argument) {
            parser_arguments.emplace_back(*argument);
        }
    }
    const auto parsed = service_app::parse_service_command_line(
        parser_arguments, service_app::native_service_command_line_platform());
    check(parsed.disposition == service_app::ServiceCommandLineDisposition::run
              && parsed.error == service_app::ServiceCommandLineError::none
              && parsed.options.project_root
                  == std::filesystem::canonical(value.project_root)
              && parsed.options.host == "127.0.0.1"
              && parsed.options.port == 43'891
              && parsed.options.runtime_repository_generation
                  == std::string(64, 'a'),
          "the real BAAS_service parser must accept the owner's exact argv");
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

[[nodiscard]] std::uint64_t read_process_id(
    const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::uint64_t value = 0;
    input >> value;
    return input ? value : 0;
}

void test_invalid_configuration(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    supervisor::ServiceProcessOwner owner;
#if !defined(_WIN32)
    check(owner.emergency_reaper_ready_for_tests(),
          "POSIX emergency reaper must exist before any child can start");
#endif

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
    invalid.trusted_application_root = value.project_root;
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "executable outside the trusted application root must be rejected");

    invalid = config(value);
    invalid.safe_working_directory = value.project_root;
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "working directory outside the trusted root must be rejected");

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

    for (const auto& generation : {
             std::string(63, 'a'), std::string(65, 'a'),
             std::string(64, 'g')}) {
        invalid = config(value);
        invalid.runtime_repository_generation = generation;
        check(owner.start(invalid).error
                  == supervisor::ServiceProcessError::invalid_configuration,
              "generation length and alphabet must be exact");
    }

    invalid = config(value);
    invalid.project_root = value.temporary.path() / "missing project root";
    check(owner.start(invalid).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "missing project root must be rejected");

    const auto bad_executable =
        value.application_root / "bad executable image";
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
    prepare_start(value);
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
    check(wait_for_file(value.project_root / ready_file),
          "child must atomically publish its completed fixture");
    check_exact_arguments(value);
    check(read_text(value.project_root / environment_file) == "empty",
          "child must receive the controlled empty environment");
    check(owner.stop() == supervisor::ServiceProcessError::none
              && owner.stop() == supervisor::ServiceProcessError::none,
          "stop after normal exit must be idempotent");
}

void test_duplicate_start_timeout_and_stop(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    prepare_start(value);
    supervisor::ServiceProcessOwner owner;
    const auto started = owner.start(config(value));
    check(static_cast<bool>(started), "blocking child must start");
    check(wait_for_file(value.project_root / ready_file),
          "blocking child must atomically publish its fixture");
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
    prepare_start(value);
    {
        std::ofstream marker(value.project_root / spawn_grandchild_file);
        marker << "spawn";
    }
    std::uint64_t process_id = 0;
    std::uint64_t grandchild_id = 0;
    {
        supervisor::ServiceProcessOwner owner;
        const auto started = owner.start(config(value));
        check(static_cast<bool>(started), "destructor fixture child must start");
        process_id = started.process_id;
        check(wait_for_file(value.project_root / ready_file),
              "destructor fixture child must reach its blocking phase");
        grandchild_id =
            read_process_id(value.project_root / grandchild_pid_file);
        check(grandchild_id != 0 && process_exists(grandchild_id),
              "destructor fixture must create a live grandchild");
    }
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (process_exists(process_id)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    check(!process_exists(process_id),
          "owner destruction must terminate and reap its child");
    const auto grandchild_deadline = std::chrono::steady_clock::now() + 5s;
    while (process_exists(grandchild_id)
           && std::chrono::steady_clock::now() < grandchild_deadline) {
        std::this_thread::sleep_for(5ms);
    }
    check(!process_exists(grandchild_id),
          "owner destruction must terminate the complete child tree");
}

void test_concurrent_wait_stop_and_restart(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    prepare_start(value);
    supervisor::ServiceProcessOwner owner;
    const auto first = owner.start(config(value));
    check(static_cast<bool>(first), "concurrency fixture child must start");
    check(wait_for_file(value.project_root / ready_file),
          "concurrency fixture child must become ready");

    auto waiter = std::async(std::launch::async, [&owner] {
        return owner.wait_for(std::chrono::hours{1});
    });
    std::this_thread::sleep_for(20ms);
    const auto stop_started = std::chrono::steady_clock::now();
    auto first_stop = std::async(std::launch::async, [&owner] {
        return owner.stop(5s);
    });
    auto second_stop = std::async(std::launch::async, [&owner] {
        return owner.stop(5s);
    });
    check(first_stop.get() == supervisor::ServiceProcessError::none
              && second_stop.get() == supervisor::ServiceProcessError::none,
          "concurrent stops must both linearize successfully");
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
    check(stop_elapsed < 2s,
          "concurrent long wait must not hold the owner state lock");
    check(waiter.wait_for(2s) == std::future_status::ready,
          "long waiter must be notified by concurrent stop");
    if (waiter.wait_for(0ms) == std::future_status::ready) {
        const auto result = waiter.get();
        check(result.disposition
                  == supervisor::ServiceProcessWaitDisposition::not_running,
              "waiter linearizes after completed stop");
    }

    prepare_start(value);
    const auto second = owner.start(config(value));
    check(static_cast<bool>(second) && second.process_id != first.process_id,
          "owner must support restart after completed stop/reset");
    check(wait_for_file(value.project_root / ready_file),
          "restarted child must become ready");
    check(owner.stop(5s) == supervisor::ServiceProcessError::none,
          "restarted child must stop cleanly");
}

void test_timeout_extremes(const std::filesystem::path& helper)
{
    auto value = fixture(helper);
    prepare_start(value);
    {
        std::ofstream exit(value.project_root / exit_file);
        exit << 7;
    }
    supervisor::ServiceProcessOwner owner;
    check(owner.wait_for(-1ms).error
              == supervisor::ServiceProcessError::invalid_configuration,
          "negative wait timeout must be rejected");
    check(owner.stop(-1ms)
              == supervisor::ServiceProcessError::invalid_configuration,
          "negative stop timeout must be rejected");
    check(static_cast<bool>(owner.start(config(value))),
          "huge-timeout fixture child must start");
    const auto waited = owner.wait_for(std::chrono::milliseconds::max());
    check(waited.disposition
                  == supervisor::ServiceProcessWaitDisposition::exited
              && waited.exit && waited.exit->exit_code == 7,
          "maximum timeout must saturate and still observe early exit");
    check(owner.stop(5s) == supervisor::ServiceProcessError::none,
          "normal exit must reset for restart");
}

void test_process_tree_cleanup(
    const std::filesystem::path& helper,
    const bool natural_leader_exit)
{
    auto value = fixture(helper);
    prepare_start(value);
    {
        std::ofstream marker(value.project_root / spawn_grandchild_file);
        marker << "spawn";
    }
    if (natural_leader_exit) {
        std::ofstream exit(value.project_root / exit_file);
        exit << 19;
    }
    supervisor::ServiceProcessOwner owner;
    check(static_cast<bool>(owner.start(config(value))),
          "process-tree fixture leader must start");
    check(wait_for_file(value.project_root / ready_file),
          "process-tree fixture must publish grandchild identity");
    const auto grandchild =
        read_process_id(value.project_root / grandchild_pid_file);
    check(grandchild != 0 && process_exists(grandchild),
          "helper must create a live grandchild in the owned tree");
    if (natural_leader_exit) {
        const auto waited = owner.wait_for(5s);
        check(waited.disposition
                      == supervisor::ServiceProcessWaitDisposition::exited
                  && waited.exit && waited.exit->exit_code == 19,
              "natural leader exit must be observed before tree cleanup");
        check(owner.stop(5s) == supervisor::ServiceProcessError::none,
              "natural-exit owner must reset cleanly");
    } else {
        check(owner.stop(5s) == supervisor::ServiceProcessError::none,
              "forced stop must terminate the complete child tree");
    }
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (process_exists(grandchild)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    check(!process_exists(grandchild),
          "no owned grandchild may survive leader cleanup");
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
        test_no_inherited_handles(helper);
        test_normal_exit_and_exact_argv(helper);
        test_duplicate_start_timeout_and_stop(helper);
        test_destruction_owns_child(helper);
        test_concurrent_wait_stop_and_restart(helper);
        test_timeout_extremes(helper);
        test_process_tree_cleanup(helper, false);
        test_process_tree_cleanup(helper, true);
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
