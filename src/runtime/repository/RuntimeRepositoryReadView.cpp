#include "RuntimeRepositoryReadViewInternal.h"
#include "RuntimeRepositoryTreeFormat.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace baas::runtime::repository {
namespace {

[[noreturn]] void fail(
    const RuntimeRepositoryReadErrorCode code, const std::string_view message) {
    throw RuntimeRepositoryReadError(code, std::string(message));
}

#ifdef BAAS_RUNTIME_REPOSITORY_TESTING
using ReadHookPoint = RuntimeRepositoryReadHookPoint;
std::atomic<RuntimeRepositoryReadViewHook> read_hook{};
void invoke_hook(const ReadHookPoint point,
                 const std::string_view repository_id,
                 const std::string_view path) {
    if (const auto hook = read_hook.load(std::memory_order_acquire))
        hook(point, repository_id, path);
}
#else
enum class ReadHookPoint : std::uint8_t {
    repository_root_opened,
    manifest_handle_opened,
    manifest_digest_finalizing,
    manifest_verified,
    payload_handle_opened,
    payload_digest_finalizing,
};
void invoke_hook(ReadHookPoint, std::string_view, std::string_view) {}
#endif

struct TreeRecord final {
    std::uintmax_t size{};
    std::string portable_key;
};

struct EnumeratedTree final {
    std::map<std::string, TreeRecord, std::less<>> files;
    std::set<std::string, std::less<>> directories;
    std::map<std::string, std::string, std::less<>> portable_paths;
    std::size_t entries{};
    std::uintmax_t total_bytes{};
};

[[nodiscard]] std::vector<std::string_view> components(const std::string_view path) {
    std::vector<std::string_view> result;
    std::size_t begin{};
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        result.push_back(path.substr(begin,
            end == std::string_view::npos ? path.size() - begin : end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return result;
}

void account_path(EnumeratedTree& tree, const std::string& relative,
                  const bool directory, const std::uintmax_t size,
                  const RuntimeRepositoryReadLimits& limits) {
    detail::TreeFormatLimits format_limits{limits.max_files - 1, limits.max_file_bytes,
        limits.max_total_bytes, limits.max_relative_path_bytes, limits.max_relative_path_depth};
    const auto key = detail::portable_path_key(relative, format_limits);
    if (!tree.portable_paths.emplace(key, relative).second)
        fail(RuntimeRepositoryReadErrorCode::invalid_manifest,
             "repository tree contains a portable path alias");
    if (++tree.entries > limits.max_entries)
        fail(RuntimeRepositoryReadErrorCode::file_limit_exceeded,
             "repository tree entry limit exceeded");
    if (directory) {
        tree.directories.emplace(relative);
        return;
    }
    if (tree.files.size() == limits.max_files || size > limits.max_file_bytes ||
        size > limits.max_total_bytes - tree.total_bytes)
        fail(RuntimeRepositoryReadErrorCode::file_limit_exceeded,
             "repository tree file limit exceeded");
    tree.total_bytes += size;
    tree.files.emplace(relative, TreeRecord{size, std::move(key)});
}

#ifdef _WIN32

constexpr DWORD repository_directory_share_mode =
    FILE_SHARE_READ | FILE_SHARE_WRITE;
constexpr DWORD repository_file_share_mode =
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

class UniqueHandle final {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
    ~UniqueHandle() { if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    UniqueHandle(UniqueHandle&& other) noexcept : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
            value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

[[nodiscard]] std::filesystem::path final_path(HANDLE handle) {
    const auto length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (length == 0) fail(RuntimeRepositoryReadErrorCode::io, "final handle path query failed");
    std::wstring buffer(length, L'\0');
    const auto written = GetFinalPathNameByHandleW(
        handle, buffer.data(), length, FILE_NAME_NORMALIZED);
    if (written == 0 || written >= length)
        fail(RuntimeRepositoryReadErrorCode::io, "final handle path read failed");
    buffer.resize(written);
    return std::filesystem::path(buffer);
}

[[nodiscard]] UniqueHandle open_windows_directory(const std::filesystem::path& path) {
    UniqueHandle result(CreateFileW(path.c_str(), GENERIC_READ,
        repository_directory_share_mode, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (result.get() == INVALID_HANDLE_VALUE)
        fail(RuntimeRepositoryReadErrorCode::io, "repository directory open failed");
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(result.get(), FileAttributeTagInfo, &tag, sizeof(tag)) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        fail(RuntimeRepositoryReadErrorCode::path_violation,
             "repository directory is linked or not a directory");
    return result;
}

[[nodiscard]] std::string utf8_name(const wchar_t* value) {
    const auto source_length = static_cast<int>(std::wstring_view(value).size());
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, source_length,
                                             nullptr, 0, nullptr, nullptr);
    if (length <= 0) fail(RuntimeRepositoryReadErrorCode::path_violation,
                          "repository name is not valid Unicode");
    std::string result(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, source_length,
                            result.data(), length, nullptr, nullptr) != length)
        fail(RuntimeRepositoryReadErrorCode::path_violation,
             "repository name UTF-8 conversion failed");
    return result;
}

[[nodiscard]] std::uintmax_t windows_file_size(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info) {
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandle(handle, &info) ||
        !GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof(tag)) ||
        (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || info.nNumberOfLinks != 1)
        fail(RuntimeRepositoryReadErrorCode::path_violation,
             "repository payload is linked or not a plain file");
    return (static_cast<std::uintmax_t>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow;
}

void enumerate_windows(const std::filesystem::path& root, const std::string& prefix,
                       EnumeratedTree& tree, const RuntimeRepositoryReadLimits& limits,
                       const std::stop_token stop_token) {
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryReadErrorCode::cancelled, "repository enumeration cancelled");
    const auto search = root / L"*";
    WIN32_FIND_DATAW data{};
    const auto raw = FindFirstFileW(search.c_str(), &data);
    if (raw == INVALID_HANDLE_VALUE)
        fail(RuntimeRepositoryReadErrorCode::io, "repository directory enumeration failed");
    struct FindOwner { HANDLE value; ~FindOwner(){FindClose(value);} } owner{raw};
    do {
        if (stop_token.stop_requested())
            fail(RuntimeRepositoryReadErrorCode::cancelled, "repository enumeration cancelled");
        if (std::wstring_view(data.cFileName) == L"." ||
            std::wstring_view(data.cFileName) == L"..") continue;
        const auto name = utf8_name(data.cFileName);
        const auto relative = prefix.empty() ? name : prefix + "/" + name;
        const auto path = root / data.cFileName;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            fail(RuntimeRepositoryReadErrorCode::path_violation,
                 "repository tree contains a reparse point");
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            auto directory = open_windows_directory(path);
            account_path(tree, relative, true, 0, limits);
            enumerate_windows(final_path(directory.get()), relative, tree, limits, stop_token);
        } else {
            UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ,
                repository_file_share_mode, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (file.get() == INVALID_HANDLE_VALUE)
                fail(RuntimeRepositoryReadErrorCode::io, "repository payload open failed");
            BY_HANDLE_FILE_INFORMATION info{};
            account_path(tree, relative, false, windows_file_size(file.get(), info), limits);
        }
    } while (FindNextFileW(raw, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        fail(RuntimeRepositoryReadErrorCode::io, "repository enumeration failed");
}

#else

class UniqueFd final {
public:
    UniqueFd() = default;
    explicit UniqueFd(const int value) noexcept : value_(value) {}
    ~UniqueFd() { if (value_ >= 0) close(value_); }
    UniqueFd(UniqueFd&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) { if (value_ >= 0) close(value_); value_ = std::exchange(other.value_, -1); }
        return *this;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    [[nodiscard]] int get() const noexcept { return value_; }
private:
    int value_{-1};
};

[[nodiscard]] UniqueFd open_unix_directory_at(const int parent, const std::string_view component) {
    const std::string owned(component);
    const auto fd = openat(parent, owned.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) fail((errno == ELOOP || errno == ENOTDIR)
                         ? RuntimeRepositoryReadErrorCode::path_violation
                         : RuntimeRepositoryReadErrorCode::io,
                     "repository directory open failed");
    struct stat info{};
    if (fstat(fd, &info) != 0 || !S_ISDIR(info.st_mode)) { close(fd); fail(
        RuntimeRepositoryReadErrorCode::path_violation, "repository component is not a directory"); }
    return UniqueFd(fd);
}

void enumerate_unix(const int root, const std::string& prefix, EnumeratedTree& tree,
                    const RuntimeRepositoryReadLimits& limits,
                    const std::stop_token stop_token) {
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryReadErrorCode::cancelled, "repository enumeration cancelled");
    // dup() shares the directory stream offset with the pinned descriptor.  A
    // second sealing pass can therefore start at end-of-directory on platforms
    // such as macOS.  Reopen "." relative to the pinned descriptor so every
    // pass has an independent cursor without resolving the repository path.
    const auto scan = openat(root, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (scan < 0) fail(RuntimeRepositoryReadErrorCode::io, "directory scan open failed");
    DIR* raw = fdopendir(scan);
    if (!raw) { close(scan); fail(RuntimeRepositoryReadErrorCode::io,
                                      "directory enumeration open failed"); }
    struct DirOwner { DIR* value; ~DirOwner(){closedir(value);} } owner{raw};
    errno = 0;
    while (const auto* item = readdir(raw)) {
        if (stop_token.stop_requested())
            fail(RuntimeRepositoryReadErrorCode::cancelled, "repository enumeration cancelled");
        const std::string name(item->d_name);
        if (name == "." || name == "..") continue;
        struct stat info{};
        if (fstatat(root, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0)
            fail(RuntimeRepositoryReadErrorCode::io, "repository entry stat failed");
        const auto relative = prefix.empty() ? name : prefix + "/" + name;
        if (S_ISLNK(info.st_mode)) fail(RuntimeRepositoryReadErrorCode::path_violation,
                                        "repository tree contains a symbolic link");
        if (S_ISDIR(info.st_mode)) {
            auto directory = open_unix_directory_at(root, name);
            account_path(tree, relative, true, 0, limits);
            enumerate_unix(directory.get(), relative, tree, limits, stop_token);
        } else if (S_ISREG(info.st_mode) && info.st_nlink == 1) {
            account_path(tree, relative, false, static_cast<std::uintmax_t>(info.st_size), limits);
        } else fail(RuntimeRepositoryReadErrorCode::path_violation,
                    "repository tree contains a linked or special file");
        errno = 0;
    }
    if (errno != 0) fail(RuntimeRepositoryReadErrorCode::io, "repository enumeration failed");
}

#endif

[[nodiscard]] bool directory_is_populated(
    const std::string& directory, const std::map<std::string, TreeRecord, std::less<>>& files) {
    const auto prefix = directory + "/";
    const auto found = files.lower_bound(prefix);
    return found != files.end() && found->first.starts_with(prefix);
}

void validate_exact_tree(const EnumeratedTree& tree, const std::string& manifest,
                         const std::vector<RuntimeRepositoryReadEntry>& expected) {
    if (tree.files.size() != expected.size() + 1 || !tree.files.contains(manifest))
        fail(RuntimeRepositoryReadErrorCode::invalid_manifest,
             "repository tree contains missing or extra files");
    for (const auto& entry : expected) {
        const auto found = tree.files.find(entry.path);
        if (found == tree.files.end() || found->second.size != entry.size)
            fail(RuntimeRepositoryReadErrorCode::invalid_manifest,
                 "repository tree differs from its manifest");
    }
    for (const auto& directory : tree.directories)
        if (!directory_is_populated(directory, tree.files))
            fail(RuntimeRepositoryReadErrorCode::invalid_manifest,
                 "repository tree contains an empty directory");
}

}  // namespace

struct RuntimeRepositoryStateRootAnchor final {
#ifdef _WIN32
    UniqueHandle handle;
    std::filesystem::path canonical;
#else
    UniqueFd descriptor;
#endif
};

struct RuntimeRepositoryReadView::Impl final {
    std::string generation;
    std::string repository_id;
    std::string manifest;
    RuntimeRepositoryReadLimits limits;
    std::vector<RuntimeRepositoryReadEntry> entries;
#ifdef _WIN32
    UniqueHandle root;
    std::filesystem::path canonical_root;
#else
    UniqueFd root;
#endif
};

RuntimeRepositoryReadError::RuntimeRepositoryReadError(
    const RuntimeRepositoryReadErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}
RuntimeRepositoryReadErrorCode RuntimeRepositoryReadError::code() const noexcept { return code_; }

std::shared_ptr<RuntimeRepositoryStateRootAnchor>
open_runtime_repository_state_root_anchor(const std::filesystem::path& state_root) {
    try {
        auto result = std::make_shared<RuntimeRepositoryStateRootAnchor>();
#ifdef _WIN32
        result->handle = open_windows_directory(state_root);
        result->canonical = final_path(result->handle.get());
#else
        const auto fd = open(state_root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) fail(errno == ELOOP ? RuntimeRepositoryReadErrorCode::path_violation
                                        : RuntimeRepositoryReadErrorCode::io,
                         "repository state root open failed");
        result->descriptor = UniqueFd(fd);
#endif
        return result;
    } catch (const std::bad_alloc&) {
        fail(RuntimeRepositoryReadErrorCode::resource_exhausted, "state root allocation failed");
    }
}

namespace {

#ifdef _WIN32
[[nodiscard]] UniqueHandle open_file_from_root(
    const RuntimeRepositoryReadView::Impl& impl, const std::string_view logical_path,
    BY_HANDLE_FILE_INFORMATION& information, std::uintmax_t& size) {
    auto path = impl.canonical_root;
    std::vector<UniqueHandle> directories;
    const auto parts = components(logical_path);
    for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
        path /= detail::path_from_utf8(parts[index]);
        directories.push_back(open_windows_directory(path));
        path = final_path(directories.back().get());
    }
    path /= detail::path_from_utf8(parts.back());
    UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ,
        repository_file_share_mode, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) fail(RuntimeRepositoryReadErrorCode::io,
                                                  "repository payload open failed");
    size = windows_file_size(file.get(), information);
    return file;
}
#else
[[nodiscard]] UniqueFd open_file_from_root(
    const RuntimeRepositoryReadView::Impl& impl, const std::string_view logical_path,
    struct stat& information, std::uintmax_t& size) {
    const auto duplicate = dup(impl.root.get());
    if (duplicate < 0) fail(RuntimeRepositoryReadErrorCode::io, "root duplicate failed");
    UniqueFd parent(duplicate);
    const auto parts = components(logical_path);
    for (std::size_t index = 0; index + 1 < parts.size(); ++index)
        parent = open_unix_directory_at(parent.get(), parts[index]);
    const std::string final(parts.back());
    auto flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
#ifdef O_NOCTTY
    flags |= O_NOCTTY;
#endif
    const auto fd = openat(parent.get(), final.c_str(), flags);
    if (fd < 0) fail(errno == ELOOP ? RuntimeRepositoryReadErrorCode::path_violation
                                    : RuntimeRepositoryReadErrorCode::io,
                     "repository payload open failed");
    UniqueFd file(fd);
    if (fstat(fd, &information) != 0 || !S_ISREG(information.st_mode) || information.st_nlink != 1)
        fail(RuntimeRepositoryReadErrorCode::path_violation,
             "repository payload is linked or not a plain file");
    size = static_cast<std::uintmax_t>(information.st_size);
    return file;
}
#endif

[[nodiscard]] std::vector<std::byte> read_handle(
    const RuntimeRepositoryReadView::Impl& impl, const std::string_view path,
    const std::uintmax_t expected_size, const std::uintmax_t max_bytes,
    const std::stop_token stop_token, const bool manifest,
    const std::string_view expected_sha256) {
    if (stop_token.stop_requested()) fail(RuntimeRepositoryReadErrorCode::cancelled, "read cancelled");
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION before{};
    std::uintmax_t actual_size{};
    auto file = open_file_from_root(impl, path, before, actual_size);
#else
    struct stat before{};
    std::uintmax_t actual_size{};
    auto file = open_file_from_root(impl, path, before, actual_size);
#endif
    if (!manifest && actual_size != expected_size)
        fail(manifest ? RuntimeRepositoryReadErrorCode::manifest_mismatch
                      : RuntimeRepositoryReadErrorCode::payload_mismatch,
             "repository file size differs from its trusted descriptor");
    const auto read_size = manifest ? actual_size : expected_size;
    if (read_size > max_bytes || read_size > std::numeric_limits<std::size_t>::max())
        fail(RuntimeRepositoryReadErrorCode::file_limit_exceeded, "read byte limit exceeded");
    invoke_hook(manifest ? ReadHookPoint::manifest_handle_opened
                         : ReadHookPoint::payload_handle_opened,
                impl.repository_id, path);
    std::vector<std::byte> result;
    try { result.resize(static_cast<std::size_t>(read_size)); }
    catch (const std::bad_alloc&) { fail(RuntimeRepositoryReadErrorCode::resource_exhausted,
                                         "repository read allocation failed"); }
    catch (const std::length_error&) { fail(RuntimeRepositoryReadErrorCode::resource_exhausted,
                                            "repository read size is not representable"); }
    detail::Sha256 hash;
    std::size_t offset{};
    while (offset < result.size()) {
        if (stop_token.stop_requested()) fail(RuntimeRepositoryReadErrorCode::cancelled, "read cancelled");
#ifdef _WIN32
        DWORD count{};
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(result.size() - offset, 64U*1024U));
        if (!ReadFile(file.get(), result.data() + offset, chunk, &count, nullptr) || count == 0)
            fail(RuntimeRepositoryReadErrorCode::io, "repository payload read failed");
        const auto consumed = static_cast<std::size_t>(count);
#else
        const auto count = ::read(file.get(), result.data() + offset,
                                  std::min<std::size_t>(result.size() - offset, 64U*1024U));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) fail(RuntimeRepositoryReadErrorCode::io, "repository payload read failed");
        const auto consumed = static_cast<std::size_t>(count);
#endif
        hash.update(std::span<const std::byte>(result.data() + offset, consumed));
        offset += consumed;
    }
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryReadErrorCode::cancelled, "read cancelled");
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(file.get(), &after) || after.nNumberOfLinks != 1 ||
        before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
        before.nFileIndexHigh != after.nFileIndexHigh || before.nFileIndexLow != after.nFileIndexLow ||
        before.nFileSizeHigh != after.nFileSizeHigh || before.nFileSizeLow != after.nFileSizeLow)
        fail(RuntimeRepositoryReadErrorCode::path_violation, "repository payload identity changed");
#else
    struct stat after{};
    if (fstat(file.get(), &after) != 0 || after.st_nlink != 1 || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size)
        fail(RuntimeRepositoryReadErrorCode::path_violation, "repository payload identity changed");
#endif
    invoke_hook(manifest ? ReadHookPoint::manifest_digest_finalizing
                         : ReadHookPoint::payload_digest_finalizing,
                impl.repository_id, path);
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryReadErrorCode::cancelled, "read cancelled");
    if (detail::sha256_hex(hash.finish()) != expected_sha256)
        fail(manifest ? RuntimeRepositoryReadErrorCode::manifest_mismatch
                      : RuntimeRepositoryReadErrorCode::payload_mismatch,
             manifest ? "manifest digest mismatch" : "payload digest mismatch");
    return result;
}

void verify_payload_streaming(
    const RuntimeRepositoryReadView::Impl& impl,
    const RuntimeRepositoryReadEntry& entry,
    const std::stop_token stop_token) {
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryReadErrorCode::cancelled, "payload verification cancelled");
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION before{};
    std::uintmax_t actual_size{};
    auto file = open_file_from_root(impl, entry.path, before, actual_size);
#else
    struct stat before{};
    std::uintmax_t actual_size{};
    auto file = open_file_from_root(impl, entry.path, before, actual_size);
#endif
    if (actual_size != entry.size)
        fail(RuntimeRepositoryReadErrorCode::payload_mismatch,
             "repository payload size differs from its manifest");
    invoke_hook(ReadHookPoint::payload_handle_opened,
                impl.repository_id, entry.path);
    detail::Sha256 hash;
    std::array<std::byte, 64U * 1024U> buffer{};
    std::uintmax_t remaining = entry.size;
    while (remaining != 0) {
        if (stop_token.stop_requested())
            fail(RuntimeRepositoryReadErrorCode::cancelled, "payload verification cancelled");
        const auto requested = static_cast<std::size_t>(
            std::min<std::uintmax_t>(remaining, buffer.size()));
#ifdef _WIN32
        DWORD count{};
        if (!ReadFile(file.get(), buffer.data(), static_cast<DWORD>(requested),
                      &count, nullptr) || count == 0)
            fail(RuntimeRepositoryReadErrorCode::io, "repository payload read failed");
        const auto consumed = static_cast<std::size_t>(count);
#else
        const auto count = ::read(file.get(), buffer.data(), requested);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
            fail(RuntimeRepositoryReadErrorCode::io, "repository payload read failed");
        const auto consumed = static_cast<std::size_t>(count);
#endif
        hash.update(std::span<const std::byte>(buffer.data(), consumed));
        remaining -= consumed;
    }
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryReadErrorCode::cancelled, "payload verification cancelled");
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(file.get(), &after) || after.nNumberOfLinks != 1 ||
        before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
        before.nFileIndexHigh != after.nFileIndexHigh ||
        before.nFileIndexLow != after.nFileIndexLow ||
        before.nFileSizeHigh != after.nFileSizeHigh ||
        before.nFileSizeLow != after.nFileSizeLow)
        fail(RuntimeRepositoryReadErrorCode::path_violation,
             "repository payload identity changed while sealing view");
#else
    struct stat after{};
    if (fstat(file.get(), &after) != 0 || after.st_nlink != 1 ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
        before.st_size != after.st_size)
        fail(RuntimeRepositoryReadErrorCode::path_violation,
             "repository payload identity changed while sealing view");
#endif
    if (detail::sha256_hex(hash.finish()) != entry.sha256)
        fail(RuntimeRepositoryReadErrorCode::payload_mismatch,
             "repository payload digest mismatch while sealing view");
}

}  // namespace

std::unique_ptr<RuntimeRepositoryReadView> RuntimeRepositoryReadViewFactory::open_view(
    const std::shared_ptr<RuntimeRepositoryStateRootAnchor>& state_root,
    const std::string& generation, const RuntimeRepository& repository,
    const RuntimeRepositoryReadLimits& limits,
    const std::stop_token stop_token) {
    if (stop_token.stop_requested())
        fail(RuntimeRepositoryReadErrorCode::cancelled, "read view open cancelled");
    auto impl = std::make_unique<RuntimeRepositoryReadView::Impl>();
    impl->generation = generation;
    impl->repository_id = repository.id;
    impl->manifest = repository.manifest;
    impl->limits = limits;
#ifdef _WIN32
    auto current = state_root->canonical;
    std::vector<UniqueHandle> parents;
    for (const auto component : {std::string_view{"objects"}, std::string_view{repository.id},
                                 std::string_view{repository.commit}}) {
        current /= detail::path_from_utf8(component);
        parents.push_back(open_windows_directory(current));
        current = final_path(parents.back().get());
    }
    impl->root = std::move(parents.back());
    impl->canonical_root = current;
#else
    const auto duplicate = dup(state_root->descriptor.get());
    if (duplicate < 0) fail(RuntimeRepositoryReadErrorCode::io, "state root duplicate failed");
    UniqueFd current(duplicate);
    current = open_unix_directory_at(current.get(), "objects");
    current = open_unix_directory_at(current.get(), repository.id);
    current = open_unix_directory_at(current.get(), repository.commit);
    impl->root = std::move(current);
#endif
    invoke_hook(ReadHookPoint::repository_root_opened,
                impl->repository_id, {});

    const auto manifest_bytes = read_handle(
        *impl, repository.manifest, 0, limits.max_manifest_bytes, stop_token,
        true, repository.manifest_sha256);
    invoke_hook(ReadHookPoint::manifest_verified,
                impl->repository_id, repository.manifest);

    detail::TreeFormatLimits format_limits{
        limits.max_files - 1, limits.max_file_bytes, limits.max_total_bytes,
        limits.max_relative_path_bytes, limits.max_relative_path_depth};
    try {
        for (auto& entry : detail::parse_tree_manifest(
                 std::string_view(reinterpret_cast<const char*>(manifest_bytes.data()),
                                  manifest_bytes.size()),
                 repository.manifest, format_limits))
            impl->entries.push_back({std::move(entry.path), entry.size, std::move(entry.sha256)});
    } catch (const detail::TreeFormatError& error) {
        fail(RuntimeRepositoryReadErrorCode::invalid_manifest, error.what());
    }
    EnumeratedTree tree;
#ifdef _WIN32
    enumerate_windows(impl->canonical_root, {}, tree, limits, stop_token);
#else
    enumerate_unix(impl->root.get(), {}, tree, limits, stop_token);
#endif
    validate_exact_tree(tree, repository.manifest, impl->entries);
    for (const auto& entry : impl->entries)
        verify_payload_streaming(*impl, entry, stop_token);
    EnumeratedTree sealed_tree;
#ifdef _WIN32
    enumerate_windows(impl->canonical_root, {}, sealed_tree, limits, stop_token);
#else
    enumerate_unix(impl->root.get(), {}, sealed_tree, limits, stop_token);
#endif
    validate_exact_tree(sealed_tree, repository.manifest, impl->entries);
    return std::unique_ptr<RuntimeRepositoryReadView>(new RuntimeRepositoryReadView(std::move(impl)));
}

std::shared_ptr<const RuntimeRepositoryReadBundle> RuntimeRepositoryReadViewFactory::open_bundle(
    std::shared_ptr<RuntimeRepositoryStateRootAnchor> state_root,
    const std::string& generation, const std::array<RuntimeRepository, 2>& repositories,
    const RuntimeRepositoryReadLimits limits,
    const std::stop_token stop_token) {
    if (!state_root || limits.max_manifest_bytes == 0 || limits.max_files < 1 ||
        limits.max_entries < limits.max_files || limits.max_total_bytes == 0 ||
        limits.max_file_bytes == 0 ||
        limits.max_relative_path_bytes == 0 || limits.max_relative_path_depth == 0)
        fail(RuntimeRepositoryReadErrorCode::file_limit_exceeded, "read view limits are invalid");
    try {
        auto resources = open_view(state_root, generation, repositories[0], limits, stop_token);
        auto scripts = open_view(state_root, generation, repositories[1], limits, stop_token);
        return std::shared_ptr<const RuntimeRepositoryReadBundle>(
            new RuntimeRepositoryReadBundle(generation, std::move(resources), std::move(scripts)));
    } catch (const std::bad_alloc&) {
        fail(RuntimeRepositoryReadErrorCode::resource_exhausted, "read bundle allocation failed");
    }
}

RuntimeRepositoryReadView::RuntimeRepositoryReadView(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
RuntimeRepositoryReadView::~RuntimeRepositoryReadView() = default;
const std::string& RuntimeRepositoryReadView::generation() const noexcept { return impl_->generation; }
const std::string& RuntimeRepositoryReadView::repository_id() const noexcept { return impl_->repository_id; }
std::span<const RuntimeRepositoryReadEntry> RuntimeRepositoryReadView::entries() const noexcept {
    return impl_->entries;
}
std::vector<std::byte> RuntimeRepositoryReadView::read(
    const std::string_view logical_path, const std::uintmax_t max_bytes,
    const std::stop_token stop_token) const {
    try {
        detail::TreeFormatLimits format_limits{impl_->limits.max_files - 1,
            impl_->limits.max_file_bytes, impl_->limits.max_total_bytes,
            impl_->limits.max_relative_path_bytes, impl_->limits.max_relative_path_depth};
        static_cast<void>(detail::portable_path_key(logical_path, format_limits));
        const auto found = std::ranges::lower_bound(impl_->entries, logical_path, {},
                                                     &RuntimeRepositoryReadEntry::path);
        if (found == impl_->entries.end() || found->path != logical_path)
            fail(RuntimeRepositoryReadErrorCode::entry_not_found, "logical path is not manifested");
        return read_handle(*impl_, logical_path, found->size, max_bytes,
                           stop_token, false, found->sha256);
    } catch (const detail::TreeFormatError& error) {
        fail(RuntimeRepositoryReadErrorCode::entry_not_found, error.what());
    } catch (const std::bad_alloc&) {
        fail(RuntimeRepositoryReadErrorCode::resource_exhausted, "read allocation failed");
    }
}

RuntimeRepositoryReadBundle::RuntimeRepositoryReadBundle(
    std::string generation, std::unique_ptr<RuntimeRepositoryReadView> resources,
    std::unique_ptr<RuntimeRepositoryReadView> scripts) noexcept
    : generation_(std::move(generation)), resources_(std::move(resources)), scripts_(std::move(scripts)) {}
RuntimeRepositoryReadBundle::~RuntimeRepositoryReadBundle() = default;
const std::string& RuntimeRepositoryReadBundle::generation() const noexcept { return generation_; }
const RuntimeRepositoryReadView& RuntimeRepositoryReadBundle::resources() const noexcept { return *resources_; }
const RuntimeRepositoryReadView& RuntimeRepositoryReadBundle::scripts() const noexcept { return *scripts_; }

#ifdef BAAS_RUNTIME_REPOSITORY_TESTING
void set_runtime_repository_read_view_hook(const RuntimeRepositoryReadViewHook hook) noexcept {
    read_hook.store(hook, std::memory_order_release);
}
#endif

}  // namespace baas::runtime::repository
