#include "service/pipe/PipeHost.h"

#include <atomic>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#endif

namespace baas::service::pipe {
namespace {

#if defined(_WIN32)

class ScopedWindowsHandle final {
public:
    explicit ScopedWindowsHandle(const HANDLE handle) : handle_(handle) {}
    ~ScopedWindowsHandle()
    {
        if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr)
            CloseHandle(handle_);
    }
    ScopedWindowsHandle(const ScopedWindowsHandle&) = delete;
    ScopedWindowsHandle& operator=(const ScopedWindowsHandle&) = delete;
private:
    HANDLE handle_{};
};

class WindowsPipeStream final : public PipeStream {
public:
    explicit WindowsPipeStream(const HANDLE handle) : handle_(handle) {}
    ~WindowsPipeStream() override
    {
        close();
        const auto handle = handle_.exchange(INVALID_HANDLE_VALUE);
        if (handle != INVALID_HANDLE_VALUE) {
            DisconnectNamedPipe(handle);
            CloseHandle(handle);
        }
    }

    PipeIoResult read(
        const std::span<std::byte> output,
        const std::chrono::milliseconds timeout) override
    {
        const auto handle = handle_.load();
        if (handle == INVALID_HANDLE_VALUE || closing_.load()) return {0, true, false, false};
        const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) return {0, false, true, false};
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        DWORD bytes{};
        bool success = ReadFile(
            handle, output.data(), static_cast<DWORD>(output.size()), &bytes, &overlapped) != FALSE;
        if (!success && GetLastError() == ERROR_IO_PENDING) {
            const auto wait = WaitForSingleObject(event, static_cast<DWORD>(timeout.count()));
            if (wait == WAIT_TIMEOUT) {
                CancelIoEx(handle, &overlapped);
                WaitForSingleObject(event, INFINITE);
                CloseHandle(event);
                return {0, false, false, true};
            }
            success = wait == WAIT_OBJECT_0
                && GetOverlappedResult(handle, &overlapped, &bytes, FALSE) != FALSE;
        }
        const auto operation_error = success ? ERROR_SUCCESS : GetLastError();
        CloseHandle(event);
        if (!success) {
            if (operation_error == ERROR_BROKEN_PIPE
                || operation_error == ERROR_PIPE_NOT_CONNECTED
                || operation_error == ERROR_OPERATION_ABORTED)
                return {0, true, false, false};
            return {0, false, true, false};
        }
        return {bytes, bytes == 0, false, false};
    }

    PipeIoResult write_all(
        const std::span<const std::byte> input,
        const std::chrono::milliseconds timeout) override
    {
        const auto handle = handle_.load();
        if (handle == INVALID_HANDLE_VALUE || closing_.load()) return {0, false, true, false};
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::size_t total{};
        while (total < input.size()) {
            const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event) return {total, false, true, false};
            OVERLAPPED overlapped{};
            overlapped.hEvent = event;
            DWORD bytes{};
            const auto remaining = std::min<std::size_t>(
                input.size() - total, std::numeric_limits<DWORD>::max());
            bool success = WriteFile(handle, input.data() + total,
                static_cast<DWORD>(remaining), &bytes, &overlapped) != FALSE;
            if (!success && GetLastError() == ERROR_IO_PENDING) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    CancelIoEx(handle, &overlapped);
                    WaitForSingleObject(event, INFINITE);
                    CloseHandle(event);
                    return {total, false, false, true};
                }
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now);
                const auto wait = WaitForSingleObject(
                    event, static_cast<DWORD>(std::max<std::int64_t>(1, left.count())));
                if (wait == WAIT_TIMEOUT) {
                    CancelIoEx(handle, &overlapped);
                    WaitForSingleObject(event, INFINITE);
                    CloseHandle(event);
                    return {total, false, false, true};
                }
                success = wait == WAIT_OBJECT_0
                    && GetOverlappedResult(handle, &overlapped, &bytes, FALSE) != FALSE;
            }
            CloseHandle(event);
            if (!success || bytes == 0) return {total, false, true, false};
            total += bytes;
        }
        return {total, false, false, false};
    }

    void close() noexcept override
    {
        if (closing_.exchange(true)) return;
        const auto handle = handle_.load();
        if (handle == INVALID_HANDLE_VALUE) return;
        CancelIoEx(handle, nullptr);
    }

private:
    std::atomic<HANDLE> handle_{INVALID_HANDLE_VALUE};
    std::atomic_bool closing_{};
};

class WindowsPipeListener final : public PipeListener {
public:
    explicit WindowsPipeListener(std::wstring endpoint)
        : endpoint_(std::move(endpoint))
    {
        HANDLE token{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return;
        const ScopedWindowsHandle token_guard{token};
        DWORD size{};
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        token_info_.resize(size);
        if (!GetTokenInformation(token, TokenUser, token_info_.data(), size, &size)) return;

        auto* user = reinterpret_cast<TOKEN_USER*>(token_info_.data());
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = static_cast<LPWSTR>(user->User.Sid);
        if (SetEntriesInAclW(1, &access, nullptr, &acl_) != ERROR_SUCCESS) return;
        if (!InitializeSecurityDescriptor(&descriptor_, SECURITY_DESCRIPTOR_REVISION)
            || !SetSecurityDescriptorDacl(&descriptor_, TRUE, acl_, FALSE)
            || !SetSecurityDescriptorControl(
                &descriptor_, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) return;
        valid_ = true;
    }

    ~WindowsPipeListener() override
    {
        close();
        if (acl_) LocalFree(acl_);
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }

    std::unique_ptr<PipeStream> accept() override
    {
        if (stopped_.load()) return {};
        SECURITY_ATTRIBUTES attributes{
            sizeof(SECURITY_ATTRIBUTES), &descriptor_, FALSE};
        auto open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (first_instance_) open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        const auto handle = CreateNamedPipeW(
            endpoint_.c_str(), open_mode,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, 64U * 1'024U, 64U * 1'024U, 0, &attributes);
        if (handle == INVALID_HANDLE_VALUE) return {};
        first_instance_ = false;
        const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event) {
            CloseHandle(handle);
            return {};
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = event;
        bool connected{};
        DWORD connect_error{ERROR_SUCCESS};
        {
            std::lock_guard lock(mutex_);
            if (stopped_.load()) {
                CloseHandle(event);
                CloseHandle(handle);
                return {};
            }
            pending_ = handle;
            pending_overlapped_ = &overlapped;
            connected = ConnectNamedPipe(handle, &overlapped) != FALSE;
            if (!connected) connect_error = GetLastError();
        }
        if (!connected) {
            if (connect_error == ERROR_IO_PENDING) {
                const auto wait = WaitForSingleObject(event, INFINITE);
                DWORD ignored{};
                connected = wait == WAIT_OBJECT_0
                    && GetOverlappedResult(handle, &overlapped, &ignored, FALSE) != FALSE;
            } else {
                connected = connect_error == ERROR_PIPE_CONNECTED;
            }
        }
        bool still_owned{};
        {
            std::lock_guard lock(mutex_);
            still_owned = pending_ == handle;
            if (still_owned) {
                pending_ = INVALID_HANDLE_VALUE;
                pending_overlapped_ = nullptr;
            }
        }
        CloseHandle(event);
        if (!still_owned) return {};
        if (!connected || stopped_.load()) {
            CloseHandle(handle);
            return {};
        }
        try {
            return std::make_unique<WindowsPipeStream>(handle);
        } catch (...) {
            DisconnectNamedPipe(handle);
            CloseHandle(handle);
            throw;
        }
    }

    void close() noexcept override
    {
        if (stopped_.exchange(true)) return;
        {
            std::lock_guard lock(mutex_);
            if (pending_ != INVALID_HANDLE_VALUE)
                CancelIoEx(pending_, pending_overlapped_);
        }
    }

private:
    std::wstring endpoint_;
    std::vector<std::byte> token_info_;
    ACL* acl_{};
    SECURITY_DESCRIPTOR descriptor_{};
    bool valid_{};
    std::atomic_bool stopped_{};
    std::mutex mutex_;
    HANDLE pending_{INVALID_HANDLE_VALUE};
    OVERLAPPED* pending_overlapped_{};
    bool first_instance_{true};
};

[[nodiscard]] std::optional<std::wstring> utf8_to_wide(const std::string_view input)
{
    if (input.empty()) return std::nullopt;
    const auto count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
        nullptr, 0);
    if (count <= 0) return std::nullopt;
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
            output.data(), count) != count) return std::nullopt;
    return output;
}

#elif defined(__unix__) || defined(__APPLE__)

[[nodiscard]] bool set_fd_flags(const int fd, const bool nonblocking) noexcept
{
    const auto descriptor_flags = fcntl(fd, F_GETFD, 0);
    if (descriptor_flags < 0
        || fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) return false;
    if (!nonblocking) return true;
    const auto status_flags = fcntl(fd, F_GETFL, 0);
    return status_flags >= 0
        && fcntl(fd, F_SETFL, status_flags | O_NONBLOCK) == 0;
}

class UnixPipeStream final : public PipeStream {
public:
    explicit UnixPipeStream(const int fd) : fd_(fd)
    {
#if defined(__APPLE__)
        const int enabled = 1;
        valid_ = setsockopt(
            fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#endif
    }
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    ~UnixPipeStream() override
    {
        close();
        const auto fd = fd_.exchange(-1);
        if (fd >= 0) ::close(fd);
    }

    PipeIoResult read(
        const std::span<std::byte> output,
        const std::chrono::milliseconds timeout) override
    {
        const auto fd = fd_.load();
        if (fd < 0 || closing_.load()) return {0, true, false, false};
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            pollfd event{fd, POLLIN, 0};
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return {0, false, false, true};
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            const auto ready = ::poll(
                &event, 1, static_cast<int>(std::max<std::int64_t>(1, left.count())));
            if (ready < 0 && errno == EINTR) continue;
            if (ready == 0) return {0, false, false, true};
            if (ready < 0) return {0, false, true, false};
            const auto result = ::recv(fd, output.data(), output.size(), 0);
            if (result == 0) return {0, true, false, false};
            if (result < 0 && errno == EINTR) continue;
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (result < 0 && (errno == EBADF || errno == ECONNRESET))
                return {0, true, false, false};
            if (result < 0) return {0, false, true, false};
            return {static_cast<std::size_t>(result), false, false, false};
        }
    }

    PipeIoResult write_all(
        const std::span<const std::byte> input,
        const std::chrono::milliseconds timeout) override
    {
        const auto fd = fd_.load();
        if (fd < 0 || closing_.load()) return {0, false, true, false};
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::size_t total{};
        while (total < input.size()) {
            pollfd event{fd, POLLOUT, 0};
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return {total, false, false, true};
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            const auto ready = ::poll(
                &event, 1, static_cast<int>(std::max<std::int64_t>(1, left.count())));
            if (ready < 0 && errno == EINTR) continue;
            if (ready == 0) return {total, false, false, true};
            if (ready < 0) return {total, false, true, false};
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto result = ::send(fd, input.data() + total, input.size() - total, flags);
            if (result < 0 && errno == EINTR) continue;
            if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
            if (result <= 0) return {total, false, true, false};
            total += static_cast<std::size_t>(result);
        }
        return {total, false, false, false};
    }

    void close() noexcept override
    {
        if (closing_.exchange(true)) return;
        const auto fd = fd_.load();
        if (fd < 0) return;
        ::shutdown(fd, SHUT_RDWR);
    }

private:
    std::atomic_int fd_{-1};
    std::atomic_bool closing_{};
    bool valid_{true};
};

[[nodiscard]] bool current_user_peer(const int fd) noexcept
{
#if defined(__linux__) && defined(SO_PEERCRED)
    struct PeerCredential { pid_t pid; uid_t uid; gid_t gid; } credential{};
    socklen_t length = sizeof(credential);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credential, &length) == 0
        && credential.uid == geteuid();
#elif defined(__APPLE__) || defined(__FreeBSD__)
    uid_t uid{};
    gid_t gid{};
    return getpeereid(fd, &uid, &gid) == 0 && uid == geteuid();
#else
    static_cast<void>(fd);
    return false;
#endif
}

class UnixPipeListener final : public PipeListener {
public:
    UnixPipeListener(std::string endpoint, const std::size_t max_connections)
        : endpoint_(std::move(endpoint))
    {
        const auto slash = endpoint_.find_last_of('/');
        if (endpoint_.empty() || endpoint_.front() != '/' || slash == std::string::npos
            || slash == 0 || endpoint_.find('\0') != std::string::npos) return;
        const auto parent = endpoint_.substr(0, slash);
        std::array<char, PATH_MAX> resolved{};
        if (!realpath(parent.c_str(), resolved.data()) || parent != resolved.data()) return;
        struct stat parent_status {};
        if (lstat(parent.c_str(), &parent_status) != 0
            || !S_ISDIR(parent_status.st_mode)
            || parent_status.st_uid != geteuid()
            || (parent_status.st_mode & (S_IRWXG | S_IRWXO)) != 0) return;
        struct stat existing {};
        if (lstat(endpoint_.c_str(), &existing) == 0 || errno != ENOENT) return;
        sockaddr_un address{};
        if (endpoint_.size() >= sizeof(address.sun_path)) return;

        int socket_type = SOCK_STREAM;
#ifdef SOCK_CLOEXEC
        socket_type |= SOCK_CLOEXEC;
#endif
        const auto fd = ::socket(AF_UNIX, socket_type, 0);
        if (fd < 0) return;
        if (!set_fd_flags(fd, true)) {
            ::close(fd);
            return;
        }
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, endpoint_.c_str(), endpoint_.size() + 1);
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            ::close(fd);
            return;
        }
        if (!record_owned_socket()
            || ::chmod(endpoint_.c_str(), S_IRUSR | S_IWUSR) != 0
            || ::listen(fd, static_cast<int>(max_connections)) != 0) {
            ::close(fd);
            unlink_owned_socket();
            return;
        }
        struct stat socket_status {};
        if (lstat(endpoint_.c_str(), &socket_status) != 0
            || !S_ISSOCK(socket_status.st_mode)
            || socket_status.st_uid != geteuid()
            || (socket_status.st_mode & 0777) != 0600) {
            ::close(fd);
            unlink_owned_socket();
            return;
        }
        fd_.store(fd);
        std::array<int, 2> wake{-1, -1};
#if defined(__linux__) && defined(O_CLOEXEC) && defined(O_NONBLOCK)
        const auto pipe_result = ::pipe2(wake.data(), O_CLOEXEC | O_NONBLOCK);
#else
        const auto pipe_result = ::pipe(wake.data());
#endif
        if (pipe_result != 0
            || !set_fd_flags(wake[0], true) || !set_fd_flags(wake[1], true)) {
            if (wake[0] >= 0) ::close(wake[0]);
            if (wake[1] >= 0) ::close(wake[1]);
            const auto owned = fd_.exchange(-1);
            if (owned >= 0) ::close(owned);
            unlink_owned_socket();
            return;
        }
        wake_read_.store(wake[0]);
        wake_write_.store(wake[1]);
        created_ = true;
    }

    ~UnixPipeListener() override
    {
        close();
        std::unique_lock lock(lifecycle_mutex_);
        lifecycle_changed_.wait(lock, [this] { return !accept_active_; });
        cleanup_locked();
    }

    [[nodiscard]] bool valid() const noexcept { return fd_.load() >= 0; }

    std::unique_ptr<PipeStream> accept() override
    {
        {
            std::lock_guard lock(lifecycle_mutex_);
            if (stopped_.load() || accept_active_) return {};
            accept_active_ = true;
        }
        std::unique_ptr<PipeStream> result;
        try {
            while (!stopped_.load()) {
                const auto listener = fd_.load();
                const auto wake = wake_read_.load();
                if (listener < 0 || wake < 0) break;
                std::array<pollfd, 2> events{
                    pollfd{listener, POLLIN, 0}, pollfd{wake, POLLIN, 0}};
                const auto ready = ::poll(events.data(), events.size(), -1);
                if (ready < 0 && errno == EINTR) continue;
                if (ready <= 0 || (events[1].revents & POLLIN) != 0
                    || stopped_.load()) break;
#if defined(__linux__) && defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
                const auto client = ::accept4(
                    listener, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
                const auto client = ::accept(listener, nullptr, nullptr);
#endif
                if (client < 0) {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    break;
                }
                if (!set_fd_flags(client, true) || !current_user_peer(client)) {
                    ::close(client);
                    continue;
                }
                try {
                    auto stream = std::make_unique<UnixPipeStream>(client);
                    if (!stream->valid()) continue;
                    result = std::move(stream);
                    break;
                } catch (...) {
                    ::close(client);
                    throw;
                }
            }
        } catch (...) {
            finish_accept();
            throw;
        }
        finish_accept();
        return result;
    }

    void close() noexcept override
    {
        if (stopped_.exchange(true)) return;
        const auto wake = wake_write_.load();
        if (wake >= 0) {
            const std::byte signal{1};
            static_cast<void>(::write(wake, &signal, 1));
        }
        std::lock_guard lock(lifecycle_mutex_);
        if (!accept_active_) cleanup_locked();
    }

private:
    void finish_accept() noexcept
    {
        std::lock_guard lock(lifecycle_mutex_);
        accept_active_ = false;
        if (stopped_.load()) cleanup_locked();
        lifecycle_changed_.notify_all();
    }

    void cleanup_locked() noexcept
    {
        const auto fd = fd_.exchange(-1);
        if (fd >= 0) ::close(fd);
        const auto wake_read = wake_read_.exchange(-1);
        if (wake_read >= 0) ::close(wake_read);
        const auto wake_write = wake_write_.exchange(-1);
        if (wake_write >= 0) ::close(wake_write);
        unlink_owned_socket();
    }

    [[nodiscard]] bool record_owned_socket() noexcept
    {
        struct stat path_status {};
        if (lstat(endpoint_.c_str(), &path_status) != 0
            || !S_ISSOCK(path_status.st_mode))
            return false;
        owned_device_ = path_status.st_dev;
        owned_inode_ = path_status.st_ino;
        created_ = true;
        return true;
    }

    void unlink_owned_socket() noexcept
    {
        if (!created_) return;
        struct stat status {};
        if (lstat(endpoint_.c_str(), &status) == 0
            && S_ISSOCK(status.st_mode)
            && status.st_dev == owned_device_ && status.st_ino == owned_inode_) {
            ::unlink(endpoint_.c_str());
        }
        created_ = false;
    }

    std::string endpoint_;
    std::atomic_int fd_{-1};
    std::atomic_int wake_read_{-1};
    std::atomic_int wake_write_{-1};
    std::atomic_bool stopped_{};
    std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_changed_;
    bool accept_active_{};
    bool created_{};
    dev_t owned_device_{};
    ino_t owned_inode_{};
};

#endif

}  // namespace

std::unique_ptr<PipeListener> make_platform_pipe_listener(
    const std::string_view endpoint,
    const std::size_t max_connections,
    std::string& diagnostic) noexcept
{
    try {
    diagnostic.clear();
    if (max_connections == 0 || max_connections > 64) {
        diagnostic = "invalid_max_connections";
        return {};
    }
#if defined(_WIN32)
    if (endpoint.size() > 512
        || endpoint.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())
        || endpoint.find('\0') != std::string_view::npos) {
        diagnostic = "invalid_named_pipe_endpoint";
        return {};
    }
    const auto wide = utf8_to_wide(endpoint);
    if (!wide || !wide->starts_with(L"\\\\.\\pipe\\")
        || wide->size() <= 9) {
        diagnostic = "invalid_named_pipe_endpoint";
        return {};
    }
    auto listener = std::make_unique<WindowsPipeListener>(*wide);
    if (!listener->valid()) {
        diagnostic = "current_user_acl_initialization_failed";
        return {};
    }
    return listener;
#elif defined(__unix__) || defined(__APPLE__)
    auto listener = std::make_unique<UnixPipeListener>(std::string{endpoint}, max_connections);
    if (!listener->valid()) {
        diagnostic = "insecure_or_invalid_unix_socket_endpoint";
        return {};
    }
    return listener;
#else
    static_cast<void>(endpoint);
    diagnostic = "pipe_transport_unavailable";
    return {};
#endif
    } catch (...) {
        try {
            diagnostic = "pipe_listener_allocation_failed";
        } catch (...) {
            diagnostic.clear();
        }
        return {};
    }
}

}  // namespace baas::service::pipe
