#include "service/supervisor/ServiceProcessOwner.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
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
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace baas::service::supervisor {
namespace {

struct ValidatedConfig {
    std::filesystem::path executable;
    std::filesystem::path project_root;
    std::string host;
    std::uint16_t port = 0;
    std::string generation;
};

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

#endif

}  // namespace

class ServiceProcessOwner::Impl final {
public:
    ~Impl() { destroy_noexcept(); }

    [[nodiscard]] ServiceProcessStartResult start(ServiceProcessConfig config)
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (state_ != ServiceProcessState::stopped) {
            return {ServiceProcessError::already_active, 0};
        }
        auto validated = validate_config(std::move(config));
        if (!validated) {
            return {ServiceProcessError::invalid_configuration, 0};
        }
        return start_locked(*validated);
    }

    [[nodiscard]] ServiceProcessWaitResult wait_for(
        const std::chrono::milliseconds timeout) noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (timeout < std::chrono::milliseconds::zero()) {
            return {
                ServiceProcessWaitDisposition::failed,
                ServiceProcessError::invalid_configuration,
                std::nullopt,
            };
        }
        if (state_ == ServiceProcessState::stopped) {
            return {
                ServiceProcessWaitDisposition::not_running,
                ServiceProcessError::none,
                std::nullopt,
            };
        }
        if (state_ == ServiceProcessState::exited) {
            return {
                ServiceProcessWaitDisposition::exited,
                ServiceProcessError::none,
                exit_,
            };
        }
        return wait_running_locked(timeout);
    }

    [[nodiscard]] ServiceProcessError stop(
        const std::chrono::milliseconds timeout) noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (timeout < std::chrono::milliseconds::zero()) {
            return ServiceProcessError::invalid_configuration;
        }
        if (state_ == ServiceProcessState::stopped) {
            return ServiceProcessError::none;
        }
        if (state_ == ServiceProcessState::exited) {
            state_ = ServiceProcessState::stopped;
            exit_.reset();
            return ServiceProcessError::none;
        }
        if (!terminate_locked()) return ServiceProcessError::terminate_failed;
        const auto waited = wait_running_locked(timeout);
        if (waited.disposition != ServiceProcessWaitDisposition::exited) {
            return ServiceProcessError::wait_failed;
        }
        state_ = ServiceProcessState::stopped;
        exit_.reset();
        return ServiceProcessError::none;
    }

    [[nodiscard]] ServiceProcessState state() const noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return state_;
    }

    [[nodiscard]] std::uint64_t process_id() const noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return process_id_;
    }

private:
    [[nodiscard]] ServiceProcessStartResult start_locked(
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
        const auto executable = config.executable.native();
        const auto project_root = config.project_root.native();
        if (!CreateProcessW(
                executable.c_str(), mutable_command.data(), nullptr, nullptr,
                FALSE,
                CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP
                    | CREATE_UNICODE_ENVIRONMENT,
                nullptr, project_root.c_str(), &startup, &process)) {
            return {ServiceProcessError::launch_failed, 0};
        }
        WindowsHandle process_handle{process.hProcess};
        WindowsHandle thread_handle{process.hThread};
        if (!AssignProcessToJobObject(job.get(), process_handle.get())
            || ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
            static_cast<void>(TerminateProcess(process_handle.get(), 127));
            static_cast<void>(WaitForSingleObject(process_handle.get(), INFINITE));
            return {ServiceProcessError::launch_failed, 0};
        }
        const auto identifier = static_cast<std::uint64_t>(process.dwProcessId);
        process_ = std::move(process_handle);
        job_ = std::move(job);
        process_id_ = identifier;
#else
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
        if (posix_spawnattr_init(&attributes) != 0) {
            destroy_actions();
            return {ServiceProcessError::launch_failed, 0};
        }
        short flags = POSIX_SPAWN_SETPGROUP;
#if defined(__APPLE__)
        flags = static_cast<short>(flags | POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
        if (posix_spawnattr_setflags(&attributes, flags) != 0
            || posix_spawnattr_setpgroup(&attributes, 0) != 0) {
            static_cast<void>(posix_spawnattr_destroy(&attributes));
            destroy_actions();
            return {ServiceProcessError::launch_failed, 0};
        }
        pid_t child = -1;
        const int spawned = posix_spawn(
            &child, config.executable.c_str(), &actions, &attributes,
            argv.data(), environ);
        static_cast<void>(posix_spawnattr_destroy(&attributes));
        destroy_actions();
        if (spawned != 0 || child <= 0) {
            return {ServiceProcessError::launch_failed, 0};
        }
        process_id_ = static_cast<std::uint64_t>(child);
        process_ = child;
#endif
        state_ = ServiceProcessState::running;
        exit_.reset();
        return {ServiceProcessError::none, process_id_};
    }

    [[nodiscard]] ServiceProcessWaitResult wait_running_locked(
        const std::chrono::milliseconds timeout) noexcept
    {
#if defined(_WIN32)
        const auto bounded = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(timeout.count()),
            static_cast<std::uint64_t>(INFINITE - 1));
        const auto wait = WaitForSingleObject(
            process_.get(), static_cast<DWORD>(bounded));
        if (wait == WAIT_TIMEOUT) {
            return {
                ServiceProcessWaitDisposition::timed_out,
                ServiceProcessError::none,
                std::nullopt,
            };
        }
        if (wait != WAIT_OBJECT_0) {
            return {
                ServiceProcessWaitDisposition::failed,
                ServiceProcessError::wait_failed,
                std::nullopt,
            };
        }
        DWORD code = 0;
        if (!GetExitCodeProcess(process_.get(), &code)) {
            return {
                ServiceProcessWaitDisposition::failed,
                ServiceProcessError::wait_failed,
                std::nullopt,
            };
        }
        exit_ = ServiceProcessExit{static_cast<int>(code), false, 0};
        process_.reset();
        job_.reset();
#else
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        int status = 0;
        for (;;) {
            const auto waited = ::waitpid(process_, &status, WNOHANG);
            if (waited == process_) break;
            if (waited < 0) {
                if (errno == EINTR) continue;
                return {
                    ServiceProcessWaitDisposition::failed,
                    ServiceProcessError::wait_failed,
                    std::nullopt,
                };
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return {
                    ServiceProcessWaitDisposition::timed_out,
                    ServiceProcessError::none,
                    std::nullopt,
                };
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        static_cast<void>(::kill(-process_, SIGKILL));
        if (WIFEXITED(status)) {
            exit_ = ServiceProcessExit{WEXITSTATUS(status), false, 0};
        } else if (WIFSIGNALED(status)) {
            exit_ = ServiceProcessExit{128 + WTERMSIG(status), true, WTERMSIG(status)};
        } else {
            return {
                ServiceProcessWaitDisposition::failed,
                ServiceProcessError::wait_failed,
                std::nullopt,
            };
        }
        process_ = -1;
#endif
        process_id_ = 0;
        state_ = ServiceProcessState::exited;
        return {
            ServiceProcessWaitDisposition::exited,
            ServiceProcessError::none,
            exit_,
        };
    }

    [[nodiscard]] bool terminate_locked() noexcept
    {
#if defined(_WIN32)
        if (!job_) return false;
        if (TerminateJobObject(job_.get(), 137) != FALSE) return true;
        return process_ && TerminateProcess(process_.get(), 137) != FALSE;
#else
        if (process_ <= 0) return false;
        if (::kill(-process_, SIGKILL) == 0) return true;
        return errno == ESRCH;
#endif
    }

    void destroy_noexcept() noexcept
    {
        std::lock_guard<std::mutex> lock{mutex_};
        if (state_ == ServiceProcessState::running) {
            static_cast<void>(terminate_locked());
#if defined(_WIN32)
            if (process_) {
                static_cast<void>(WaitForSingleObject(process_.get(), INFINITE));
            }
            process_.reset();
            job_.reset();
#else
            if (process_ > 0) {
                int status = 0;
                while (::waitpid(process_, &status, 0) < 0 && errno == EINTR) {
                }
                static_cast<void>(::kill(-process_, SIGKILL));
            }
            process_ = -1;
#endif
        }
        process_id_ = 0;
        state_ = ServiceProcessState::stopped;
        exit_.reset();
    }

    mutable std::mutex mutex_;
    ServiceProcessState state_ = ServiceProcessState::stopped;
    std::uint64_t process_id_ = 0;
    std::optional<ServiceProcessExit> exit_;
#if defined(_WIN32)
    WindowsHandle process_;
    WindowsHandle job_;
#else
    pid_t process_ = -1;
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

}  // namespace baas::service::supervisor
