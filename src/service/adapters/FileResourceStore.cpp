#include "service/adapters/FileResourceStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winternl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace baas::service::adapters {
namespace {

[[nodiscard]] bool is_valid_utf8(const std::string_view input) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
    std::size_t index = 0;
    while (index < input.size()) {
        const auto lead = bytes[index++];
        if (lead <= 0x7fU) continue;

        std::size_t trailing{};
        std::uint32_t code_point{};
        std::uint32_t minimum{};
        if (lead >= 0xc2U && lead <= 0xdfU) {
            trailing = 1;
            code_point = lead & 0x1fU;
            minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            trailing = 2;
            code_point = lead & 0x0fU;
            minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            trailing = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (trailing > input.size() - index) return false;
        for (std::size_t offset = 0; offset < trailing; ++offset) {
            const auto byte = bytes[index++];
            if ((byte & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU
            || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

using Json = nlohmann::json;
using channels::ResourceKey;
using channels::ResourcePatchOperation;
using channels::ResourceSnapshot;
using channels::ResourceStoreError;
using channels::SyncResource;

struct JsonBounds {
    std::size_t bytes;
    std::size_t depth;
    std::size_t nodes;
};

struct ResourceKeyHash {
    std::size_t operator()(const ResourceKey& key) const noexcept
    {
        auto result = static_cast<std::size_t>(key.resource) * 0x9e3779b9U;
        if (key.resource_id) result ^= std::hash<std::string>{}(*key.resource_id);
        return result;
    }
};

bool bounded_tree(const Json& value, const std::size_t maximum_depth,
                  const std::size_t maximum_nodes, const std::size_t depth,
                  std::size_t& nodes)
{
    if (++nodes > maximum_nodes || depth > maximum_depth) return false;
    if (value.is_array()) {
        for (const auto& child : value) {
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) {
                return false;
            }
        }
    } else if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            static_cast<void>(key);
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<Json> parse_json(const std::string_view text, const JsonBounds bounds)
{
    if (text.size() > bounds.bytes || !is_valid_utf8(text)) return std::nullopt;
    try {
        bool duplicate{};
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto callback = [&duplicate, &object_keys](
                                  int, const Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == Json::parse_event_t::key && !object_keys.empty()) {
                if (!object_keys.back().insert(parsed.get<std::string>()).second) {
                    duplicate = true;
                }
            } else if (event == Json::parse_event_t::object_end && !object_keys.empty()) {
                object_keys.pop_back();
            }
            return !duplicate;
        };
        Json value = Json::parse(text, callback, false);
        if (duplicate || value.is_discarded()) return std::nullopt;
        std::size_t nodes{};
        if (!bounded_tree(value, bounds.depth, bounds.nodes, 0, nodes)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> timestamp_value(const Json& value)
{
    if (!value.is_number()) return std::nullopt;
    const auto result = value.get<double>();
    return std::isfinite(result) ? std::optional{result} : std::nullopt;
}

std::optional<double> timestamp_value(
    const std::string_view value, const JsonBounds bounds)
{
    const auto parsed = parse_json(value, bounds);
    return parsed ? timestamp_value(*parsed) : std::nullopt;
}

std::string timestamp_json(const double value) { return Json(value).dump(); }

std::optional<std::vector<std::string>> split_pointer(const std::string& path)
{
    if (path.empty() || path == "/") return std::vector<std::string>{};
    if (path.front() != '/') return std::nullopt;
    std::vector<std::string> result;
    std::size_t begin = 1;
    while (true) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(begin, end - begin);
        std::string decoded;
        for (std::size_t index = 0; index < component.size(); ++index) {
            if (component[index] != '~') {
                decoded.push_back(component[index]);
                continue;
            }
            if (++index >= component.size()) return std::nullopt;
            if (component[index] == '0') decoded.push_back('~');
            else if (component[index] == '1') decoded.push_back('/');
            else return std::nullopt;
        }
        result.push_back(std::move(decoded));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

std::optional<std::size_t> array_index(const std::string& value)
{
    if (value.empty()
        || !std::all_of(value.begin(), value.end(), [](const char character) {
               return character >= '0' && character <= '9';
           })) {
        return std::nullopt;
    }
    try {
        const auto parsed = std::stoull(value);
        if (parsed > std::numeric_limits<std::size_t>::max()) return std::nullopt;
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

bool apply_operation(Json& document, const ResourcePatchOperation& operation,
                     const JsonBounds bounds, std::string& error)
{
    const auto parts = split_pointer(operation.path);
    if (!parts) {
        error = "Invalid JSON pointer";
        return false;
    }
    const bool needs_value = operation.op == "add" || operation.op == "replace";
    std::optional<Json> value;
    if (needs_value) {
        if (!operation.value_json) {
            error = "Patch operation is missing value";
            return false;
        }
        value = parse_json(*operation.value_json, bounds);
        if (!value) {
            error = "Patch value is invalid";
            return false;
        }
    } else if (operation.op != "remove") {
        error = "Unsupported patch operation";
        return false;
    }
    if (parts->empty()) {
        if (operation.op == "remove") {
            error = "Cannot remove document root";
            return false;
        }
        document = std::move(*value);
        return true;
    }

    Json* parent = std::addressof(document);
    for (std::size_t index = 0; index + 1 < parts->size(); ++index) {
        const auto& part = (*parts)[index];
        if (parent->is_object()) {
            const auto found = parent->find(part);
            if (found == parent->end()) {
                error = "Patch path does not exist";
                return false;
            }
            parent = std::addressof(found.value());
        } else if (parent->is_array()) {
            const auto position = array_index(part);
            if (!position || *position >= parent->size()) {
                error = "Patch array index is invalid";
                return false;
            }
            parent = std::addressof((*parent)[*position]);
        } else {
            error = "Patch path traverses a scalar";
            return false;
        }
    }

    const auto& leaf = parts->back();
    if (parent->is_object()) {
        if (operation.op == "remove") {
            if (parent->erase(leaf) == 0) {
                error = "Patch path does not exist";
                return false;
            }
        } else {
            (*parent)[leaf] = std::move(*value);
        }
        return true;
    }
    if (parent->is_array()) {
        if (operation.op == "add" && leaf == "-") {
            parent->push_back(std::move(*value));
            return true;
        }
        const auto position = array_index(leaf);
        if (!position) {
            error = "Patch array index is invalid";
            return false;
        }
        if (operation.op == "add") {
            if (*position > parent->size()) {
                error = "Patch array index is invalid";
                return false;
            }
            parent->insert(
                parent->begin() + static_cast<Json::difference_type>(*position),
                std::move(*value));
        } else if (*position >= parent->size()) {
            error = "Patch array index is invalid";
            return false;
        } else if (operation.op == "replace") {
            (*parent)[*position] = std::move(*value);
        } else {
            parent->erase(
                parent->begin() + static_cast<Json::difference_type>(*position));
        }
        return true;
    }
    error = "Patch path parent is not a container";
    return false;
}

Json operations_json(const std::vector<ResourcePatchOperation>& operations)
{
    Json result = Json::array();
    for (const auto& operation : operations) {
        Json item{{"op", operation.op}, {"path", operation.path}};
        if (operation.value_json) {
            item["value"] = Json::parse(*operation.value_json);
        }
        result.push_back(std::move(item));
    }
    return result;
}

bool valid_resource_id(const std::string& value, const std::size_t maximum)
{
    if (value.empty() || value.size() > maximum || !is_valid_utf8(value)
        || value == "." || value == "..") {
        return false;
    }
    const bool safe_characters = std::none_of(
        value.begin(), value.end(), [](const unsigned char character) {
        return character == '/' || character == '\\' || character == ':'
            || character == 0 || character < 0x20 || character == 0x7f;
    });
    if (!safe_characters) return false;
#if defined(_WIN32)
    if (value.back() == '.' || value.back() == ' ') return false;
    const auto dot = value.find('.');
    std::string base = value.substr(0, dot);
    std::transform(base.begin(), base.end(), base.begin(), [](const unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    static const std::unordered_set<std::string> reserved{
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
        "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
        "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (reserved.contains(base)) return false;
#endif
    return true;
}

enum class PathKind { missing, regular_file, directory, reparse, other, error };

PathKind path_kind(const std::filesystem::path& path) noexcept
{
    try {
#if defined(_WIN32)
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return GetLastError() == ERROR_FILE_NOT_FOUND
                || GetLastError() == ERROR_PATH_NOT_FOUND
            ? PathKind::missing
            : PathKind::error;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return PathKind::reparse;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return PathKind::directory;
    return PathKind::regular_file;
#else
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        return error == std::errc::no_such_file_or_directory
            ? PathKind::missing : PathKind::error;
    }
    if (std::filesystem::is_symlink(status)) return PathKind::reparse;
    if (std::filesystem::is_regular_file(status)) return PathKind::regular_file;
    if (std::filesystem::is_directory(status)) return PathKind::directory;
    if (!std::filesystem::exists(status)) return PathKind::missing;
    return PathKind::other;
#endif
    } catch (...) {
        return PathKind::error;
    }
}

bool path_is_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) noexcept
{
    try {
    std::error_code root_error;
    std::error_code candidate_error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, root_error);
    const auto canonical_candidate =
        std::filesystem::weakly_canonical(candidate, candidate_error);
    if (root_error || candidate_error) return false;
    auto root_part = canonical_root.begin();
    auto candidate_part = canonical_candidate.begin();
    for (; root_part != canonical_root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == canonical_candidate.end() || *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
    } catch (...) {
        return false;
    }
}

std::uint64_t process_id() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::filesystem::path temporary_path(const std::filesystem::path& target,
                                     const std::uint64_t sequence)
{
    auto name = target.filename().string();
    name += ".baas.tmp." + std::to_string(process_id()) + "."
        + std::to_string(sequence);
    std::u8string encoded;
    encoded.reserve(name.size());
    for (const auto character : name) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return target.parent_path() / std::filesystem::path(encoded);
}

std::string filename_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.filename().u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

#if defined(_WIN32)
class WindowsHandle {
public:
    WindowsHandle() = default;
    explicit WindowsHandle(const HANDLE value) : value_(value) {}
    ~WindowsHandle()
    {
        if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    }
    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;
    WindowsHandle(WindowsHandle&& other) noexcept
        : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE))
    {}
    WindowsHandle& operator=(WindowsHandle&& other) noexcept
    {
        if (this == &other) return *this;
        if (value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
        value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value_ != INVALID_HANDLE_VALUE;
    }
private:
    HANDLE value_{INVALID_HANDLE_VALUE};
};

struct WindowsRootAnchor {
    WindowsHandle handle;
    std::filesystem::path path;
};

using NtCreateFileFunction = NTSTATUS(NTAPI*)(
    PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER,
    ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
using NtSetInformationFileFunction = NTSTATUS(NTAPI*)(
    HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, FILE_INFORMATION_CLASS);

NtCreateFileFunction nt_create_file() noexcept
{
    static const auto function = reinterpret_cast<NtCreateFileFunction>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
    return function;
}

NtSetInformationFileFunction nt_set_information_file() noexcept
{
    static const auto function = reinterpret_cast<NtSetInformationFileFunction>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile"));
    return function;
}

WindowsHandle open_relative_windows(
    const HANDLE parent, const std::wstring_view name, const ACCESS_MASK access,
    const ULONG disposition, const bool directory) noexcept
{
    if (name.empty() || name.size() > USHRT_MAX / sizeof(wchar_t)) return {};
    const auto create_file = nt_create_file();
    if (!create_file) return {};
    UNICODE_STRING unicode{};
    unicode.Buffer = const_cast<PWSTR>(name.data());
    unicode.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    unicode.MaximumLength = unicode.Length;
    OBJECT_ATTRIBUTES attributes{};
    InitializeObjectAttributes(&attributes, &unicode, 0, parent, nullptr);
    IO_STATUS_BLOCK status{};
    HANDLE result = INVALID_HANDLE_VALUE;
    const ULONG options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT
        | (directory ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE)
        | (disposition == FILE_CREATE ? FILE_WRITE_THROUGH : 0U);
    const auto nt_status = create_file(
        &result, access | SYNCHRONIZE, &attributes, &status, nullptr,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE
            | (directory ? 0U : FILE_SHARE_DELETE),
        disposition, options, nullptr, 0);
    if (nt_status < 0) return {};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(
            result, FileAttributeTagInfo, &tag, sizeof(tag))
        || (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        if (disposition == FILE_CREATE) {
            FILE_DISPOSITION_INFO delete_on_close{TRUE};
            static_cast<void>(SetFileInformationByHandle(
                result, FileDispositionInfo, &delete_on_close,
                sizeof(delete_on_close)));
        }
        CloseHandle(result);
        return {};
    }
    return WindowsHandle(result);
}

std::shared_ptr<WindowsRootAnchor> open_windows_root_anchor(
    const std::filesystem::path& root)
{
    WindowsHandle handle(CreateFileW(
        root.c_str(), FILE_LIST_DIRECTORY | FILE_TRAVERSE
            | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle) return {};
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(
            handle.get(), FileAttributeTagInfo, &tag, sizeof(tag))
        || (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
        || (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return {};
    }
    const auto required = GetFinalPathNameByHandleW(
        handle.get(), nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (required == 0) return {};
    std::wstring final_path(required, L'\0');
    const auto written = GetFinalPathNameByHandleW(
        handle.get(), final_path.data(), required,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (written == 0 || written >= required) return {};
    final_path.resize(written);
    constexpr std::wstring_view unc_prefix = L"\\\\?\\UNC\\";
    constexpr std::wstring_view prefix = L"\\\\?\\";
    if (final_path.starts_with(unc_prefix)) {
        final_path = L"\\\\" + final_path.substr(unc_prefix.size());
    } else if (final_path.starts_with(prefix)) {
        final_path.erase(0, prefix.size());
    }
    return std::make_shared<WindowsRootAnchor>(WindowsRootAnchor{
        std::move(handle), std::filesystem::path(final_path).lexically_normal()});
}

struct WindowsDirectoryChain {
    std::vector<WindowsHandle> handles;
    [[nodiscard]] HANDLE get() const noexcept
    {
        return handles.empty() ? INVALID_HANDLE_VALUE : handles.back().get();
    }
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return !handles.empty();
    }
};

WindowsDirectoryChain open_windows_resource_parent(
    const WindowsRootAnchor& anchor, const std::filesystem::path& target,
    const bool writable) noexcept
{
    try {
        const auto relative = target.lexically_relative(anchor.path);
        std::vector<std::wstring> components;
        for (const auto& component : relative.parent_path()) {
            const auto value = component.native();
            if (value.empty() || value == L"." || value == L"..") return {};
            components.push_back(value);
        }
        HANDLE current = anchor.handle.get();
        WindowsDirectoryChain chain;
        chain.handles.reserve(components.size());
        for (std::size_t index = 0; index < components.size(); ++index) {
            const auto& component = components[index];
            auto next = open_relative_windows(
                current, component,
                FILE_LIST_DIRECTORY | FILE_TRAVERSE
                    | FILE_READ_ATTRIBUTES
                    | (writable && index + 1 == components.size()
                           ? FILE_ADD_FILE : 0U),
                FILE_OPEN, true);
            if (!next) return {};
            chain.handles.push_back(std::move(next));
            current = chain.handles.back().get();
        }
        return chain;
    } catch (...) {
        return {};
    }
}

bool windows_handle_has_exact_path(
    const HANDLE handle, const std::filesystem::path& expected) noexcept
{
    try {
        const auto required = GetFinalPathNameByHandleW(
            handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (required == 0) return false;
        std::wstring actual(required, L'\0');
        const auto written = GetFinalPathNameByHandleW(
            handle, actual.data(), required,
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (written == 0 || written >= required) return false;
        actual.resize(written);
        constexpr std::wstring_view unc_prefix = L"\\\\?\\UNC\\";
        constexpr std::wstring_view prefix = L"\\\\?\\";
        if (actual.starts_with(unc_prefix)) {
            actual = L"\\\\" + actual.substr(unc_prefix.size());
        } else if (actual.starts_with(prefix)) {
            actual.erase(0, prefix.size());
        }
        const auto actual_native =
            std::filesystem::path(actual).lexically_normal().native();
        const auto expected_native = expected.lexically_normal().native();
        return actual_native == expected_native;
    } catch (...) {
        return false;
    }
}

struct WindowsAnchoredRead {
    std::string bytes;
    double modified_ms{};
    ResourceStoreError error{ResourceStoreError::none};
};

WindowsAnchoredRead read_windows_resource(
    const WindowsRootAnchor& anchor, const std::filesystem::path& target,
    const std::size_t maximum_bytes)
{
    auto directory = open_windows_resource_parent(anchor, target, false);
    if (!directory) return {{}, 0, ResourceStoreError::invalid_data};
    const auto name = target.filename().native();
    auto file = open_relative_windows(
        directory.get(), name, GENERIC_READ | FILE_READ_ATTRIBUTES,
        FILE_OPEN, false);
    if (!file) return {{}, 0, ResourceStoreError::not_found};
    if (!windows_handle_has_exact_path(file.get(), target)) {
        return {{}, 0, ResourceStoreError::invalid_data};
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0) {
        return {{}, 0, ResourceStoreError::internal_error};
    }
    if (static_cast<std::uint64_t>(size.QuadPart) > maximum_bytes) {
        return {{}, 0, ResourceStoreError::capacity};
    }
    WindowsAnchoredRead result;
    result.bytes.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t offset{};
    while (offset < result.bytes.size()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
            result.bytes.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD read{};
        if (!ReadFile(file.get(), result.bytes.data() + offset, chunk, &read, nullptr)
            || read == 0) {
            return {{}, 0, ResourceStoreError::internal_error};
        }
        offset += read;
    }
    LARGE_INTEGER size_after{};
    if (!GetFileSizeEx(file.get(), &size_after)
        || size_after.QuadPart != size.QuadPart) {
        return {{}, 0, ResourceStoreError::internal_error};
    }
    FILETIME modified{};
    if (!GetFileTime(file.get(), nullptr, nullptr, &modified)) {
        return {{}, 0, ResourceStoreError::internal_error};
    }
    ULARGE_INTEGER ticks{};
    ticks.LowPart = modified.dwLowDateTime;
    ticks.HighPart = modified.dwHighDateTime;
    constexpr std::uint64_t epoch_delta_100ns = 116'444'736'000'000'000ULL;
    if (ticks.QuadPart < epoch_delta_100ns) {
        return {{}, 0, ResourceStoreError::internal_error};
    }
    result.modified_ms = static_cast<double>(
        (ticks.QuadPart - epoch_delta_100ns) / 10'000ULL);
    return result;
}

bool windows_regular_resource_exists(
    const WindowsRootAnchor& anchor, const std::filesystem::path& target) noexcept
{
    auto directory = open_windows_resource_parent(anchor, target, false);
    if (!directory) return false;
    const auto name = target.filename().native();
    auto file = open_relative_windows(
        directory.get(), name, FILE_READ_ATTRIBUTES, FILE_OPEN, false);
    return file && windows_handle_has_exact_path(file.get(), target);
}
#else
class PosixFd {
public:
    PosixFd() = default;
    explicit PosixFd(const int value) : value_(value) {}
    ~PosixFd() { if (value_ >= 0) static_cast<void>(::close(value_)); }
    PosixFd(const PosixFd&) = delete;
    PosixFd& operator=(const PosixFd&) = delete;
    PosixFd(PosixFd&& other) noexcept
        : value_(std::exchange(other.value_, -1))
    {}
    PosixFd& operator=(PosixFd&& other) noexcept
    {
        if (this == &other) return *this;
        if (value_ >= 0) static_cast<void>(::close(value_));
        value_ = std::exchange(other.value_, -1);
        return *this;
    }
    [[nodiscard]] int get() const noexcept { return value_; }
    [[nodiscard]] int release() noexcept { return std::exchange(value_, -1); }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ >= 0; }
private:
    int value_{-1};
};

struct PosixRootAnchor {
    PosixFd fd;
    std::filesystem::path path;
};

std::shared_ptr<PosixRootAnchor> open_posix_root_anchor(
    const std::filesystem::path& root)
{
    PosixFd fd(::open(
        root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!fd) return {};
    struct stat status {};
    if (::fstat(fd.get(), &status) != 0 || !S_ISDIR(status.st_mode)) return {};
    return std::make_shared<PosixRootAnchor>(
        PosixRootAnchor{std::move(fd), root});
}

PosixFd open_posix_resource_parent(
    const PosixRootAnchor& anchor, const std::filesystem::path& target)
{
    const auto relative = target.lexically_relative(anchor.path);
    int current = anchor.fd.get();
    PosixFd owned;
    for (const auto& component : relative.parent_path()) {
        const auto name = component.native();
        if (name.empty() || name == "." || name == "..") return {};
        PosixFd next(::openat(
            current, name.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (!next) return {};
        owned = std::move(next);
        current = owned.get();
    }
    return owned;
}

struct PosixAnchoredRead {
    std::string bytes;
    double modified_ms{};
    ResourceStoreError error{ResourceStoreError::none};
};

PosixAnchoredRead read_posix_resource(
    const PosixRootAnchor& anchor, const std::filesystem::path& target,
    const std::size_t maximum_bytes)
{
    auto directory = open_posix_resource_parent(anchor, target);
    if (!directory) return {{}, 0, ResourceStoreError::invalid_data};
    const auto name = target.filename().native();
    PosixFd file(::openat(
        directory.get(), name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!file) return {{}, 0, ResourceStoreError::not_found};
    struct stat status {};
    if (::fstat(file.get(), &status) != 0 || !S_ISREG(status.st_mode)) {
        return {{}, 0, ResourceStoreError::invalid_data};
    }
    if (status.st_size < 0
        || static_cast<std::uint64_t>(status.st_size) > maximum_bytes) {
        return {{}, 0, ResourceStoreError::capacity};
    }
    PosixAnchoredRead result;
    result.bytes.resize(static_cast<std::size_t>(status.st_size));
    std::size_t offset{};
    while (offset < result.bytes.size()) {
        const auto read = ::read(
            file.get(), result.bytes.data() + offset, result.bytes.size() - offset);
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) return {{}, 0, ResourceStoreError::internal_error};
        offset += static_cast<std::size_t>(read);
    }
    struct stat after {};
    if (::fstat(file.get(), &after) != 0 || after.st_size != status.st_size) {
        return {{}, 0, ResourceStoreError::internal_error};
    }
#if defined(__APPLE__)
    result.modified_ms = static_cast<double>(after.st_mtimespec.tv_sec) * 1000.0
        + static_cast<double>(after.st_mtimespec.tv_nsec) / 1'000'000.0;
#else
    result.modified_ms = static_cast<double>(after.st_mtim.tv_sec) * 1000.0
        + static_cast<double>(after.st_mtim.tv_nsec) / 1'000'000.0;
#endif
    return result;
}

bool posix_regular_resource_exists(
    const PosixRootAnchor& anchor, const std::filesystem::path& target)
{
    auto directory = open_posix_resource_parent(anchor, target);
    if (!directory) return false;
    const auto name = target.filename().native();
    PosixFd file(::openat(
        directory.get(), name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!file) return false;
    struct stat status {};
    return ::fstat(file.get(), &status) == 0 && S_ISREG(status.st_mode);
}
#endif

AtomicWriteResult checked_post_commit_durability(
    const std::filesystem::path& parent,
    const FileResourceStoreDependencies::PostCommitDurabilityCheck& check) noexcept
{
    if (!check) return AtomicWriteResult::committed;
    try {
        return check(parent) ? AtomicWriteResult::committed
                             : AtomicWriteResult::committed_durability_uncertain;
    } catch (...) {
        return AtomicWriteResult::committed_durability_uncertain;
    }
}

#if defined(_WIN32)
AtomicWriteResult durable_atomic_write(const std::filesystem::path& target,
                                       const std::string_view bytes,
                                       const FileResourceStoreDependencies::
                                           PostCommitDurabilityCheck& check,
                                       const std::shared_ptr<WindowsRootAnchor>& anchor)
{
    if (!anchor) return AtomicWriteResult::not_committed;
    static std::atomic<std::uint64_t> next_sequence{};
    const auto parent = target.parent_path();
    const auto target_name = target.filename().native();
    if (target_name.empty()) return AtomicWriteResult::not_committed;
    auto directory = open_windows_resource_parent(*anchor, target, true);
    if (!directory) return AtomicWriteResult::not_committed;
    const auto target_name_bytes = target_name.size() * sizeof(wchar_t);
    // Follow the WDK sizing guidance conservatively. FILE_RENAME_INFO embeds
    // one WCHAR and may have tail padding, so sizeof + FileNameLength is
    // deliberately larger than offsetof(FileName) + FileNameLength.
    const auto rename_size = sizeof(FILE_RENAME_INFO) + target_name_bytes;
    const auto rename_units =
        (rename_size + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
    std::vector<std::max_align_t> rename_storage(rename_units);
    auto* const rename = reinterpret_cast<FILE_RENAME_INFO*>(rename_storage.data());
    rename->ReplaceIfExists = TRUE;
    rename->RootDirectory = directory.get();
    rename->FileNameLength = static_cast<DWORD>(target_name_bytes);
    std::memcpy(rename->FileName, target_name.data(), rename->FileNameLength);
    for (std::size_t attempt = 0; attempt < 64; ++attempt) {
        const auto temporary_name = temporary_path(
            target, next_sequence.fetch_add(1)).filename().native();
        auto file = open_relative_windows(
            directory.get(), temporary_name,
            GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES,
            FILE_CREATE, false);
        if (!file) continue;

        bool ok = true;
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                remaining, std::numeric_limits<DWORD>::max()));
            DWORD written{};
            if (!WriteFile(file.get(), bytes.data() + offset, chunk, &written, nullptr)
                || written == 0) {
                ok = false;
                break;
            }
            offset += written;
        }
        if (ok && !FlushFileBuffers(file.get())) ok = false;
        if (ok) {
            IO_STATUS_BLOCK rename_status{};
            const auto set_information = nt_set_information_file();
            ok = set_information
                && set_information(
                       file.get(), &rename_status, rename,
                       static_cast<ULONG>(rename_size),
                       static_cast<FILE_INFORMATION_CLASS>(10)) >= 0;
        }
        if (!ok) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            static_cast<void>(SetFileInformationByHandle(
                file.get(), FileDispositionInfo, &disposition,
                sizeof(disposition)));
        }
        if (!ok) return AtomicWriteResult::not_committed;
        return checked_post_commit_durability(parent, check);
    }
    return AtomicWriteResult::not_committed;
}
#else
AtomicWriteResult durable_atomic_write(const std::filesystem::path& target,
                                       const std::string_view bytes,
                                       const FileResourceStoreDependencies::
                                           PostCommitDurabilityCheck& check,
                                       const std::shared_ptr<PosixRootAnchor>& anchor)
{
    if (!anchor) return AtomicWriteResult::not_committed;
    static std::atomic<std::uint64_t> next_sequence{};
    const auto parent = target.parent_path();
    const auto target_name = target.filename().native();
    auto directory = open_posix_resource_parent(*anchor, target);
    if (!directory) return AtomicWriteResult::not_committed;
    mode_t mode = 0600;
    struct stat target_status {};
    if (::fstatat(directory.get(), target_name.c_str(), &target_status,
                  AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(target_status.st_mode)) {
        return AtomicWriteResult::not_committed;
    }
    mode = target_status.st_mode & 0777;
    for (std::size_t attempt = 0; attempt < 64; ++attempt) {
        const auto temporary_name = temporary_path(
            target, next_sequence.fetch_add(1)).filename().native();
        const int file = ::openat(
            directory.get(), temporary_name.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
        if (file < 0) {
            if (errno == EEXIST) continue;
            return AtomicWriteResult::not_committed;
        }
        bool ok = true;
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                ok = false;
                break;
            }
            offset += static_cast<std::size_t>(written);
        }
        if (ok && ::fsync(file) != 0) ok = false;
        if (::close(file) != 0) ok = false;
        if (ok && ::renameat(directory.get(), temporary_name.c_str(), directory.get(),
                             target_name.c_str()) != 0) {
            ok = false;
        }
        if (!ok) {
            static_cast<void>(::unlinkat(
                directory.get(), temporary_name.c_str(), 0));
            return AtomicWriteResult::not_committed;
        }
        const bool directory_ok = ::fsync(directory.get()) == 0;
        const bool close_ok = ::close(directory.release()) == 0;
        if (!directory_ok || !close_ok) {
            return AtomicWriteResult::committed_durability_uncertain;
        }
        return checked_post_commit_durability(parent, check);
    }
    return AtomicWriteResult::not_committed;
}
#endif

double system_clock_ms()
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool valid_limits(const channels::ResourceStoreLimits& limits) noexcept
{
    return limits.max_json_bytes != 0 && limits.max_json_depth != 0
        && limits.max_json_nodes != 0 && limits.max_resources != 0
        && limits.max_subscribers != 0 && limits.max_patch_operations != 0
        && limits.max_resource_id_bytes != 0 && limits.max_origin_bytes != 0;
}

constexpr std::size_t config_copy_max_files = 4'096;
constexpr std::uintmax_t config_copy_max_bytes = 64U * 1'024U * 1'024U;
constexpr std::size_t config_copy_max_depth = 32;

[[nodiscard]] ConfigCommandError inspect_config_tree(
    const std::filesystem::path& root, const std::stop_token stop)
{
    std::error_code error;
    if (path_kind(root) == PathKind::missing) return ConfigCommandError::not_found;
    if (path_kind(root) != PathKind::directory) return ConfigCommandError::invalid_data;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    std::size_t files{};
    std::uintmax_t bytes{};
    for (; !error && iterator != end; iterator.increment(error)) {
        if (stop.stop_requested()) return ConfigCommandError::cancelled;
        if (iterator.depth() >= static_cast<int>(config_copy_max_depth)) {
            return ConfigCommandError::capacity;
        }
        const auto kind = path_kind(iterator->path());
        if (kind == PathKind::directory) continue;
        if (kind != PathKind::regular_file) {
            return ConfigCommandError::invalid_data;
        }
        if (++files > config_copy_max_files) return ConfigCommandError::capacity;
        const auto size = iterator->file_size(error);
        if (error) break;
        if (size > config_copy_max_bytes || bytes > config_copy_max_bytes - size) {
            return ConfigCommandError::capacity;
        }
        bytes += size;
    }
    return error ? ConfigCommandError::internal_error : ConfigCommandError::none;
}

[[nodiscard]] ConfigCommandError copy_config_tree(
    const std::filesystem::path& source,
    const std::filesystem::path& target,
    const std::stop_token stop,
    const std::function<std::pair<std::optional<std::string>, ConfigCommandError>(
        const std::filesystem::path&)>& reader,
    const std::function<bool(const std::filesystem::path&, std::string_view)>& writer)
{
    std::error_code error;
    const auto created = std::filesystem::create_directory(target, error);
    if (error) return ConfigCommandError::internal_error;
    if (!created) return ConfigCommandError::conflict;
    std::filesystem::recursive_directory_iterator iterator(
        source, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        if (stop.stop_requested()) return ConfigCommandError::cancelled;
        const auto relative = iterator->path().lexically_relative(source);
        if (relative.empty()) return ConfigCommandError::invalid_data;
        const auto destination = target / relative;
        const auto kind = path_kind(iterator->path());
        if (kind == PathKind::directory) {
            const auto directory_created =
                std::filesystem::create_directory(destination, error);
            if (!error && !directory_created) return ConfigCommandError::conflict;
        } else if (kind == PathKind::regular_file) {
            auto [bytes, read_error] = reader(iterator->path());
            if (!bytes) return read_error;
            if (!writer(destination, *bytes)) {
                return ConfigCommandError::internal_error;
            }
        } else {
            return ConfigCommandError::invalid_data;
        }
        if (error) break;
    }
    return error ? ConfigCommandError::internal_error : ConfigCommandError::none;
}

void remove_tree_best_effort(const std::filesystem::path& path) noexcept
{
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
}

}  // namespace

class FileResourceStore::Impl {
public:
    struct SubscriberSlot {
        UpdateCallback callback;
        bool accepting{true};
        std::size_t entered{};
    };

    inline static thread_local SubscriberSlot* callback_slot{};
    inline static thread_local Impl* publication_drainer{};

    struct Subscribers {
        std::mutex mutex;
        std::condition_variable condition;
        bool active{true};
        std::size_t next_id{};
        std::size_t maximum{};
        std::unordered_map<std::size_t, std::shared_ptr<SubscriberSlot>> slots;
    };

    struct PublicationJob {
        channels::ResourceUpdate update;
        std::vector<std::shared_ptr<SubscriberSlot>> slots;
        std::shared_ptr<PublicationJob> next;
        std::condition_variable condition;
        bool done{};
    };

    struct Subscription final : channels::ResourceSubscription {
        Subscription(std::weak_ptr<Subscribers> owner, const std::size_t id,
                     std::shared_ptr<SubscriberSlot> slot)
            : owner(std::move(owner)), id(id), slot(std::move(slot))
        {}

        ~Subscription() override
        {
            const auto owner_state = owner.lock();
            if (!owner_state) return;
            std::unique_lock lock(owner_state->mutex);
            slot->accepting = false;
            owner_state->slots.erase(id);
            if (Impl::callback_slot == slot.get()) return;
            owner_state->condition.wait(lock, [this] { return slot->entered == 0; });
        }

        std::weak_ptr<Subscribers> owner;
        std::size_t id;
        std::shared_ptr<SubscriberSlot> slot;
    };

    struct LoadedResource {
        ResourceSnapshot snapshot;
        Json document;
        std::filesystem::path path;
    };

    struct LoadResult {
        std::optional<LoadedResource> value;
        ResourceStoreError error{ResourceStoreError::none};
    };

    Impl(std::filesystem::path supplied_root,
         FileResourceStoreDependencies dependencies,
         const channels::ResourceStoreLimits supplied_limits)
        : limits(supplied_limits), subscribers(std::make_shared<Subscribers>())
    {
        if (!valid_limits(limits) || supplied_root.empty()) {
            throw std::invalid_argument("invalid file resource store configuration");
        }
        std::error_code absolute_error;
        root = std::filesystem::absolute(supplied_root, absolute_error).lexically_normal();
        if (absolute_error || path_kind(root) != PathKind::directory) {
            throw std::invalid_argument("project root must be a safe existing directory");
        }
#if defined(_WIN32)
        windows_root = open_windows_root_anchor(root);
        if (!windows_root) {
            throw std::invalid_argument("project root cannot be anchored safely");
        }
        root = windows_root->path;
#else
        posix_root = open_posix_root_anchor(root);
        if (!posix_root) {
            throw std::invalid_argument("project root cannot be anchored safely");
        }
#endif
        config_root = root / "config";
        const auto config_kind = path_kind(config_root);
        if (config_kind != PathKind::missing && config_kind != PathKind::directory) {
            throw std::invalid_argument("config root must be a safe directory");
        }
        clock = dependencies.clock ? std::move(dependencies.clock) : system_clock_ms;
        if (dependencies.atomic_writer) {
            atomic_writer = std::move(dependencies.atomic_writer);
        } else {
            auto post_commit_check =
                std::move(dependencies.post_commit_durability_check);
#if defined(_WIN32)
            auto root_anchor = windows_root;
            atomic_writer = [post_commit_check = std::move(post_commit_check),
                             root_anchor = std::move(root_anchor)](
                                const std::filesystem::path& target,
                                const std::string_view bytes) {
                return durable_atomic_write(
                    target, bytes, post_commit_check, root_anchor);
            };
#else
            auto root_anchor = posix_root;
            atomic_writer = [post_commit_check = std::move(post_commit_check),
                             root_anchor = std::move(root_anchor)](
                                const std::filesystem::path& target,
                                const std::string_view bytes) {
                return durable_atomic_write(
                    target, bytes, post_commit_check, root_anchor);
            };
#endif
        }
        if (!clock || !atomic_writer) {
            throw std::invalid_argument("file resource store dependencies are invalid");
        }
        subscribers->maximum = limits.max_subscribers;
    }

    ~Impl()
    {
        std::unique_lock lock(subscribers->mutex);
        subscribers->active = false;
        for (auto& [id, slot] : subscribers->slots) {
            static_cast<void>(id);
            slot->accepting = false;
        }
        subscribers->condition.wait(lock, [this] {
            return std::all_of(subscribers->slots.begin(), subscribers->slots.end(),
                               [](const auto& item) {
                const auto& slot = item.second;
                return slot->entered == 0;
            });
        });
        subscribers->slots.clear();
    }

    JsonBounds bounds() const noexcept
    {
        return {limits.max_json_bytes, limits.max_json_depth, limits.max_json_nodes};
    }

    std::optional<ResourceStoreError> validate_key(const ResourceKey& key) const
    {
        if (key.resource == SyncResource::config || key.resource == SyncResource::event) {
            if (!key.resource_id
                || !valid_resource_id(*key.resource_id, limits.max_resource_id_bytes)) {
                return ResourceStoreError::invalid_data;
            }
        } else if (key.resource_id) {
            return ResourceStoreError::invalid_data;
        }
        if (key.resource == SyncResource::setup_toml) {
            return ResourceStoreError::not_found;
        }
        return std::nullopt;
    }

    std::filesystem::path unchecked_path(const ResourceKey& key) const
    {
        const auto id_path = [&key] {
            std::u8string encoded;
            if (!key.resource_id) return std::filesystem::path{};
            encoded.reserve(key.resource_id->size());
            for (const auto character : *key.resource_id) {
                encoded.push_back(
                    static_cast<char8_t>(static_cast<unsigned char>(character)));
            }
            return std::filesystem::path(encoded);
        }();
        switch (key.resource) {
            case SyncResource::config:
                return config_root / id_path / "config.json";
            case SyncResource::event:
                return config_root / id_path / "event.json";
            case SyncResource::gui: return config_root / "gui.json";
            case SyncResource::static_data: return config_root / "static.json";
            case SyncResource::setup_toml: break;
        }
        return {};
    }

    ResourceStoreError validate_existing_path(
        const ResourceKey& key, const std::filesystem::path& path) const
    {
        const auto config_kind = path_kind(config_root);
        if (config_kind == PathKind::missing) return ResourceStoreError::not_found;
        if (config_kind != PathKind::directory) return ResourceStoreError::invalid_data;
        if (!path_is_within(config_root, path)) return ResourceStoreError::invalid_data;
        if (key.resource == SyncResource::config || key.resource == SyncResource::event) {
            const auto directory = path.parent_path();
            const auto directory_kind = path_kind(directory);
            if (directory_kind == PathKind::missing) return ResourceStoreError::not_found;
            if (directory_kind != PathKind::directory) return ResourceStoreError::invalid_data;
        }
        const auto target_kind = path_kind(path);
        if (target_kind == PathKind::missing) return ResourceStoreError::not_found;
        if (target_kind != PathKind::regular_file) return ResourceStoreError::invalid_data;
        return ResourceStoreError::none;
    }

    LoadResult load(const ResourceKey& key) const
    {
        if (const auto invalid = validate_key(key)) return {std::nullopt, *invalid};
        const auto path = unchecked_path(key);
        const auto path_error = validate_existing_path(key, path);
        if (path_error != ResourceStoreError::none) return {std::nullopt, path_error};

        std::string bytes;
        double modified_value{};
#if defined(_WIN32)
        auto anchored = read_windows_resource(*windows_root, path, limits.max_json_bytes);
        if (anchored.error != ResourceStoreError::none) {
            return {std::nullopt, anchored.error};
        }
        bytes = std::move(anchored.bytes);
        modified_value = anchored.modified_ms;
#else
        auto anchored = read_posix_resource(*posix_root, path, limits.max_json_bytes);
        if (anchored.error != ResourceStoreError::none) {
            return {std::nullopt, anchored.error};
        }
        bytes = std::move(anchored.bytes);
        modified_value = anchored.modified_ms;
#endif
        const auto document = parse_json(bytes, bounds());
        if (!document) return {std::nullopt, ResourceStoreError::invalid_data};
        try {
            ResourceSnapshot snapshot{timestamp_json(modified_value), document->dump()};
            if (snapshot.data_json.size() > limits.max_json_bytes) {
                return {std::nullopt, ResourceStoreError::capacity};
            }
            return {LoadedResource{std::move(snapshot), *document, path},
                    ResourceStoreError::none};
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
    }

    std::shared_ptr<PublicationJob> prepare_publication(
        const channels::ResourceUpdate& update)
    {
        try {
            auto job = std::make_shared<PublicationJob>();
            job->update = update;
            job->slots.reserve(subscribers->maximum);
            return job;
        } catch (...) {
            return {};
        }
    }

    void finalize_publication(const std::shared_ptr<PublicationJob>& job) noexcept
    {
        std::lock_guard lock(subscribers->mutex);
        if (!subscribers->active) return;
        for (const auto& [id, slot] : subscribers->slots) {
            static_cast<void>(id);
            if (slot->accepting) job->slots.push_back(slot);
        }
    }

    void invoke_callbacks(PublicationJob& job) noexcept
    {
        for (const auto& slot : job.slots) {
            {
                std::lock_guard lock(subscribers->mutex);
                if (!subscribers->active || !slot->accepting) continue;
                ++slot->entered;
            }
            auto* const previous_slot = callback_slot;
            callback_slot = slot.get();
            try {
                slot->callback(job.update);
            } catch (...) {
            }
            callback_slot = previous_slot;
            std::lock_guard lock(subscribers->mutex);
            --slot->entered;
            subscribers->condition.notify_all();
        }
    }

    bool enqueue_publication(const std::shared_ptr<PublicationJob>& job) noexcept
    {
        std::lock_guard lock(publication_mutex);
        if (publication_tail) publication_tail->next = job;
        else publication_head = job;
        publication_tail = job;
        if (publication_active) return false;
        publication_active = true;
        return true;
    }

    void drain_or_wait(const std::shared_ptr<PublicationJob>& own_job,
                       const bool leader) noexcept
    {
        if (!leader) {
            if (publication_drainer == this) return;
            std::unique_lock lock(publication_mutex);
            own_job->condition.wait(lock, [&own_job] { return own_job->done; });
            return;
        }

        auto* const previous_drainer = publication_drainer;
        publication_drainer = this;
        for (;;) {
            std::shared_ptr<PublicationJob> job;
            {
                std::lock_guard lock(publication_mutex);
                job = publication_head;
                if (!job) {
                    publication_tail.reset();
                    publication_active = false;
                    break;
                }
                publication_head = job->next;
                if (!publication_head) publication_tail.reset();
                job->next.reset();
            }
            invoke_callbacks(*job);
            {
                std::lock_guard lock(publication_mutex);
                job->done = true;
                job->condition.notify_all();
            }
        }
        publication_drainer = previous_drainer;
    }

    std::filesystem::path root;
    std::filesystem::path config_root;
#if defined(_WIN32)
    std::shared_ptr<WindowsRootAnchor> windows_root;
#else
    std::shared_ptr<PosixRootAnchor> posix_root;
#endif
    FileResourceStoreDependencies::Clock clock;
    FileResourceStoreDependencies::AtomicWriter atomic_writer;
    channels::ResourceStoreLimits limits;
    std::mutex state_mutex;
    std::mutex mutation_mutex;
    std::mutex publication_mutex;
    std::shared_ptr<PublicationJob> publication_head;
    std::shared_ptr<PublicationJob> publication_tail;
    bool publication_active{};
    std::unordered_map<ResourceKey, ResourceSnapshot, ResourceKeyHash> resources;
    std::shared_ptr<Subscribers> subscribers;
};

FileResourceStore::FileResourceStore(
    std::filesystem::path project_root, FileResourceStoreDependencies dependencies,
    const channels::ResourceStoreLimits limits)
    : impl_(std::make_shared<Impl>(
          std::move(project_root), std::move(dependencies), limits))
{}

FileResourceStore::~FileResourceStore() = default;

channels::ResourceStoreResult<ResourceSnapshot> FileResourceStore::config_list(
    const std::stop_token stop) try
{
    const auto impl = impl_;
    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    std::vector<std::string> identifiers;
    const auto config_kind = path_kind(impl->config_root);
    if (config_kind == PathKind::missing) {
        double now{};
        try {
            now = impl->clock();
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (!std::isfinite(now)) return {std::nullopt, ResourceStoreError::internal_error};
        return {ResourceSnapshot{timestamp_json(now), "[]"}, ResourceStoreError::none};
    }
    if (config_kind != PathKind::directory) {
        return {std::nullopt, ResourceStoreError::invalid_data};
    }

    std::error_code iteration_error;
    std::filesystem::directory_iterator iterator(
        impl->config_root, std::filesystem::directory_options::skip_permission_denied,
        iteration_error);
    const std::filesystem::directory_iterator end;
    for (; !iteration_error && iterator != end; iterator.increment(iteration_error)) {
        if (stop.stop_requested()) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        const auto& child = iterator->path();
        std::string name;
        try {
            name = filename_utf8(child);
        } catch (...) {
            continue;
        }
        if (!valid_resource_id(name, impl->limits.max_resource_id_bytes)) {
            continue;
        }
        const auto config_path = child / "config.json";
        const auto event_path = child / "event.json";
#if defined(_WIN32)
        const bool safe_pair = windows_regular_resource_exists(
                                   *impl->windows_root, config_path)
            && windows_regular_resource_exists(*impl->windows_root, event_path);
#else
        const bool safe_pair = posix_regular_resource_exists(
                                   *impl->posix_root, config_path)
            && posix_regular_resource_exists(*impl->posix_root, event_path);
#endif
        if (!safe_pair) continue;
        if (identifiers.size() >= impl->limits.max_resources) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        identifiers.push_back(name);
    }
    if (iteration_error) return {std::nullopt, ResourceStoreError::internal_error};
    std::sort(identifiers.begin(), identifiers.end());

    try {
        Json data = identifiers;
        const auto serialized = data.dump();
        if (serialized.size() > impl->limits.max_json_bytes) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        double now = impl->clock();
        if (!std::isfinite(now)) return {std::nullopt, ResourceStoreError::internal_error};
        return {ResourceSnapshot{timestamp_json(now), serialized}, ResourceStoreError::none};
    } catch (...) {
        return {std::nullopt, ResourceStoreError::internal_error};
    }
} catch (...) {
    return {std::nullopt, ResourceStoreError::internal_error};
}

channels::ResourceStoreResult<ResourceSnapshot> FileResourceStore::pull(
    const ResourceKey& key, const std::stop_token stop) try
{
    const auto impl = impl_;
    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    std::lock_guard lock(impl->state_mutex);
    const auto found = impl->resources.find(key);
    if (found != impl->resources.end()) return {found->second, ResourceStoreError::none};
    auto loaded = impl->load(key);
    if (!loaded.value) return {std::nullopt, loaded.error};
    if (impl->resources.size() >= impl->limits.max_resources) {
        return {std::nullopt, ResourceStoreError::capacity};
    }
    ResourceSnapshot result = loaded.value->snapshot;
    try {
        impl->resources.emplace(key, std::move(loaded.value->snapshot));
    } catch (...) {
        return {std::nullopt, ResourceStoreError::internal_error};
    }
    return {std::move(result), ResourceStoreError::none};
} catch (...) {
    return {std::nullopt, ResourceStoreError::internal_error};
}

channels::ResourceStoreResult<channels::ResourcePatchResult>
FileResourceStore::apply_patch(channels::ResourcePatchRequest request,
                               const std::stop_token stop) try
{
    using channels::ResourcePatchDisposition;
    using channels::ResourcePatchResult;
    using channels::ResourceUpdate;
    const auto impl = impl_;

    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    if (request.operations.size() > impl->limits.max_patch_operations) {
        return {std::nullopt, ResourceStoreError::capacity};
    }
    if (const auto invalid = impl->validate_key(request.key)) {
        return {std::nullopt, *invalid};
    }
    if (request.key.resource == SyncResource::static_data) {
        return {std::nullopt, ResourceStoreError::invalid_data};
    }
    const auto bounds = impl->bounds();
    const auto expected = timestamp_value(request.expected_timestamp_json, bounds);
    if (!expected) return {std::nullopt, ResourceStoreError::invalid_data};

    std::size_t operation_bytes = 2;
    for (const auto& operation : request.operations) {
        const std::size_t components[]{
            operation.op.size(), operation.path.size(),
            operation.value_json ? operation.value_json->size() : 0, 48};
        for (const auto component : components) {
            if (component > impl->limits.max_json_bytes
                || operation_bytes > impl->limits.max_json_bytes - component) {
                return {std::nullopt, ResourceStoreError::capacity};
            }
            operation_bytes += component;
        }
        if ((operation.op != "add" && operation.op != "remove"
             && operation.op != "replace")
            || !split_pointer(operation.path)) {
            return {std::nullopt, ResourceStoreError::invalid_data};
        }
        if (operation.value_json && !parse_json(*operation.value_json, bounds)) {
            return {std::nullopt, ResourceStoreError::invalid_data};
        }
    }

    std::string serialized_operations;
    try {
        serialized_operations = operations_json(request.operations).dump();
    } catch (...) {
        return {std::nullopt, ResourceStoreError::invalid_data};
    }
    if (serialized_operations.size() > impl->limits.max_json_bytes) {
        return {std::nullopt, ResourceStoreError::capacity};
    }

    ResourcePatchResult result;
    ResourceUpdate update;
    std::shared_ptr<Impl::PublicationJob> publication;
    std::unique_lock mutation_lock(impl->mutation_mutex);
    {
        std::lock_guard lock(impl->state_mutex);
        if (stop.stop_requested()) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        auto found = impl->resources.find(request.key);
        if (found == impl->resources.end()) {
            auto loaded = impl->load(request.key);
            if (!loaded.value) return {std::nullopt, loaded.error};
            if (impl->resources.size() >= impl->limits.max_resources) {
                return {std::nullopt, ResourceStoreError::capacity};
            }
            try {
                found = impl->resources
                            .emplace(request.key, std::move(loaded.value->snapshot))
                            .first;
            } catch (...) {
                return {std::nullopt, ResourceStoreError::internal_error};
            }
        }
        auto disk = impl->load(request.key);
        if (!disk.value) return {std::nullopt, disk.error};
        if (disk.value->snapshot.data_json != found->second.data_json) {
            found->second = std::move(disk.value->snapshot);
            result = {ResourcePatchDisposition::conflict, found->second,
                      "Resource changed outside the store"};
            return {std::move(result), ResourceStoreError::none};
        }
        const auto current = timestamp_value(found->second.timestamp_json, bounds);
        if (!current) return {std::nullopt, ResourceStoreError::internal_error};
        if (*expected < *current) {
            result = {ResourcePatchDisposition::conflict, found->second,
                      "Incoming patch is older than current snapshot"};
            return {std::move(result), ResourceStoreError::none};
        }
        auto document = parse_json(found->second.data_json, bounds);
        if (!document) return {std::nullopt, ResourceStoreError::internal_error};
        std::string patch_error;
        for (const auto& operation : request.operations) {
            if (!apply_operation(*document, operation, bounds, patch_error)) {
                result = {ResourcePatchDisposition::conflict, found->second,
                          std::move(patch_error)};
                return {std::move(result), ResourceStoreError::none};
            }
        }
        std::size_t nodes{};
        if (!bounded_tree(
                *document, bounds.depth, bounds.nodes, 0, nodes)) {
            return {std::nullopt, ResourceStoreError::capacity};
        }

        double now{};
        try {
            now = impl->clock();
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (!std::isfinite(now)) return {std::nullopt, ResourceStoreError::internal_error};

        std::string bytes;
        try {
            bytes = document->dump(2);
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (bytes.size() > impl->limits.max_json_bytes) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        const auto path = impl->unchecked_path(request.key);
        const auto path_error = impl->validate_existing_path(request.key, path);
        if (path_error != ResourceStoreError::none) return {std::nullopt, path_error};

        ResourceSnapshot committed;
        try {
            committed = {timestamp_json(std::max(*expected, now)), document->dump()};
            result = {ResourcePatchDisposition::applied, committed, {}};
            update = {request.key, committed.timestamp_json,
                      serialized_operations, "frontend"};
            publication = impl->prepare_publication(update);
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (!publication) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }

        AtomicWriteResult write_result{AtomicWriteResult::not_committed};
        try {
            write_result = impl->atomic_writer(path, bytes);
        } catch (...) {
            write_result = AtomicWriteResult::not_committed;
        }
        if (write_result == AtomicWriteResult::not_committed) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }

        found->second = std::move(committed);
        impl->finalize_publication(publication);
    }
    const bool publication_leader = impl->enqueue_publication(publication);
    mutation_lock.unlock();
    impl->drain_or_wait(publication, publication_leader);
    return {std::move(result), ResourceStoreError::none};
} catch (...) {
    return {std::nullopt, ResourceStoreError::internal_error};
}

channels::ResourceSubscribeResult FileResourceStore::subscribe_updates(
    UpdateCallback callback) try
{
    const auto impl = impl_;
    if (!callback) return {nullptr, ResourceStoreError::invalid_data};
    const auto subscribers = impl->subscribers;
    std::lock_guard lock(subscribers->mutex);
    if (!subscribers->active) return {nullptr, ResourceStoreError::internal_error};
    if (subscribers->slots.size() >= subscribers->maximum) {
        return {nullptr, ResourceStoreError::capacity};
    }
    try {
        auto slot = std::make_shared<Impl::SubscriberSlot>();
        slot->callback = std::move(callback);
        for (std::size_t attempt = 0; attempt <= subscribers->slots.size(); ++attempt) {
            const auto id = subscribers->next_id;
            subscribers->next_id = id == std::numeric_limits<std::size_t>::max()
                ? 0 : id + 1;
            if (subscribers->slots.contains(id)) continue;
            const auto [position, inserted] = subscribers->slots.emplace(id, slot);
            static_cast<void>(position);
            if (!inserted) continue;
            try {
                return {std::make_unique<Impl::Subscription>(subscribers, id, slot),
                        ResourceStoreError::none};
            } catch (...) {
                subscribers->slots.erase(id);
                throw;
            }
        }
        return {nullptr, ResourceStoreError::internal_error};
    } catch (...) {
        return {nullptr, ResourceStoreError::internal_error};
    }
} catch (...) {
    return {nullptr, ResourceStoreError::internal_error};
}

bool FileResourceStore::refresh_and_publish(ResourceKey key, std::string origin) try
{
    const auto impl = impl_;
    if (origin.size() > impl->limits.max_origin_bytes || !is_valid_utf8(origin)) {
        return false;
    }
    if (const auto invalid = impl->validate_key(key)) return false;

    channels::ResourceUpdate update;
    std::shared_ptr<Impl::PublicationJob> publication;
    std::unique_lock mutation_lock(impl->mutation_mutex);
    {
        std::lock_guard lock(impl->state_mutex);
        auto loaded = impl->load(key);
        if (!loaded.value) return false;
        auto found = impl->resources.find(key);
        if (found != impl->resources.end()
            && found->second.data_json == loaded.value->snapshot.data_json) {
            found->second.timestamp_json = std::move(loaded.value->snapshot.timestamp_json);
            return false;
        }
        try {
            Json operations = Json::array({
                Json{{"op", "replace"}, {"path", ""},
                     {"value", loaded.value->document}}});
            update = {key, loaded.value->snapshot.timestamp_json,
                      operations.dump(), std::move(origin)};
            publication = impl->prepare_publication(update);
            if (!publication) return false;
            if (found == impl->resources.end()) {
                if (impl->resources.size() >= impl->limits.max_resources) return false;
                impl->resources.emplace(key, std::move(loaded.value->snapshot));
            } else {
                found->second = std::move(loaded.value->snapshot);
            }
            impl->finalize_publication(publication);
        } catch (...) {
            return false;
        }
    }
    const bool publication_leader = impl->enqueue_publication(publication);
    mutation_lock.unlock();
    impl->drain_or_wait(publication, publication_leader);
    return true;
} catch (...) {
    return false;
}

const std::filesystem::path& FileResourceStore::project_root() const noexcept
{
    return impl_->root;
}

ConfigCopyResult FileResourceStore::copy_config(
    const std::string_view source_id, const std::stop_token stop) try
{
    const auto impl = impl_;
    if (stop.stop_requested()) return {{}, {}, ConfigCommandError::cancelled};
    const std::string source_name{source_id};
    if (!valid_resource_id(source_name, impl->limits.max_resource_id_bytes)) {
        return {{}, {}, ConfigCommandError::invalid_id};
    }
    std::unique_lock mutation_lock(impl->mutation_mutex);
    if (stop.stop_requested()) return {{}, {}, ConfigCommandError::cancelled};

    const ResourceKey config_key{SyncResource::config, source_name};
    const ResourceKey event_key{SyncResource::event, source_name};
    auto config = impl->load(config_key);
    if (!config.value) {
        const auto mapped = config.error == ResourceStoreError::not_found
            ? ConfigCommandError::not_found
            : config.error == ResourceStoreError::capacity
                ? ConfigCommandError::capacity
                : config.error == ResourceStoreError::invalid_data
                    ? ConfigCommandError::invalid_data
                    : ConfigCommandError::internal_error;
        return {{}, {}, mapped};
    }
    auto event = impl->load(event_key);
    if (!event.value) {
        const auto mapped = event.error == ResourceStoreError::not_found
            ? ConfigCommandError::not_found
            : event.error == ResourceStoreError::capacity
                ? ConfigCommandError::capacity
                : event.error == ResourceStoreError::invalid_data
                    ? ConfigCommandError::invalid_data
                    : ConfigCommandError::internal_error;
        return {{}, {}, mapped};
    }
    static_cast<void>(event);
    const auto source = config.value->path.parent_path();
    if (const auto inspected = inspect_config_tree(source, stop);
        inspected != ConfigCommandError::none) {
        return {{}, {}, inspected};
    }

    std::string base_name = source_name;
    if (const auto found = config.value->document.find("name");
        found != config.value->document.end() && found->is_string()) {
        base_name = found->get<std::string>();
        if (base_name.empty()) base_name = source_name;
    }
    std::unordered_set<std::string> existing_names;
    std::error_code iteration_error;
    std::filesystem::directory_iterator iterator(
        impl->config_root, std::filesystem::directory_options::skip_permission_denied,
        iteration_error);
    const std::filesystem::directory_iterator end;
    for (; !iteration_error && iterator != end; iterator.increment(iteration_error)) {
        if (stop.stop_requested()) return {{}, {}, ConfigCommandError::cancelled};
        std::string id;
        try {
            id = filename_utf8(iterator->path());
        } catch (...) {
            continue;
        }
        if (!valid_resource_id(id, impl->limits.max_resource_id_bytes)) continue;
        auto loaded = impl->load({SyncResource::config, id});
        if (!loaded.value) continue;
        const auto name = loaded.value->document.find("name");
        if (name != loaded.value->document.end() && name->is_string()) {
            existing_names.insert(name->get<std::string>());
        }
    }
    if (iteration_error) return {{}, {}, ConfigCommandError::internal_error};

    std::string copy_name = base_name + "_copy";
    for (std::size_t suffix = 2; existing_names.contains(copy_name); ++suffix) {
        if (suffix > impl->limits.max_resources + 2) {
            return {{}, {}, ConfigCommandError::capacity};
        }
        copy_name = base_name + "_copy" + std::to_string(suffix);
    }
    if (copy_name.size() > impl->limits.max_json_bytes) {
        return {{}, {}, ConfigCommandError::capacity};
    }

    double now{};
    try {
        now = impl->clock();
    } catch (...) {
        return {{}, {}, ConfigCommandError::internal_error};
    }
    if (!std::isfinite(now) || now < 0
        || now > static_cast<double>(
            std::numeric_limits<std::int64_t>::max() - 1'024)) {
        return {{}, {}, ConfigCommandError::internal_error};
    }
    const auto initial = static_cast<std::int64_t>(now);
    std::string target_id;
    std::filesystem::path target;
    for (std::size_t attempt = 0; attempt < 1'024; ++attempt) {
        target_id = std::to_string(initial + static_cast<std::int64_t>(attempt));
        target = impl->config_root / target_id;
        if (path_kind(target) == PathKind::missing) break;
        target_id.clear();
    }
    if (target_id.empty()) return {{}, {}, ConfigCommandError::conflict};

    const auto staging_root = impl->root / ".baas-config-staging";
    std::error_code filesystem_error;
    if (path_kind(staging_root) == PathKind::missing) {
        std::filesystem::create_directory(staging_root, filesystem_error);
    }
    if (filesystem_error || path_kind(staging_root) != PathKind::directory) {
        return {{}, {}, ConfigCommandError::internal_error};
    }
    static std::atomic<std::uint64_t> staging_sequence{};
    const auto staging = staging_root /
        ("copy-" + target_id + "-" +
         std::to_string(staging_sequence.fetch_add(1, std::memory_order_relaxed)));
    if (path_kind(staging) != PathKind::missing) {
        return {{}, {}, ConfigCommandError::conflict};
    }
    const auto reader = [impl](const std::filesystem::path& path)
        -> std::pair<std::optional<std::string>, ConfigCommandError> {
#if defined(_WIN32)
        auto result = read_windows_resource(
            *impl->windows_root, path,
            static_cast<std::size_t>(config_copy_max_bytes));
#else
        auto result = read_posix_resource(
            *impl->posix_root, path,
            static_cast<std::size_t>(config_copy_max_bytes));
#endif
        if (result.error == ResourceStoreError::none) {
            return {std::move(result.bytes), ConfigCommandError::none};
        }
        if (result.error == ResourceStoreError::capacity) {
            return {std::nullopt, ConfigCommandError::capacity};
        }
        if (result.error == ResourceStoreError::invalid_data) {
            return {std::nullopt, ConfigCommandError::invalid_data};
        }
        if (result.error == ResourceStoreError::not_found) {
            return {std::nullopt, ConfigCommandError::conflict};
        }
        return {std::nullopt, ConfigCommandError::internal_error};
    };
    const auto writer = [impl](const std::filesystem::path& path,
                               const std::string_view bytes) {
        try {
            return impl->atomic_writer(path, bytes)
                != AtomicWriteResult::not_committed;
        } catch (...) {
            return false;
        }
    };
    const auto copied = copy_config_tree(source, staging, stop, reader, writer);
    if (copied != ConfigCommandError::none) {
        remove_tree_best_effort(staging);
        return {{}, {}, copied};
    }
    auto copied_document = config.value->document;
    copied_document["name"] = copy_name;
    std::string copied_bytes;
    try {
        copied_bytes = copied_document.dump(2);
    } catch (...) {
        remove_tree_best_effort(staging);
        return {{}, {}, ConfigCommandError::internal_error};
    }
    if (copied_bytes.size() > impl->limits.max_json_bytes) {
        remove_tree_best_effort(staging);
        return {{}, {}, ConfigCommandError::capacity};
    }
    AtomicWriteResult write{AtomicWriteResult::not_committed};
    try {
        write = impl->atomic_writer(staging / "config.json", copied_bytes);
    } catch (...) {
    }
    if (write == AtomicWriteResult::not_committed) {
        remove_tree_best_effort(staging);
        return {{}, {}, ConfigCommandError::internal_error};
    }
    if (stop.stop_requested()) {
        remove_tree_best_effort(staging);
        return {{}, {}, ConfigCommandError::cancelled};
    }
    std::filesystem::rename(staging, target, filesystem_error);
    if (filesystem_error) {
        remove_tree_best_effort(staging);
        return {{}, {}, path_kind(target) == PathKind::missing
                ? ConfigCommandError::internal_error : ConfigCommandError::conflict};
    }
    return {std::move(target_id), std::move(copy_name), ConfigCommandError::none};
} catch (...) {
    return {{}, {}, ConfigCommandError::internal_error};
}

ConfigRemoveResult FileResourceStore::remove_config(
    const std::string_view config_id, const std::stop_token stop) try
{
    const auto impl = impl_;
    if (stop.stop_requested()) return {ConfigCommandError::cancelled};
    const std::string id{config_id};
    if (!valid_resource_id(id, impl->limits.max_resource_id_bytes)) {
        return {ConfigCommandError::invalid_id};
    }
    std::unique_lock mutation_lock(impl->mutation_mutex);
    if (stop.stop_requested()) return {ConfigCommandError::cancelled};
    const auto target = impl->config_root / id;
    if (path_kind(target) == PathKind::missing) return {};
    if (const auto inspected = inspect_config_tree(target, stop);
        inspected != ConfigCommandError::none) {
        return {inspected};
    }
    const auto staging_root = impl->root / ".baas-config-staging";
    std::error_code error;
    if (path_kind(staging_root) == PathKind::missing) {
        std::filesystem::create_directory(staging_root, error);
    }
    if (error || path_kind(staging_root) != PathKind::directory) {
        return {ConfigCommandError::internal_error};
    }
    static std::atomic<std::uint64_t> removal_sequence{};
    const auto tombstone = staging_root /
        ("remove-" + std::to_string(removal_sequence.fetch_add(
                         1, std::memory_order_relaxed)));
    if (path_kind(tombstone) != PathKind::missing) {
        return {ConfigCommandError::conflict};
    }
    std::filesystem::rename(target, tombstone, error);
    if (error) {
        return {path_kind(target) == PathKind::missing
                ? ConfigCommandError::none : ConfigCommandError::internal_error};
    }
    {
        std::lock_guard state_lock(impl->state_mutex);
        impl->resources.erase({SyncResource::config, id});
        impl->resources.erase({SyncResource::event, id});
    }
    mutation_lock.unlock();
    remove_tree_best_effort(tombstone);
    return {};
} catch (...) {
    return {ConfigCommandError::internal_error};
}

}  // namespace baas::service::adapters
