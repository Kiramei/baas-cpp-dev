#include "service/supervisor/ServiceProcessOwner.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <AvailabilityMacros.h>
#include <sys/event.h>
#endif

#endif

namespace baas::service::supervisor {
namespace {

struct ValidatedConfig {
    std::filesystem::path executable;
    std::filesystem::path application_root;
    std::filesystem::path working_directory;
    std::filesystem::path project_root;
    std::string host;
    std::uint16_t port = 0;
    std::string generation;
};

[[nodiscard]] bool is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) noexcept
{
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() || *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool canonical_generation(const std::string_view value) noexcept
{
    return value.size() == 64
        && std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
               return (byte >= '0' && byte <= '9')
                   || (byte >= 'a' && byte <= 'f');
           });
}

[[nodiscard]] std::optional<ValidatedConfig> validate_config(
    ServiceProcessConfig config) noexcept
{
    try {
        if (config.service_executable.empty()
            || !config.service_executable.is_absolute()
            || config.trusted_application_root.empty()
            || !config.trusted_application_root.is_absolute()
            || config.safe_working_directory.empty()
            || !config.safe_working_directory.is_absolute()
            || config.project_root.empty() || !config.project_root.is_absolute()
            || config.host != service_process_loopback_host || config.port == 0
            || !canonical_generation(config.runtime_repository_generation)) {
            return std::nullopt;
        }
        std::error_code error;
        const auto executable_status =
            std::filesystem::symlink_status(config.service_executable, error);
        if (error || executable_status.type() != std::filesystem::file_type::regular) {
            return std::nullopt;
        }
        const auto executable = std::filesystem::canonical(
            config.service_executable, error);
        if (error || executable.empty()) return std::nullopt;
#if !defined(_WIN32)
        if (::access(executable.c_str(), X_OK) != 0) return std::nullopt;
#endif
        error.clear();
        const auto application_status = std::filesystem::symlink_status(
            config.trusted_application_root, error);
        if (error
            || application_status.type()
                != std::filesystem::file_type::directory) {
            return std::nullopt;
        }
        const auto application_root = std::filesystem::canonical(
            config.trusted_application_root, error);
        if (error || application_root.empty()
            || !is_within(application_root, executable)) {
            return std::nullopt;
        }
        error.clear();
        const auto working_status = std::filesystem::symlink_status(
            config.safe_working_directory, error);
        if (error
            || working_status.type() != std::filesystem::file_type::directory) {
            return std::nullopt;
        }
        const auto working_directory = std::filesystem::canonical(
            config.safe_working_directory, error);
        if (error || working_directory.empty()
            || !is_within(application_root, working_directory)) {
            return std::nullopt;
        }
        error.clear();
        const auto project_status =
            std::filesystem::symlink_status(config.project_root, error);
        if (error || project_status.type() != std::filesystem::file_type::directory) {
            return std::nullopt;
        }
        const auto project_root = std::filesystem::canonical(
            config.project_root, error);
        if (error || project_root.empty()) return std::nullopt;
        return ValidatedConfig{
            executable,
            application_root,
            working_directory,
            project_root,
            std::move(config.host),
            config.port,
            std::move(config.runtime_repository_generation),
        };
    } catch (...) {
        return std::nullopt;
    }
}

#if defined(_WIN32)

class WindowsHandle final {
public:
    WindowsHandle() noexcept = default;
    explicit WindowsHandle(HANDLE value) noexcept : value_(value) {}
    ~WindowsHandle() { reset(); }

    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;

    WindowsHandle(WindowsHandle&& other) noexcept
        : value_(std::exchange(other.value_, nullptr))
    {}

    WindowsHandle& operator=(WindowsHandle&& other) noexcept
    {
        if (this != &other) reset(std::exchange(other.value_, nullptr));
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE value = nullptr) noexcept
    {
        if (*this) static_cast<void>(CloseHandle(value_));
        value_ = value;
    }

private:
    HANDLE value_ = nullptr;
};

[[nodiscard]] std::optional<std::wstring> utf8_to_wide(
    const std::string_view value) noexcept
{
    if (value.empty()) return std::wstring{};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    const auto size = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, nullptr, 0);
    if (required <= 0) return std::nullopt;
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size,
            result.data(), required) != required) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::wstring quote_windows_argument(const std::wstring_view value)
{
    std::wstring result;
    result.reserve(value.size() + 2);
    result.push_back(L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

[[nodiscard]] std::optional<std::wstring> windows_command_line(
    const ValidatedConfig& config) noexcept
{
    try {
        const auto host = utf8_to_wide(config.host);
        const auto port = utf8_to_wide(std::to_string(config.port));
        const auto generation = utf8_to_wide(config.generation);
        if (!host || !port || !generation) return std::nullopt;
        const std::array arguments{
            config.executable.native(),
            std::wstring{L"--project-root"},
            config.project_root.native(),
            std::wstring{L"--host"},
            *host,
            std::wstring{L"--port"},
            *port,
            std::wstring{L"--runtime-repository-generation"},
            *generation,
        };
        std::wstring result;
        for (const auto& argument : arguments) {
            if (!result.empty()) result.push_back(L' ');
            result += quote_windows_argument(argument);
        }
        if (result.size() >= 32'767) return std::nullopt;
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

#else

[[nodiscard]] std::vector<std::string> service_arguments(
    const ValidatedConfig& config)
{
    return {
        "--project-root",
        config.project_root.native(),
        "--host",
        config.host,
        "--port",
        std::to_string(config.port),
        "--runtime-repository-generation",
        config.generation,
    };
}

[[nodiscard]] int add_spawn_working_directory(
    posix_spawn_file_actions_t* actions,
    const std::filesystem::path& directory) noexcept
{
#if defined(__APPLE__) \
    && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) \
    && __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000
    // macOS 26 made the standardized spelling available. Calling it only for
    // a 26+ deployment target keeps binaries targeting older macOS releases
    // free of a new symbol dependency.
    return posix_spawn_file_actions_addchdir(actions, directory.c_str());
#elif defined(__APPLE__)
    // The _np spelling is the only deployment-compatible API before macOS 26.
    // SDK 26 deprecates it at compile time even when the deployment target is
    // older, so isolate the unavoidable compatibility call narrowly.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const int result =
        posix_spawn_file_actions_addchdir_np(actions, directory.c_str());
#pragma clang diagnostic pop
    return result;
#else
    return posix_spawn_file_actions_addchdir_np(actions, directory.c_str());
#endif
}

struct PosixEmergencyReaperState final {
    std::mutex mutex;
    std::condition_variable changed;
    bool shutdown = false;
    pid_t child = -1;
};

[[nodiscard]] std::shared_ptr<PosixEmergencyReaperState>
create_posix_emergency_reaper()
{
    auto state = std::make_shared<PosixEmergencyReaperState>();
    std::thread worker{[state] {
        pid_t child = -1;
        {
            std::unique_lock lock{state->mutex};
            state->changed.wait(lock, [&state] { return state->shutdown; });
            child = state->child;
        }
        if (child > 0) {
            // The handoff occurs before the leader is reaped, so this group
            // kill cannot target a reused PID/PGID.
            static_cast<void>(::kill(-child, SIGKILL));
            int status = 0;
            while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }};
    worker.detach();
    return state;
}

void shutdown_posix_emergency_reaper(
    const std::shared_ptr<PosixEmergencyReaperState>& state,
    const pid_t child = -1) noexcept
{
    {
        std::lock_guard lock{state->mutex};
        state->child = child;
        state->shutdown = true;
    }
    state->changed.notify_one();
}

#endif

}  // namespace

class ServiceProcessOwner::Impl final {
public:
    Impl()
#if !defined(_WIN32)
        : emergency_reaper_(create_posix_emergency_reaper())
#endif
    {}

    ~Impl()
    {
        if (stop(cleanup_limit) != ServiceProcessError::none) {
            emergency_release_noexcept();
#if !defined(_WIN32)
        } else {
            if (!emergency_reaper_handed_off_) {
                shutdown_posix_emergency_reaper(emergency_reaper_);
            }
#endif
        }
    }

    [[nodiscard]] ServiceProcessStartResult start(ServiceProcessConfig config)
    {
        auto validated = validate_config(std::move(config));
        if (!validated) {
            return {ServiceProcessError::invalid_configuration, 0};
        }
        std::uint64_t generation = 0;
        {
            std::lock_guard lock{state_mutex_};
            if (phase_ != Phase::stopped) {
                return {ServiceProcessError::already_active, 0};
            }
            phase_ = Phase::starting;
            generation = ++operation_generation_;
        }

        ServiceProcessStartResult result;
        {
            std::lock_guard operation{operation_mutex_};
            result = start_platform(*validated);
        }
        {
            std::lock_guard lock{state_mutex_};
            (void)generation;
            if (result) {
                phase_ = Phase::running;
                process_id_ = result.process_id;
            } else {
                phase_ = Phase::stopped;
                process_id_ = 0;
            }
            exit_.reset();
        }
        state_changed_.notify_all();
        return result;
    }

    [[nodiscard]] ServiceProcessWaitResult wait_for(
        const std::chrono::milliseconds timeout) noexcept
    {
        if (timeout < std::chrono::milliseconds::zero()) {
            return {
                ServiceProcessWaitDisposition::failed,
                ServiceProcessError::invalid_configuration,
                std::nullopt,
            };
        }
        const auto deadline = saturated_deadline(timeout);
        for (;;) {
            {
                std::unique_lock lock{state_mutex_};
                if (phase_ == Phase::stopped) {
                    return {
                        ServiceProcessWaitDisposition::not_running,
                        ServiceProcessError::none,
                        std::nullopt,
                    };
                }
                if (phase_ == Phase::exited) {
                    return {
                        ServiceProcessWaitDisposition::exited,
                        ServiceProcessError::none,
                        exit_,
                    };
                }
                if (phase_ == Phase::starting || phase_ == Phase::stopping) {
                    if (deadline_reached(deadline)) return timed_out_wait();
                    state_changed_.wait_until(lock, next_poll(deadline));
                    continue;
                }
            }

            const auto polled = poll_running_process();
            if (polled.has_value()) return *polled;
            if (deadline_reached(deadline)) return timed_out_wait();
            std::unique_lock lock{state_mutex_};
            state_changed_.wait_until(lock, next_poll(deadline));
        }
    }

    [[nodiscard]] ServiceProcessError stop(
        const std::chrono::milliseconds timeout) noexcept
    {
        if (timeout < std::chrono::milliseconds::zero()) {
            return ServiceProcessError::invalid_configuration;
        }
        const auto deadline = saturated_deadline(timeout);
        std::uint64_t generation = 0;
        for (;;) {
            std::unique_lock lock{state_mutex_};
            if (phase_ == Phase::stopped) return ServiceProcessError::none;
            if (phase_ == Phase::exited) {
                phase_ = Phase::stopped;
                exit_.reset();
                ++operation_generation_;
                lock.unlock();
                state_changed_.notify_all();
                return ServiceProcessError::none;
            }
            if (phase_ == Phase::starting || phase_ == Phase::stopping) {
                if (deadline_reached(deadline)) {
                    return ServiceProcessError::wait_failed;
                }
                state_changed_.wait_until(lock, next_poll(deadline));
                continue;
            }
            phase_ = Phase::stopping;
            generation = operation_generation_;
            break;
        }
        state_changed_.notify_all();

        ServiceProcessError result = ServiceProcessError::none;
        {
            std::lock_guard operation{operation_mutex_};
            result = terminate_and_reap_platform(deadline);
        }
        {
            std::lock_guard lock{state_mutex_};
            (void)generation;
            if (result == ServiceProcessError::none) {
                phase_ = Phase::stopped;
                process_id_ = 0;
                exit_.reset();
                ++operation_generation_;
            } else {
                phase_ = Phase::running;
            }
        }
        state_changed_.notify_all();
        return result;
    }

    [[nodiscard]] ServiceProcessState state() const noexcept
    {
        std::lock_guard lock{state_mutex_};
        if (phase_ == Phase::stopped) return ServiceProcessState::stopped;
        if (phase_ == Phase::exited) return ServiceProcessState::exited;
        return ServiceProcessState::running;
    }

    [[nodiscard]] std::uint64_t process_id() const noexcept
    {
        std::lock_guard lock{state_mutex_};
        return process_id_;
    }

#if defined(BAAS_SERVICE_PROCESS_OWNER_TEST_HOOKS)
    [[nodiscard]] bool emergency_reaper_ready_for_tests() const noexcept
    {
#if defined(_WIN32)
        return true;
#else
        std::lock_guard lock{emergency_reaper_->mutex};
        return !emergency_reaper_->shutdown;
#endif
    }
#endif

private:
    using Clock = std::chrono::steady_clock;
    enum class Phase : std::uint8_t { stopped, starting, running, stopping, exited };
    static constexpr auto cleanup_limit = std::chrono::seconds{5};
    static constexpr auto poll_interval = std::chrono::milliseconds{2};

    [[nodiscard]] static Clock::time_point saturated_deadline(
        const std::chrono::milliseconds timeout) noexcept
    {
        const auto now = Clock::now();
        const auto available = Clock::time_point::max() - now;
        const auto available_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(available);
        if (timeout >= available_ms) return Clock::time_point::max();
        return now + std::chrono::duration_cast<Clock::duration>(timeout);
    }

    [[nodiscard]] static bool deadline_reached(
        const Clock::time_point deadline) noexcept
    {
        return deadline != Clock::time_point::max() && Clock::now() >= deadline;
    }

    [[nodiscard]] static Clock::time_point next_poll(
        const Clock::time_point deadline) noexcept
    {
        const auto now = Clock::now();
        const auto available = Clock::time_point::max() - now;
        const auto candidate = available <= poll_interval
            ? Clock::time_point::max()
            : now + poll_interval;
        if (deadline == Clock::time_point::max()) return candidate;
        return std::min(deadline, candidate);
    }

    [[nodiscard]] static ServiceProcessWaitResult timed_out_wait() noexcept
    {
        return {
            ServiceProcessWaitDisposition::timed_out,
            ServiceProcessError::none,
            std::nullopt,
        };
    }

    [[nodiscard]] ServiceProcessStartResult start_platform(
        const ValidatedConfig& config)
    {
#if defined(_WIN32)
        auto command_line = windows_command_line(config);
        if (!command_line) {
            return {ServiceProcessError::invalid_configuration, 0};
        }
        WindowsHandle job{CreateJobObjectW(nullptr, nullptr)};
        if (!job) return {ServiceProcessError::launch_failed, 0};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                job.get(), JobObjectExtendedLimitInformation, &limits,
                sizeof(limits))) {
            return {ServiceProcessError::launch_failed, 0};
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> mutable_command(
            command_line->begin(), command_line->end());
        mutable_command.push_back(L'\0');
        std::array<wchar_t, 2> empty_environment{L'\0', L'\0'};
        const auto executable = config.executable.native();
        const auto working_directory = config.working_directory.native();
        if (!CreateProcessW(
                executable.c_str(), mutable_command.data(), nullptr, nullptr,
                FALSE,
                CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP
                    | CREATE_UNICODE_ENVIRONMENT,
                empty_environment.data(), working_directory.c_str(),
                &startup, &process)) {
            return {ServiceProcessError::launch_failed, 0};
        }
        WindowsHandle process_handle{process.hProcess};
        WindowsHandle thread_handle{process.hThread};
        const bool assigned =
            AssignProcessToJobObject(job.get(), process_handle.get()) != FALSE;
        if (!assigned
            || ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
            if (assigned) {
                static_cast<void>(TerminateJobObject(job.get(), 127));
                job.reset();
            } else {
                static_cast<void>(TerminateProcess(process_handle.get(), 127));
            }
            if (WaitForSingleObject(
                    process_handle.get(),
                    static_cast<DWORD>(cleanup_limit.count() * 1000))
                != WAIT_OBJECT_0) {
                job.reset();
            }
            return {ServiceProcessError::launch_failed, 0};
        }
        const auto identifier = static_cast<std::uint64_t>(process.dwProcessId);
        process_ = std::move(process_handle);
        job_ = std::move(job);
        observed_windows_exit_.reset();
        return {ServiceProcessError::none, identifier};
#else
        if (emergency_reaper_handed_off_) {
            return {ServiceProcessError::launch_failed, 0};
        }
        std::vector<std::string> storage;
        storage.push_back(config.executable.native());
        auto arguments = service_arguments(config);
        storage.insert(
            storage.end(), std::make_move_iterator(arguments.begin()),
            std::make_move_iterator(arguments.end()));
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (auto& argument : storage) argv.push_back(argument.data());
        argv.push_back(nullptr);
        std::array<char*, 1> empty_environment{nullptr};

        posix_spawn_file_actions_t actions{};
        posix_spawnattr_t attributes{};
        if (posix_spawn_file_actions_init(&actions) != 0) {
            return {ServiceProcessError::launch_failed, 0};
        }
        const auto destroy_actions = [&actions] {
            static_cast<void>(posix_spawn_file_actions_destroy(&actions));
        };
#if defined(__linux__)
        if (posix_spawn_file_actions_addclosefrom_np(&actions, 3) != 0) {
            destroy_actions();
            return {ServiceProcessError::launch_failed, 0};
        }
#endif
        if (add_spawn_working_directory(
                &actions, config.working_directory) != 0) {
            destroy_actions();
            return {ServiceProcessError::launch_failed, 0};
        }
        if (posix_spawnattr_init(&attributes) != 0) {
            destroy_actions();
            return {ServiceProcessError::launch_failed, 0};
        }
        short flags = POSIX_SPAWN_SETPGROUP;
#if defined(__APPLE__)
        flags = static_cast<short>(
            flags | POSIX_SPAWN_CLOEXEC_DEFAULT
            | POSIX_SPAWN_START_SUSPENDED);
#endif
        if (posix_spawnattr_setflags(&attributes, flags) != 0
            || posix_spawnattr_setpgroup(&attributes, 0) != 0) {
            static_cast<void>(posix_spawnattr_destroy(&attributes));
            destroy_actions();
            return {ServiceProcessError::launch_failed, 0};
        }
        pid_t child = -1;
#if defined(__APPLE__)
        const int exit_observer = ::kqueue();
        if (exit_observer < 0
            || ::fcntl(exit_observer, F_SETFD, FD_CLOEXEC) != 0) {
            if (exit_observer >= 0) static_cast<void>(::close(exit_observer));
            static_cast<void>(posix_spawnattr_destroy(&attributes));
            destroy_actions();
            return {ServiceProcessError::launch_failed, 0};
        }
#endif
        const int spawned = posix_spawn(
            &child, config.executable.c_str(), &actions, &attributes,
            argv.data(), empty_environment.data());
        static_cast<void>(posix_spawnattr_destroy(&attributes));
        destroy_actions();
        if (spawned != 0 || child <= 0) {
#if defined(__APPLE__)
            static_cast<void>(::close(exit_observer));
#endif
            return {ServiceProcessError::launch_failed, 0};
        }
#if defined(__APPLE__)
        struct kevent change {};
        EV_SET(
            &change, static_cast<uintptr_t>(child), EVFILT_PROC,
            EV_ADD | EV_ENABLE, NOTE_EXIT | NOTE_EXITSTATUS, 0, nullptr);
        if (::kevent(exit_observer, &change, 1, nullptr, 0, nullptr) != 0) {
            cleanup_unobserved_child(child, exit_observer);
            return {ServiceProcessError::launch_failed, 0};
        }
        // POSIX_SPAWN_START_SUSPENDED closes the fast-exit registration race:
        // NOTE_EXIT is armed before the selected image can execute.
        if (::kill(child, SIGCONT) != 0) {
            cleanup_unobserved_child(child, exit_observer);
            return {ServiceProcessError::launch_failed, 0};
        }
        exit_observer_ = exit_observer;
        mac_exit_observed_ = false;
        mac_exit_status_.reset();
#endif
        process_ = child;
        return {
            ServiceProcessError::none,
            static_cast<std::uint64_t>(child),
        };
#endif
    }

#if defined(__APPLE__)
    void cleanup_unobserved_child(
        const pid_t child,
        const int exit_observer) noexcept
    {
        static_cast<void>(::close(exit_observer));
        static_cast<void>(::kill(-child, SIGKILL));
        const auto deadline = saturated_deadline(cleanup_limit);
        for (;;) {
            int status = 0;
            pid_t waited = -1;
            do {
                waited = ::waitpid(child, &status, WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited == child || (waited < 0 && errno == ECHILD)) return;
            if (waited < 0 || deadline_reached(deadline)) break;
            std::this_thread::sleep_until(next_poll(deadline));
        }
        // Registration failed, but the pre-created reaper can still own the
        // killed direct child without any PID reuse window or destructor-time
        // allocation. This owner is poisoned against later starts because its
        // sole emergency reaper has been consumed.
        shutdown_posix_emergency_reaper(emergency_reaper_, child);
        emergency_reaper_handed_off_ = true;
    }
#endif

    [[nodiscard]] std::optional<ServiceProcessWaitResult>
    poll_running_process() noexcept
    {
        std::scoped_lock lock{operation_mutex_, state_mutex_};
        if (phase_ != Phase::running) return std::nullopt;
        ServiceProcessExit process_exit{};
        const auto result = poll_and_reap_platform(process_exit);
        if (result == PollResult::pending) return std::nullopt;
        if (result == PollResult::failed) {
            return ServiceProcessWaitResult{
                ServiceProcessWaitDisposition::failed,
                ServiceProcessError::wait_failed,
                std::nullopt,
            };
        }
        phase_ = Phase::exited;
        process_id_ = 0;
        exit_ = process_exit;
        state_changed_.notify_all();
        return ServiceProcessWaitResult{
            ServiceProcessWaitDisposition::exited,
            ServiceProcessError::none,
            exit_,
        };
    }

    enum class PollResult : std::uint8_t { pending, exited, failed };

    [[nodiscard]] PollResult poll_and_reap_platform(
        ServiceProcessExit& process_exit) noexcept
    {
#if defined(_WIN32)
        if (!process_ || !job_) return PollResult::failed;
        if (!observed_windows_exit_) {
            const auto wait = WaitForSingleObject(process_.get(), 0);
            if (wait == WAIT_TIMEOUT) return PollResult::pending;
            if (wait != WAIT_OBJECT_0) return PollResult::failed;
            DWORD code = 0;
            if (!GetExitCodeProcess(process_.get(), &code)) {
                return PollResult::failed;
            }
            observed_windows_exit_ = static_cast<int>(code);
        }
        if (TerminateJobObject(job_.get(), 137) == FALSE && !job_is_empty()) {
            return PollResult::failed;
        }
        if (!job_is_empty()) return PollResult::pending;
        process_exit = ServiceProcessExit{*observed_windows_exit_, false, 0};
        process_.reset();
        job_.reset();
        observed_windows_exit_.reset();
        return PollResult::exited;
#else
        if (process_ <= 0) return PollResult::failed;
#if defined(__APPLE__)
        if (exit_observer_ < 0) return PollResult::failed;
        if (!mac_exit_observed_) {
            struct kevent event {};
            const struct timespec no_wait {};
            int observed = -1;
            do {
                observed = ::kevent(
                    exit_observer_, nullptr, 0, &event, 1, &no_wait);
            } while (observed < 0 && errno == EINTR);
            if (observed < 0) return PollResult::failed;
            if (observed == 0) return PollResult::pending;
            // This queue owns exactly one EVFILT_PROC registration and that
            // registration requests only terminal state.  Darwin may report the
            // terminal state primarily through EV_EOF, so matching the queue,
            // filter, and process identity is the authoritative boundary;
            // requiring the output fflags to echo NOTE_EXIT can reject a
            // genuine terminal event and strand the zombie.
            if ((event.flags & EV_ERROR) != 0
                || event.filter != EVFILT_PROC
                || static_cast<pid_t>(event.ident) != process_) {
                return PollResult::failed;
            }
            if ((event.fflags & NOTE_EXITSTATUS) == 0) {
                return PollResult::failed;
            }
            mac_exit_observed_ = true;
            mac_exit_status_ = static_cast<int>(event.data);
        }
#else
        siginfo_t information{};
        int observed = 0;
        do {
            observed = ::waitid(
                P_PID, static_cast<id_t>(process_), &information,
                WEXITED | WNOHANG | WNOWAIT);
        } while (observed < 0 && errno == EINTR);
        if (observed < 0) return PollResult::failed;
        if (information.si_pid == 0) return PollResult::pending;
#endif

        // The leader remains a zombie here, so its PID/PGID cannot be reused.
        // Kill descendants before the sole waitpid that releases that identity.
        if (::kill(-process_, SIGKILL) != 0 && errno != ESRCH) {
            return PollResult::failed;
        }
        int status = 0;
        pid_t waited = -1;
        do {
            waited = ::waitpid(process_, &status, WNOHANG);
        } while (waited < 0 && errno == EINTR);
        if (waited == 0) return PollResult::pending;
#if defined(__APPLE__)
        if (waited < 0 && errno == ECHILD && mac_exit_status_) {
            // Darwin may finish reaping before the NOTE_EXIT consumer runs.
            // NOTE_EXITSTATUS carries the same p_xstat value returned by
            // waitpid, so the owned kqueue event still preserves the exact
            // terminal result without accepting another process identity.
            status = *mac_exit_status_;
        } else
#endif
        if (waited != process_) return PollResult::failed;
        if (WIFEXITED(status)) {
            process_exit = ServiceProcessExit{WEXITSTATUS(status), false, 0};
        } else if (WIFSIGNALED(status)) {
            process_exit = ServiceProcessExit{
                128 + WTERMSIG(status), true, WTERMSIG(status)};
        } else {
            return PollResult::failed;
        }
        process_ = -1;
#if defined(__APPLE__)
        static_cast<void>(::close(exit_observer_));
        exit_observer_ = -1;
        mac_exit_observed_ = false;
        mac_exit_status_.reset();
#endif
        return PollResult::exited;
#endif
    }

    [[nodiscard]] ServiceProcessError terminate_and_reap_platform(
        const Clock::time_point deadline) noexcept
    {
#if defined(_WIN32)
        if (!process_ || !job_) return ServiceProcessError::terminate_failed;
        if (TerminateJobObject(job_.get(), 137) == FALSE && !job_is_empty()) {
            return ServiceProcessError::terminate_failed;
        }
#else
        if (process_ <= 0) return ServiceProcessError::terminate_failed;
        if (::kill(-process_, SIGKILL) != 0 && errno != ESRCH) {
            return ServiceProcessError::terminate_failed;
        }
#endif
#if defined(__APPLE__)
        // Group termination was issued while the direct child identity was
        // still owned, so stop no longer needs the asynchronous NOTE_EXIT path.
        // Reap directly and accept ECHILD only as "already reaped"; stop does
        // not publish a natural exit status.
        for (;;) {
            int status = 0;
            pid_t waited = -1;
            do {
                waited = ::waitpid(process_, &status, WNOHANG);
            } while (waited < 0 && errno == EINTR);
            if (waited == process_ || (waited < 0 && errno == ECHILD)) {
                process_ = -1;
                static_cast<void>(::close(exit_observer_));
                exit_observer_ = -1;
                mac_exit_observed_ = false;
                mac_exit_status_.reset();
                return ServiceProcessError::none;
            }
            if (waited < 0 || deadline_reached(deadline)) {
                return ServiceProcessError::wait_failed;
            }
            std::this_thread::sleep_until(next_poll(deadline));
        }
#else
        for (;;) {
            ServiceProcessExit ignored{};
            const auto result = poll_and_reap_platform(ignored);
            if (result == PollResult::exited) return ServiceProcessError::none;
            if (result == PollResult::failed) {
                return ServiceProcessError::wait_failed;
            }
            if (deadline_reached(deadline)) {
                return ServiceProcessError::wait_failed;
            }
            std::this_thread::sleep_until(next_poll(deadline));
        }
#endif
    }

    void emergency_release_noexcept() noexcept
    {
        std::lock_guard operation{operation_mutex_};
#if defined(_WIN32)
        // Closing the last kill-on-close Job handle is the fail-closed tree
        // boundary. The finite wait only confirms the leader; Windows has no
        // zombie resource after both handles are closed.
        job_.reset();
        if (process_) {
            static_cast<void>(WaitForSingleObject(
                process_.get(),
                static_cast<DWORD>(cleanup_limit.count() * 1000)));
        }
        process_.reset();
        observed_windows_exit_.reset();
#else
        if (process_ > 0) {
            const pid_t child = std::exchange(process_, -1);
#if defined(__APPLE__)
            if (exit_observer_ >= 0) {
                static_cast<void>(::close(exit_observer_));
                exit_observer_ = -1;
                mac_exit_observed_ = false;
                mac_exit_status_.reset();
            }
#endif
            static_cast<void>(::kill(-child, SIGKILL));
            // This worker was created before start could launch any child, so
            // emergency cleanup performs no allocation or thread creation.
            shutdown_posix_emergency_reaper(emergency_reaper_, child);
            emergency_reaper_handed_off_ = true;
        } else {
            if (!emergency_reaper_handed_off_) {
                shutdown_posix_emergency_reaper(emergency_reaper_);
            }
        }
#endif
        std::lock_guard state{state_mutex_};
        phase_ = Phase::stopped;
        process_id_ = 0;
        exit_.reset();
    }

#if defined(_WIN32)
    [[nodiscard]] bool job_is_empty() const noexcept
    {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        return job_
            && QueryInformationJobObject(
                   job_.get(), JobObjectBasicAccountingInformation,
                   &accounting, sizeof(accounting), nullptr)
                != FALSE
            && accounting.ActiveProcesses == 0;
    }
#endif

    mutable std::mutex state_mutex_;
    std::condition_variable state_changed_;
    std::mutex operation_mutex_;
    Phase phase_ = Phase::stopped;
    std::uint64_t operation_generation_ = 0;
    std::uint64_t process_id_ = 0;
    std::optional<ServiceProcessExit> exit_;
#if defined(_WIN32)
    WindowsHandle process_;
    WindowsHandle job_;
    std::optional<int> observed_windows_exit_;
#else
    pid_t process_ = -1;
#if defined(__APPLE__)
    int exit_observer_ = -1;
    bool mac_exit_observed_ = false;
    std::optional<int> mac_exit_status_;
#endif
    std::shared_ptr<PosixEmergencyReaperState> emergency_reaper_;
    bool emergency_reaper_handed_off_ = false;
#endif
};

std::string_view service_process_error_name(
    const ServiceProcessError error) noexcept
{
    using enum ServiceProcessError;
    switch (error) {
        case none: return "none";
        case invalid_configuration: return "invalid_configuration";
        case already_active: return "already_active";
        case launch_failed: return "launch_failed";
        case wait_failed: return "wait_failed";
        case terminate_failed: return "terminate_failed";
    }
    return "unknown";
}

ServiceProcessOwner::ServiceProcessOwner()
    : impl_(std::make_unique<Impl>())
{}

ServiceProcessOwner::~ServiceProcessOwner() = default;

ServiceProcessStartResult ServiceProcessOwner::start(ServiceProcessConfig config)
{
    return impl_->start(std::move(config));
}

ServiceProcessWaitResult ServiceProcessOwner::wait_for(
    const std::chrono::milliseconds timeout) noexcept
{
    return impl_->wait_for(timeout);
}

ServiceProcessError ServiceProcessOwner::stop(
    const std::chrono::milliseconds timeout) noexcept
{
    return impl_->stop(timeout);
}

ServiceProcessState ServiceProcessOwner::state() const noexcept
{
    return impl_->state();
}

std::uint64_t ServiceProcessOwner::process_id() const noexcept
{
    return impl_->process_id();
}

#if defined(BAAS_SERVICE_PROCESS_OWNER_TEST_HOOKS)
bool ServiceProcessOwner::emergency_reaper_ready_for_tests() const noexcept
{
    return impl_->emergency_reaper_ready_for_tests();
}
#endif

}  // namespace baas::service::supervisor
