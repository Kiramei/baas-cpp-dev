#include "service/app/AdbDiscoveryTriggerRegistration.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cwchar>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <optional>
#include <system_error>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <winioctl.h>
#include <winternl.h>
#endif

namespace baas::service::app {
namespace {

constexpr std::string_view cancelled_error = "cancelled";
constexpr std::string_view source_capacity_error =
    "adb_discovery_source_capacity";
constexpr std::string_view source_unavailable_error =
    "adb_discovery_source_unavailable";
constexpr std::string_view source_exception_error =
    "adb_discovery_source_exception";
constexpr std::string_view response_rejected_error =
    "adb_discovery_response_rejected";

[[nodiscard]] bool valid_limits(const AdbDiscoveryLimits& limits) noexcept
{
    return limits.max_processes != 0
        && limits.max_processes <= adb_discovery_hard_max_processes
        && limits.max_command_line_bytes != 0
        && limits.max_command_line_bytes
            <= adb_discovery_hard_max_command_line_bytes
        && limits.max_addresses != 0
        && limits.max_addresses <= adb_discovery_hard_max_addresses
        && limits.max_json_bytes >= sizeof(R"({"addresses":[]})") - 1
        && limits.max_json_bytes <= adb_discovery_hard_max_json_bytes
        && limits.scan_timeout > std::chrono::milliseconds::zero()
        && limits.scan_timeout <= adb_discovery_hard_max_scan_timeout
        && limits.vendor_query_timeout > std::chrono::milliseconds::zero()
        && limits.vendor_query_timeout
            <= adb_discovery_hard_max_vendor_query_timeout;
}

[[nodiscard]] char ascii_lower(const char value) noexcept
{
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] std::string lower_ascii(std::string_view value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const char byte : value) lowered.push_back(ascii_lower(byte));
    return lowered;
}

[[nodiscard]] bool ascii_iequals(
    const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) return false;
    }
    return true;
}

[[nodiscard]] std::string trim_ascii(std::string value)
{
    const auto whitespace = [](const char byte) {
        return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
    };
    const auto first = std::find_if_not(value.begin(), value.end(), whitespace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), whitespace)
                          .base();
    if (first >= last) return {};
    return {first, last};
}

[[nodiscard]] std::optional<std::string> environment_value(
    const char* name) noexcept
{
#if defined(_WIN32)
    char* value{};
    std::size_t length{};
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        std::free(value);
        return std::nullopt;
    }
    try {
        std::string copied(value, length == 0 ? 0 : length - 1);
        std::free(value);
        return copied;
    } catch (...) {
        std::free(value);
        return std::nullopt;
    }
#else
    const char* value = std::getenv(name);
    if (value == nullptr) return std::nullopt;
    try {
        return std::string{value};
    } catch (...) {
        return std::nullopt;
    }
#endif
}

[[nodiscard]] bool truthy_android_environment() noexcept
{
    const auto value = environment_value("BAAS_ANDROID");
    if (!value) return false;
    // Python deliberately does not trim BAAS_ANDROID.
    return ascii_iequals(*value, "1") || ascii_iequals(*value, "true")
        || ascii_iequals(*value, "yes") || ascii_iequals(*value, "on");
}

[[nodiscard]] std::optional<std::uint32_t> parse_decimal(
    const std::string_view value) noexcept
{
    if (value.empty()) return std::nullopt;
    std::uint32_t parsed{};
    const auto result = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::optional<std::string_view> word_after(
    const std::string_view command_line, const std::string_view marker) noexcept
{
    const auto marker_position = command_line.find(marker);
    if (marker_position == std::string_view::npos) return std::nullopt;
    auto begin = marker_position + marker.size();
    while (begin < command_line.size()
           && (command_line[begin] == ' ' || command_line[begin] == '\t')) {
        ++begin;
    }
    auto end = begin;
    while (end < command_line.size()) {
        const auto byte = command_line[end];
        if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n'
            || byte == '|' || byte == '"') {
            break;
        }
        ++end;
    }
    if (end == begin) return std::nullopt;
    return command_line.substr(begin, end - begin);
}

[[nodiscard]] std::optional<std::uint32_t> numeric_after(
    const std::string_view command_line, const std::string_view marker) noexcept
{
    const auto word = word_after(command_line, marker);
    return word ? parse_decimal(*word) : std::nullopt;
}

[[nodiscard]] std::optional<std::uint16_t> checked_port(
    const std::uint64_t value) noexcept
{
    if (value == 0 || value > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] std::optional<std::string> loopback_address(
    const std::optional<std::uint16_t> port)
{
    if (!port) return std::nullopt;
    return "127.0.0.1:" + std::to_string(*port);
}

[[nodiscard]] std::optional<std::string> process_address(
    const EmulatorProcessInfo& process,
    const BlueStacksPortResolver& bluestacks_port)
{
    const auto name = lower_ascii(process.executable_name);
    if (!process.command_line_available) return std::nullopt;
    if (name == "dnplayer.exe") {
        const auto instance = numeric_after(process.command_line, "index=")
                                  .value_or(0);
        return loopback_address(checked_port(
            5'555ULL + static_cast<std::uint64_t>(instance) * 2ULL));
    }
    if (name == "memu.exe") {
        const auto instance = numeric_after(process.command_line, "MEmu_")
                                  .value_or(0);
        return loopback_address(checked_port(
            21'503ULL + static_cast<std::uint64_t>(instance) * 10ULL));
    }
    if (name == "mumuplayer.exe" || name == "mumunxdevice.exe") {
        if (!process.resolved_adb_address.empty()) {
            return process.resolved_adb_address;
        }
        const auto instance = numeric_after(process.command_line, "-v")
                                  .value_or(0);
        if (instance > 1'536) return std::nullopt;
        // Python first asks MuMuManager and falls back to this documented
        // deterministic mapping when its JSON omits adb_host/adb_port.
        return loopback_address(checked_port(
            16'384ULL + static_cast<std::uint64_t>(instance) * 32ULL));
    }
    if (name == "nox.exe") {
        auto instance = numeric_after(process.command_line, "Nox_").value_or(1);
        const auto port = instance <= 1
            ? 62'001ULL : 62'023ULL + static_cast<std::uint64_t>(instance);
        return loopback_address(checked_port(port));
    }
    if (name == "hd-player.exe" && bluestacks_port) {
        const auto instance = word_after(process.command_line, "--instance");
        if (!instance) return std::nullopt;
        const auto path = lower_ascii(process.executable_path);
        if (path.find("bluestacks_nxt_cn") != std::string::npos) {
            return loopback_address(bluestacks_port(*instance, true));
        }
        if (path.find("bluestacks_nxt") != std::string::npos) {
            return loopback_address(bluestacks_port(*instance, false));
        }
        // A protected process may hide ExecutablePath. In that case neither
        // installation region is authoritative: accept one unambiguous result,
        // but fail closed when the same instance exists with different ports.
        const auto global_port = bluestacks_port(*instance, false);
        const auto china_port = bluestacks_port(*instance, true);
        if (global_port && china_port && global_port != china_port) {
            return std::nullopt;
        }
        return loopback_address(global_port ? global_port : china_port);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> encode_addresses_json(
    const std::vector<std::string>& addresses, const std::size_t max_bytes)
{
    std::size_t required = sizeof(R"({"addresses":[]})") - 1;
    for (const auto& address : addresses) {
        std::size_t escaped{};
        for (const unsigned char byte : address) {
            const std::size_t width = byte < 0x20U ? 6U
                : (byte == '"' || byte == '\\' ? 2U : 1U);
            if (escaped > max_bytes - std::min(max_bytes, width)) {
                return std::nullopt;
            }
            escaped += width;
        }
        const auto overhead = escaped + 3U;
        if (required > max_bytes - std::min(max_bytes, overhead)) {
            return std::nullopt;
        }
        required += overhead;
    }
    if (required > max_bytes) return std::nullopt;
    std::string output;
    output.reserve(required);
    output.append(R"({"addresses":[)");
    for (std::size_t index = 0; index < addresses.size(); ++index) {
        if (index != 0) output.push_back(',');
        output.push_back('"');
        // Production addresses are generated from a fixed ASCII host and a
        // numeric port. Android's configured serial is untrusted, so encode
        // the full JSON string grammar here rather than interpolating it.
        for (const unsigned char byte : addresses[index]) {
            switch (byte) {
                case '"': output.append(R"(\")"); break;
                case '\\': output.append(R"(\\)"); break;
                case '\b': output.append(R"(\b)"); break;
                case '\f': output.append(R"(\f)"); break;
                case '\n': output.append(R"(\n)"); break;
                case '\r': output.append(R"(\r)"); break;
                case '\t': output.append(R"(\t)"); break;
                default:
                    if (byte < 0x20U) {
                        constexpr std::array<char, 16> hex{
                            '0', '1', '2', '3', '4', '5', '6', '7',
                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
                        output.append("\\u00");
                        output.push_back(hex[(byte >> 4U) & 0x0fU]);
                        output.push_back(hex[byte & 0x0fU]);
                    } else {
                        output.push_back(static_cast<char>(byte));
                    }
            }
        }
        output.push_back('"');
    }
    output.append("]}");
    return output;
}

#if defined(_WIN32)

class WindowsHandle final {
public:
    WindowsHandle() = default;
    explicit WindowsHandle(HANDLE value) noexcept : value_(value) {}
    ~WindowsHandle() { reset(); }
    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;
    WindowsHandle(WindowsHandle&& other) noexcept
        : value_(std::exchange(other.value_, nullptr))
    {}
    WindowsHandle& operator=(WindowsHandle&& other) noexcept
    {
        if (this != &other) {
            reset();
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
    void reset(HANDLE value = nullptr) noexcept
    {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
        value_ = value;
    }

private:
    HANDLE value_{};
};

class ProcessAttributeList final {
public:
    ProcessAttributeList() = default;
    [[nodiscard]] bool initialize(HANDLE inherited_handle)
    {
        SIZE_T bytes{};
        static_cast<void>(InitializeProcThreadAttributeList(
            nullptr, 1, 0, &bytes));
        if (bytes == 0 || bytes > 64U * 1'024U) return false;
        storage_.resize(bytes);
        list_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) {
            list_ = nullptr;
            return false;
        }
        if (!UpdateProcThreadAttribute(
                list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                &inherited_handle, sizeof(inherited_handle), nullptr,
                nullptr)) {
            DeleteProcThreadAttributeList(list_);
            list_ = nullptr;
            return false;
        }
        return true;
    }
    ~ProcessAttributeList()
    {
        if (list_ != nullptr) DeleteProcThreadAttributeList(list_);
    }
    ProcessAttributeList(const ProcessAttributeList&) = delete;
    ProcessAttributeList& operator=(const ProcessAttributeList&) = delete;
    [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST get() const noexcept
    {
        return list_;
    }

private:
    std::vector<std::byte> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST list_{};
};

class RunningChildGuard final {
public:
    explicit RunningChildGuard(HANDLE process) noexcept : process_(process) {}
    ~RunningChildGuard()
    {
        if (!running_) return;
        static_cast<void>(TerminateProcess(process_, 1));
        static_cast<void>(WaitForSingleObject(process_, 1'000));
    }
    RunningChildGuard(const RunningChildGuard&) = delete;
    RunningChildGuard& operator=(const RunningChildGuard&) = delete;
    void mark_exited() noexcept { running_ = false; }

private:
    HANDLE process_{};
    bool running_{true};
};

[[nodiscard]] std::optional<std::wstring> registry_string(
    const wchar_t* subkey, const wchar_t* value_name)
{
    for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY raw_key{};
        if (RegOpenKeyExW(
                HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ | view, &raw_key)
            != ERROR_SUCCESS) {
            continue;
        }
        DWORD type{};
        DWORD bytes{};
        auto result = RegQueryValueExW(
            raw_key, value_name, nullptr, &type, nullptr, &bytes);
        if (result != ERROR_SUCCESS
            || (type != REG_SZ && type != REG_EXPAND_SZ)
            || bytes == 0 || bytes > 64U * 1'024U) {
            RegCloseKey(raw_key);
            continue;
        }
        std::vector<wchar_t> buffer(
            static_cast<std::size_t>(bytes) / sizeof(wchar_t) + 1, L'\0');
        result = RegQueryValueExW(
            raw_key, value_name, nullptr, &type,
            reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
        RegCloseKey(raw_key);
        if (result != ERROR_SUCCESS) continue;
        std::wstring value(buffer.data());
        if (type == REG_EXPAND_SZ) {
            const auto required = ExpandEnvironmentStringsW(
                value.c_str(), nullptr, 0);
            if (required != 0 && required <= 32'768) {
                std::wstring expanded(required, L'\0');
                if (ExpandEnvironmentStringsW(
                        value.c_str(), expanded.data(), required)
                    == required) {
                    if (!expanded.empty() && expanded.back() == L'\0') {
                        expanded.pop_back();
                    }
                    value = std::move(expanded);
                }
            }
        }
        return value;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::filesystem::path> mumu_manager_path()
{
    constexpr std::array<const wchar_t*, 2> keys{
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MuMuPlayer-12.0",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MuMuPlayer",
    };
    for (const auto* key : keys) {
        auto icon = registry_string(key, L"DisplayIcon");
        if (!icon) continue;
        while (!icon->empty() && (icon->front() == L'"' || icon->front() == L' ')) {
            icon->erase(icon->begin());
        }
        const auto quote = icon->find(L'"');
        if (quote != std::wstring::npos) icon->erase(quote);
        auto directory = std::filesystem::path{*icon}.parent_path();
        if (directory.empty()) continue;
        return directory / L"MuMuManager.exe";
    }
    return std::nullopt;
}

enum class LocalPathState : std::uint8_t { safe, missing, unsafe };

[[nodiscard]] std::optional<std::filesystem::path> local_reparse_target(
    const std::filesystem::path& link)
{
    WindowsHandle handle{CreateFileW(
        link.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (handle.get() == INVALID_HANDLE_VALUE) return std::nullopt;
    std::vector<std::byte> storage(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    DWORD returned{};
    if (!DeviceIoControl(
            handle.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0, storage.data(),
            static_cast<DWORD>(storage.size()), &returned, nullptr)
        || returned < 8U) {
        return std::nullopt;
    }
    const auto read_word = [&storage, returned](const std::size_t offset)
        -> std::optional<std::uint16_t> {
        if (offset > returned || sizeof(std::uint16_t) > returned - offset) {
            return std::nullopt;
        }
        std::uint16_t value{};
        std::memcpy(&value, storage.data() + offset, sizeof(value));
        return value;
    };
    DWORD tag{};
    std::memcpy(&tag, storage.data(), sizeof(tag));
    std::size_t path_buffer_offset{};
    bool relative{};
    if (tag == IO_REPARSE_TAG_MOUNT_POINT) {
        path_buffer_offset = 16U;
    } else if (tag == IO_REPARSE_TAG_SYMLINK) {
        path_buffer_offset = 20U;
        if (returned < path_buffer_offset) return std::nullopt;
        DWORD flags{};
        std::memcpy(&flags, storage.data() + 16U, sizeof(flags));
        relative = (flags & 1U) != 0;
    } else {
        return std::nullopt;
    }
    const auto substitute_offset = read_word(8U);
    const auto substitute_bytes = read_word(10U);
    if (!substitute_offset || !substitute_bytes
        || *substitute_bytes % sizeof(wchar_t) != 0) {
        return std::nullopt;
    }
    const auto absolute_offset = path_buffer_offset + *substitute_offset;
    if (absolute_offset > returned || *substitute_bytes > returned - absolute_offset) {
        return std::nullopt;
    }
    const auto characters = *substitute_bytes / sizeof(wchar_t);
    if (characters == 0 || characters > 32'768) return std::nullopt;
    const auto* text = reinterpret_cast<const wchar_t*>(
        storage.data() + absolute_offset);
    std::wstring target{text, characters};
    if (relative) return (link.parent_path() / target).lexically_normal();
    constexpr std::wstring_view nt_prefix = L"\\??\\";
    constexpr std::wstring_view win32_prefix = L"\\\\?\\";
    if (target.starts_with(nt_prefix)) {
        target.erase(0, nt_prefix.size());
    } else if (target.starts_with(win32_prefix)) {
        target.erase(0, win32_prefix.size());
    }
    if (target.starts_with(L"UNC\\")) {
        target.replace(0, 4, L"\\\\");
    }
    return std::filesystem::path{std::move(target)}.lexically_normal();
}

[[nodiscard]] LocalPathState local_regular_file_state_impl(
    const std::filesystem::path& input, const std::size_t depth)
{
    if (depth > 8) return LocalPathState::unsafe;
    const auto path = input.lexically_normal();
    const auto native = path.native();
    // Vendor registry values are never allowed to redirect service work to
    // UNC/device namespaces or relative paths. Restrict to a fixed local
    // drive before any open/read/CreateProcess call can touch the target.
    if (native.size() < 3
        || !((native[0] >= L'A' && native[0] <= L'Z')
             || (native[0] >= L'a' && native[0] <= L'z'))
        || native[1] != L':'
        || (native[2] != L'\\' && native[2] != L'/')) {
        return LocalPathState::unsafe;
    }
    const std::array<wchar_t, 4> root{
        native[0], L':', L'\\', L'\0'};
    if (GetDriveTypeW(root.data()) != DRIVE_FIXED) {
        return LocalPathState::unsafe;
    }

    constexpr DWORD unavailable_attributes = FILE_ATTRIBUTE_OFFLINE
        | FILE_ATTRIBUTE_RECALL_ON_OPEN | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
    std::vector<std::filesystem::path> components;
    for (const auto& component : path.relative_path()) {
        components.push_back(component);
    }
    auto current = path.root_path();
    for (std::size_t index = 0; index < components.size(); ++index) {
        current /= components[index];
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            return (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                ? LocalPathState::missing : LocalPathState::unsafe;
        }
        if ((attributes & unavailable_attributes) != 0) {
            return LocalPathState::unsafe;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            const auto target = local_reparse_target(current);
            if (!target) return LocalPathState::unsafe;
            auto resolved = *target;
            for (std::size_t tail = index + 1; tail < components.size(); ++tail) {
                resolved /= components[tail];
            }
            return local_regular_file_state_impl(resolved, depth + 1);
        }
        if (current == path && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return LocalPathState::unsafe;
        }
    }
    return LocalPathState::safe;
}

[[nodiscard]] LocalPathState local_regular_file_state(
    const std::filesystem::path& input) noexcept
{
    try {
        return local_regular_file_state_impl(input, 0);
    } catch (...) {
        return LocalPathState::unsafe;
    }
}

enum class MumuQueryState : std::uint8_t {
    resolved,
    fields_missing,
    cancelled,
    capacity,
    unsupported_path,
    unavailable,
    malformed,
};

struct MumuQueryResult {
    MumuQueryState state{MumuQueryState::unavailable};
    std::string address;
};

class BoundedJsonParser final {
public:
    explicit BoundedJsonParser(const std::string_view input) noexcept
        : input_(input)
    {}

    [[nodiscard]] bool parse_mumu_object(
        std::optional<std::string>& host,
        std::optional<std::string>& port_text) noexcept
    {
        skip_space();
        if (!consume('{')) return false;
        skip_space();
        if (consume('}')) {
            skip_space();
            return cursor_ == input_.size();
        }
        for (;;) {
            std::string key;
            if (!parse_string(&key)) return false;
            skip_space();
            if (!consume(':')) return false;
            skip_space();
            if (key == "adb_host") {
                if (!parse_target_string(host)) return false;
            } else if (key == "adb_port") {
                if (!parse_target_port(port_text)) return false;
            } else if (!skip_value(1)) {
                return false;
            }
            skip_space();
            if (consume('}')) break;
            if (!consume(',')) return false;
            skip_space();
        }
        skip_space();
        return cursor_ == input_.size();
    }

    [[nodiscard]] bool capacity_exhausted() const noexcept
    {
        return capacity_exhausted_;
    }

private:
    static constexpr std::size_t max_depth = 32;

    void skip_space() noexcept
    {
        while (cursor_ < input_.size()) {
            const char byte = input_[cursor_];
            if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n') {
                break;
            }
            ++cursor_;
        }
    }

    [[nodiscard]] bool consume(const char expected) noexcept
    {
        if (cursor_ >= input_.size() || input_[cursor_] != expected) {
            return false;
        }
        ++cursor_;
        return true;
    }

    [[nodiscard]] static std::optional<unsigned> hex_digit(
        const char byte) noexcept
    {
        if (byte >= '0' && byte <= '9') return byte - '0';
        if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10U;
        if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10U;
        return std::nullopt;
    }

    [[nodiscard]] bool parse_string(std::string* decoded) noexcept
    {
        if (!consume('"')) return false;
        try {
            if (decoded != nullptr) decoded->clear();
            while (cursor_ < input_.size()) {
                const unsigned char byte =
                    static_cast<unsigned char>(input_[cursor_++]);
                if (byte == '"') return true;
                if (byte < 0x20U) return false;
                if (byte != '\\') {
                    if (decoded != nullptr) {
                        decoded->push_back(static_cast<char>(byte));
                    }
                    continue;
                }
                if (cursor_ >= input_.size()) return false;
                const char escaped = input_[cursor_++];
                char replacement{};
                switch (escaped) {
                    case '"': replacement = '"'; break;
                    case '\\': replacement = '\\'; break;
                    case '/': replacement = '/'; break;
                    case 'b': replacement = '\b'; break;
                    case 'f': replacement = '\f'; break;
                    case 'n': replacement = '\n'; break;
                    case 'r': replacement = '\r'; break;
                    case 't': replacement = '\t'; break;
                    case 'u': {
                        if (input_.size() - cursor_ < 4) return false;
                        unsigned value{};
                        for (std::size_t index = 0; index < 4; ++index) {
                            const auto digit = hex_digit(input_[cursor_ + index]);
                            if (!digit) return false;
                            value = value * 16U + *digit;
                        }
                        cursor_ += 4;
                        if (decoded != nullptr) {
                            // ADB hosts are ASCII. Valid non-ASCII JSON remains
                            // accepted for fields that are only being skipped.
                            if (value > 0x7fU || value < 0x20U) return false;
                            decoded->push_back(static_cast<char>(value));
                        }
                        continue;
                    }
                    default: return false;
                }
                if (decoded != nullptr) decoded->push_back(replacement);
            }
        } catch (const std::bad_alloc&) {
            capacity_exhausted_ = true;
            return false;
        } catch (...) {
            return false;
        }
        return false;
    }

    [[nodiscard]] bool consume_literal(const std::string_view literal) noexcept
    {
        if (input_.substr(cursor_, literal.size()) != literal) return false;
        cursor_ += literal.size();
        return true;
    }

    [[nodiscard]] std::optional<std::string_view> parse_number() noexcept
    {
        const auto begin = cursor_;
        if (cursor_ < input_.size() && input_[cursor_] == '-') ++cursor_;
        if (cursor_ >= input_.size()) return std::nullopt;
        if (input_[cursor_] == '0') {
            ++cursor_;
        } else {
            if (input_[cursor_] < '1' || input_[cursor_] > '9') {
                return std::nullopt;
            }
            while (cursor_ < input_.size()
                   && input_[cursor_] >= '0' && input_[cursor_] <= '9') {
                ++cursor_;
            }
        }
        if (cursor_ < input_.size() && input_[cursor_] == '.') {
            ++cursor_;
            const auto fraction = cursor_;
            while (cursor_ < input_.size()
                   && input_[cursor_] >= '0' && input_[cursor_] <= '9') {
                ++cursor_;
            }
            if (fraction == cursor_) return std::nullopt;
        }
        if (cursor_ < input_.size()
            && (input_[cursor_] == 'e' || input_[cursor_] == 'E')) {
            ++cursor_;
            if (cursor_ < input_.size()
                && (input_[cursor_] == '+' || input_[cursor_] == '-')) {
                ++cursor_;
            }
            const auto exponent = cursor_;
            while (cursor_ < input_.size()
                   && input_[cursor_] >= '0' && input_[cursor_] <= '9') {
                ++cursor_;
            }
            if (exponent == cursor_) return std::nullopt;
        }
        return input_.substr(begin, cursor_ - begin);
    }

    [[nodiscard]] bool skip_value(const std::size_t depth) noexcept
    {
        if (depth > max_depth || cursor_ >= input_.size()) return false;
        if (input_[cursor_] == '"') return parse_string(nullptr);
        if (input_[cursor_] == '{') {
            ++cursor_;
            skip_space();
            if (consume('}')) return true;
            for (;;) {
                if (!parse_string(nullptr)) return false;
                skip_space();
                if (!consume(':')) return false;
                skip_space();
                if (!skip_value(depth + 1)) return false;
                skip_space();
                if (consume('}')) return true;
                if (!consume(',')) return false;
                skip_space();
            }
        }
        if (input_[cursor_] == '[') {
            ++cursor_;
            skip_space();
            if (consume(']')) return true;
            for (;;) {
                if (!skip_value(depth + 1)) return false;
                skip_space();
                if (consume(']')) return true;
                if (!consume(',')) return false;
                skip_space();
            }
        }
        return consume_literal("true") || consume_literal("false")
            || consume_literal("null") || parse_number().has_value();
    }

    [[nodiscard]] bool parse_target_string(
        std::optional<std::string>& value) noexcept
    {
        if (consume_literal("null")) {
            value.reset();
            return true;
        }
        std::string parsed;
        if (!parse_string(&parsed)) return false;
        value = std::move(parsed);
        return true;
    }

    [[nodiscard]] bool parse_target_port(
        std::optional<std::string>& value) noexcept
    {
        if (consume_literal("null")) {
            value.reset();
            return true;
        }
        if (cursor_ < input_.size() && input_[cursor_] == '"') {
            return parse_target_string(value);
        }
        const auto number = parse_number();
        if (!number || number->find_first_of(".eE-") != std::string_view::npos) {
            return false;
        }
        try {
            value = std::string{*number};
            return true;
        } catch (const std::bad_alloc&) {
            capacity_exhausted_ = true;
            return false;
        } catch (...) {
            return false;
        }
    }

    std::string_view input_;
    std::size_t cursor_{};
    bool capacity_exhausted_{};
};

[[nodiscard]] MumuQueryResult parse_mumu_adb_json(
    const std::string_view output) noexcept
{
    std::optional<std::string> host;
    std::optional<std::string> port_text;
    BoundedJsonParser parser(output);
    if (!parser.parse_mumu_object(host, port_text)) {
        return {parser.capacity_exhausted() ? MumuQueryState::capacity
                                           : MumuQueryState::malformed,
                {}};
    }
    // Python falls back to the documented formula only after json.loads()
    // succeeded and either adb_host or adb_port is absent/None.
    if (!host || !port_text) return {MumuQueryState::fields_missing, {}};
    const auto port = parse_decimal(*port_text);
    const auto valid_port = port ? checked_port(*port) : std::nullopt;
    if (host->empty() || !valid_port) {
        return {MumuQueryState::malformed, {}};
    }
    try {
        return {MumuQueryState::resolved,
                *host + ":" + std::to_string(*valid_port)};
    } catch (const std::bad_alloc&) {
        return {MumuQueryState::capacity, {}};
    } catch (...) {
        return {MumuQueryState::malformed, {}};
    }
}

[[nodiscard]] MumuQueryResult run_mumu_adb_query(
    const std::filesystem::path& manager, const std::uint32_t instance,
    const std::stop_token stop,
    const std::chrono::steady_clock::time_point deadline)
{
    if (local_regular_file_state(manager) != LocalPathState::safe) {
        return {MumuQueryState::unsupported_path, {}};
    }
    SECURITY_ATTRIBUTES security{
        sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE raw_read{};
    HANDLE raw_write{};
    if (!CreatePipe(&raw_read, &raw_write, &security, 0)) {
        return {MumuQueryState::unavailable, {}};
    }
    WindowsHandle read_pipe{raw_read};
    WindowsHandle write_pipe{raw_write};
    if (!SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0)) {
        return {MumuQueryState::unavailable, {}};
    }
    ProcessAttributeList attributes;
    if (!attributes.initialize(write_pipe.get())) {
        return {MumuQueryState::unavailable, {}};
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = write_pipe.get();
    startup.StartupInfo.hStdError = write_pipe.get();
    startup.StartupInfo.hStdInput = nullptr;
    startup.lpAttributeList = attributes.get();
    PROCESS_INFORMATION process_info{};
    std::wstring command = L"\"" + manager.wstring() + L"\" adb -v "
        + std::to_wstring(instance);
    if (!CreateProcessW(
            manager.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
            manager.parent_path().c_str(), &startup.StartupInfo,
            &process_info)) {
        return {MumuQueryState::unavailable, {}};
    }
    WindowsHandle process{process_info.hProcess};
    WindowsHandle thread{process_info.hThread};
    RunningChildGuard child{process.get()};
    write_pipe.reset();

    constexpr std::size_t max_output_bytes = 64U * 1'024U;
    std::string output;
    output.reserve(4'096);
    bool exited = false;
    for (;;) {
        DWORD available{};
        if (!PeekNamedPipe(
                read_pipe.get(), nullptr, 0, nullptr, &available, nullptr)) {
            break;
        }
        while (available != 0) {
            if (output.size() >= max_output_bytes) {
                return {MumuQueryState::capacity, {}};
            }
            std::array<char, 4'096> chunk{};
            const auto wanted = static_cast<DWORD>(std::min<std::size_t>(
                chunk.size(), std::min<std::size_t>(
                                  available, max_output_bytes - output.size())));
            DWORD read{};
            if (!ReadFile(read_pipe.get(), chunk.data(), wanted, &read, nullptr)
                || read == 0) {
                available = 0;
                break;
            }
            output.append(chunk.data(), read);
            available -= std::min(available, read);
        }
        const auto wait = WaitForSingleObject(process.get(), exited ? 0 : 20);
        if (wait == WAIT_OBJECT_0) {
            if (exited) break;
            exited = true;
            child.mark_exited();
            continue;
        }
        if (wait == WAIT_FAILED) break;
        if (stop.stop_requested()
            || std::chrono::steady_clock::now() >= deadline) {
            return {stop.stop_requested() ? MumuQueryState::cancelled
                                          : MumuQueryState::unavailable,
                    {}};
        }
    }
    if (stop.stop_requested()) return {MumuQueryState::cancelled, {}};
    if (!exited) return {MumuQueryState::unavailable, {}};
    DWORD exit_code{};
    if (!GetExitCodeProcess(process.get(), &exit_code) || exit_code != 0) {
        return {MumuQueryState::unavailable, {}};
    }
    return parse_mumu_adb_json(output);
}

[[nodiscard]] std::string utf8_from_wide(
    const wchar_t* value, const std::size_t length)
{
    if (value == nullptr || length == 0) return {};
    const auto bounded = static_cast<int>(std::min<std::size_t>(
        length, static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const int bytes = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, bounded, nullptr, 0, nullptr,
        nullptr);
    if (bytes <= 0) return {};
    std::string output(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value, bounded, output.data(), bytes,
            nullptr, nullptr)
        != bytes) {
        return {};
    }
    return output;
}

enum class ProcessInspection : std::uint8_t { success, capacity };

[[nodiscard]] bool supported_emulator_process(const wchar_t* name) noexcept
{
    return _wcsicmp(name, L"HD-Player.exe") == 0
        || _wcsicmp(name, L"nox.exe") == 0
        || _wcsicmp(name, L"MuMuPlayer.exe") == 0
        || _wcsicmp(name, L"MuMuNxDevice.exe") == 0
        || _wcsicmp(name, L"dnplayer.exe") == 0
        || _wcsicmp(name, L"MEmu.exe") == 0;
}

using NtQueryInformationProcessFunction = LONG(NTAPI*)(
    HANDLE, ULONG, PVOID, ULONG, PULONG);

[[nodiscard]] NtQueryInformationProcessFunction nt_query_information_process()
    noexcept
{
    static const auto function = reinterpret_cast<
        NtQueryInformationProcessFunction>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    return function;
}

[[nodiscard]] ProcessInspection populate_process_details(
    EmulatorProcessInfo& item, const AdbDiscoveryLimits& limits)
{
    WindowsHandle process{OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
        static_cast<DWORD>(item.pid))};
    if (process.get() == nullptr) return ProcessInspection::success;

    std::array<wchar_t, 32'768> path{};
    DWORD path_size = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(
            process.get(), 0, path.data(), &path_size)) {
        item.executable_path = utf8_from_wide(path.data(), path_size);
        if (item.executable_path.size() > limits.max_command_line_bytes) {
            return ProcessInspection::capacity;
        }
    }

    const auto query = nt_query_information_process();
    if (query == nullptr) return ProcessInspection::success;
    constexpr ULONG process_command_line_information = 60;
    ULONG required{};
    static_cast<void>(query(
        process.get(), process_command_line_information, nullptr, 0,
        &required));
    constexpr ULONG max_native_command_line_bytes = 256U * 1'024U;
    if (required == 0) return ProcessInspection::success;
    if (required > max_native_command_line_bytes) {
        return ProcessInspection::capacity;
    }
    std::vector<std::byte> buffer(required);
    const auto status = query(
        process.get(), process_command_line_information, buffer.data(),
        required, &required);
    if (status < 0 || buffer.size() < sizeof(UNICODE_STRING)) {
        return ProcessInspection::success;
    }
    const auto* command = reinterpret_cast<const UNICODE_STRING*>(buffer.data());
    if (command->Length == 0) {
        item.command_line.clear();
        item.command_line_available = true;
        return ProcessInspection::success;
    }
    const auto buffer_begin = reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto buffer_end = buffer_begin + buffer.size();
    const auto text_begin = reinterpret_cast<std::uintptr_t>(command->Buffer);
    if (command->Buffer == nullptr || command->Length % sizeof(wchar_t) != 0
        || text_begin < buffer_begin || text_begin > buffer_end
        || command->Length > buffer_end - text_begin) {
        return ProcessInspection::success;
    }
    item.command_line = utf8_from_wide(
        command->Buffer, command->Length / sizeof(wchar_t));
    if (item.command_line.size() > limits.max_command_line_bytes) {
        return ProcessInspection::capacity;
    }
    item.command_line_available = true;
    return ProcessInspection::success;
}

class BlueStacksConfigCache final {
public:
    BlueStacksConfigCache(
        const std::stop_token stop, const AdbDiscoveryLimits& limits,
        const std::chrono::steady_clock::time_point deadline) noexcept
        : stop_(stop), limits_(limits), deadline_(deadline)
    {}

    [[nodiscard]] std::optional<std::uint16_t> resolve(
        const std::string_view instance, const bool china)
    {
        if (instance.size() > limits_.max_command_line_bytes) {
            set_error(AdbDiscoverySourceError::capacity);
            return std::nullopt;
        }
        auto& region = regions_[china ? 1U : 0U];
        if (!region.loaded && !load(region, china)) return std::nullopt;
        const auto found = region.ports.find(std::string{instance});
        return found == region.ports.end()
            ? std::nullopt : std::optional<std::uint16_t>{found->second};
    }

    [[nodiscard]] AdbDiscoverySourceError error() const noexcept
    {
        return error_;
    }

private:
    struct Region {
        bool loaded{};
        std::unordered_map<std::string, std::uint16_t> ports;
    };

    void set_error(const AdbDiscoverySourceError error) noexcept
    {
        if (error_ == AdbDiscoverySourceError::none) error_ = error;
    }

    [[nodiscard]] bool observe_boundary() noexcept
    {
        if (stop_.stop_requested()) {
            set_error(AdbDiscoverySourceError::cancelled);
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline_) {
            set_error(AdbDiscoverySourceError::unavailable);
            return false;
        }
        return true;
    }

    [[nodiscard]] static std::string_view trim_view(
        std::string_view value) noexcept
    {
        while (!value.empty()
               && (value.front() == ' ' || value.front() == '\t'
                   || value.front() == '\r')) {
            value.remove_prefix(1);
        }
        while (!value.empty()
               && (value.back() == ' ' || value.back() == '\t'
                   || value.back() == '\r')) {
            value.remove_suffix(1);
        }
        return value;
    }

    [[nodiscard]] bool load(Region& region, const bool china)
    {
        region.loaded = true;
        if (!observe_boundary()) return false;
        const wchar_t* subkey = china
            ? L"SOFTWARE\\BlueStacks_nxt_cn" : L"SOFTWARE\\BlueStacks_nxt";
        const auto directory = registry_string(subkey, L"UserDefinedDir");
        if (!directory || directory->empty()) return true;
        const auto path = std::filesystem::path{*directory} / L"BlueStacks.conf";
        const auto path_state = local_regular_file_state(path);
        if (path_state == LocalPathState::missing) return true;
        if (path_state != LocalPathState::safe) {
            // One unsupported vendor installation must not suppress safe
            // LDPlayer/MEmu/etc. candidates discovered in the same scan.
            return true;
        }
        std::error_code file_error;
        const auto declared_size = std::filesystem::file_size(path, file_error);
        if (file_error) return true;
        if (declared_size > limits_.max_json_bytes) {
            set_error(AdbDiscoverySourceError::capacity);
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) return true;
        std::string content;
        content.reserve(static_cast<std::size_t>(declared_size));
        std::array<char, 8U * 1'024U> chunk{};
        while (input) {
            if (!observe_boundary()) return false;
            input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto read = input.gcount();
            if (read <= 0) break;
            const auto bytes = static_cast<std::size_t>(read);
            if (bytes > limits_.max_json_bytes - content.size()) {
                set_error(AdbDiscoverySourceError::capacity);
                return false;
            }
            content.append(chunk.data(), bytes);
        }
        if (input.bad()) {
            set_error(AdbDiscoverySourceError::unavailable);
            return false;
        }

        constexpr std::string_view prefix = "bst.instance.";
        constexpr std::string_view suffix = ".status.adb_port";
        std::size_t cursor{};
        while (cursor < content.size()) {
            if (!observe_boundary()) return false;
            auto end = content.find('\n', cursor);
            if (end == std::string::npos) end = content.size();
            auto line = trim_view(
                std::string_view{content}.substr(cursor, end - cursor));
            const auto equal = line.find('=');
            if (equal != std::string_view::npos) {
                const auto key = trim_view(line.substr(0, equal));
                if (key.size() > prefix.size() + suffix.size()
                    && key.starts_with(prefix) && key.ends_with(suffix)) {
                    const auto name = key.substr(
                        prefix.size(), key.size() - prefix.size() - suffix.size());
                    auto value = trim_view(line.substr(equal + 1));
                    if (value.size() >= 2 && value.front() == '"') {
                        const auto quote = value.find('"', 1);
                        if (quote != std::string_view::npos
                            && trim_view(value.substr(quote + 1)).empty()) {
                            const auto port = parse_decimal(value.substr(1, quote - 1));
                            const auto checked = port ? checked_port(*port) : std::nullopt;
                            if (checked) {
                                region.ports.insert_or_assign(
                                    std::string{name}, *checked);
                            }
                        }
                    }
                }
            }
            if (end == content.size()) break;
            cursor = end + 1;
        }
        return true;
    }

    std::stop_token stop_;
    const AdbDiscoveryLimits& limits_;
    std::chrono::steady_clock::time_point deadline_;
    std::array<Region, 2> regions_;
    AdbDiscoverySourceError error_{AdbDiscoverySourceError::none};
};

[[nodiscard]] AdbDiscoverySourceResult windows_processes(
    const std::stop_token stop, const AdbDiscoveryLimits& limits)
{
    const auto scan_deadline = std::chrono::steady_clock::now()
        + limits.scan_timeout;
    WindowsHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return {{}, AdbDiscoverySourceError::unavailable};
    }
    std::vector<EmulatorProcessInfo> processes;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        return {{}, AdbDiscoverySourceError::unavailable};
    }
    do {
        if (stop.stop_requested()) {
            return {{}, AdbDiscoverySourceError::cancelled};
        }
        if (std::chrono::steady_clock::now() >= scan_deadline) {
            return {{}, AdbDiscoverySourceError::unavailable};
        }
        if (!supported_emulator_process(entry.szExeFile)) continue;
        if (processes.size() >= limits.max_processes) {
            return {{}, AdbDiscoverySourceError::capacity};
        }
        EmulatorProcessInfo item;
        item.pid = entry.th32ProcessID;
        item.command_line_available = false;
        item.executable_name = utf8_from_wide(
            entry.szExeFile, std::wcslen(entry.szExeFile));
        if (populate_process_details(item, limits)
            == ProcessInspection::capacity) {
            return {{}, AdbDiscoverySourceError::capacity};
        }
        processes.emplace_back(std::move(item));
    } while (Process32NextW(snapshot.get(), &entry));
    std::sort(processes.begin(), processes.end(), [](const auto& left, const auto& right) {
        return left.pid < right.pid;
    });
    const auto has_mumu = std::any_of(
        processes.begin(), processes.end(), [](const auto& process) {
            const auto name = lower_ascii(process.executable_name);
            return process.command_line_available
                && (name == "mumuplayer.exe" || name == "mumunxdevice.exe");
        });
    if (has_mumu) {
        const auto manager = mumu_manager_path();
        if (!manager) return {{}, AdbDiscoverySourceError::unavailable};
        for (auto& process : processes) {
            const auto name = lower_ascii(process.executable_name);
            if (name != "mumuplayer.exe" && name != "mumunxdevice.exe") continue;
            if (!process.command_line_available) continue;
            const auto instance = numeric_after(process.command_line, "-v")
                                      .value_or(0);
            const auto query_deadline = std::min(
                scan_deadline,
                std::chrono::steady_clock::now()
                    + limits.vendor_query_timeout);
            auto query = run_mumu_adb_query(
                *manager, instance, stop, query_deadline);
            if (query.state == MumuQueryState::resolved) {
                process.resolved_adb_address = std::move(query.address);
            } else if (query.state == MumuQueryState::cancelled) {
                return {{}, AdbDiscoverySourceError::cancelled};
            } else if (query.state == MumuQueryState::capacity) {
                return {{}, AdbDiscoverySourceError::capacity};
            } else if (query.state == MumuQueryState::unsupported_path) {
                process.command_line_available = false;
                continue;
            } else if (query.state != MumuQueryState::fields_missing) {
                return {{}, AdbDiscoverySourceError::unavailable};
            }
            if (std::chrono::steady_clock::now() >= scan_deadline) {
                return {{}, AdbDiscoverySourceError::unavailable};
            }
        }
    }
    BlueStacksConfigCache bluestacks{stop, limits, scan_deadline};
    auto result = discover_emulator_adb_addresses(
        processes,
        [&bluestacks](const std::string_view instance, const bool china) {
            return bluestacks.resolve(instance, china);
        },
        stop, limits);
    if (bluestacks.error() != AdbDiscoverySourceError::none) {
        return {{}, bluestacks.error()};
    }
    return result;
}

#endif

class ProductionAdbDiscoverySource final : public AdbDiscoverySource {
public:
    explicit ProductionAdbDiscoverySource(AdbDiscoveryLimits limits)
        : limits_(limits)
    {}

    [[nodiscard]] AdbDiscoverySourceResult detect(
        const std::stop_token stop) override
    {
        if (stop.stop_requested()) return {{}, AdbDiscoverySourceError::cancelled};
        if (truthy_android_environment()) {
            std::vector<std::string> candidates;
            if (auto configured = environment_value("BAAS_ANDROID_ADB_SERIAL")) {
                auto value = trim_ascii(std::move(*configured));
                if (value.size() > limits_.max_command_line_bytes) {
                    return {{}, AdbDiscoverySourceError::capacity};
                }
                if (!value.empty()) candidates.emplace_back(std::move(value));
            }
            candidates.emplace_back("127.0.0.1:5555");
            candidates.emplace_back("localhost:5555");
            std::vector<std::string> unique;
            unique.reserve(candidates.size());
            for (auto& candidate : candidates) {
                if (std::find(unique.begin(), unique.end(), candidate) == unique.end()) {
                    unique.emplace_back(std::move(candidate));
                }
            }
            return {std::move(unique), AdbDiscoverySourceError::none};
        }
#if defined(_WIN32)
        return windows_processes(stop, limits_);
#else
        // Python recognizes Windows emulator executable names. A native
        // non-Windows host therefore has no supported vendor process mapping.
        return {{}, AdbDiscoverySourceError::none};
#endif
    }

private:
    AdbDiscoveryLimits limits_;
};

class CallbackAdbDiscoverySource final : public AdbDiscoverySource {
public:
    explicit CallbackAdbDiscoverySource(AdbDiscoverySourceCallback callback)
        : callback_(std::move(callback))
    {}
    [[nodiscard]] AdbDiscoverySourceResult detect(
        const std::stop_token stop) override
    {
        return callback_(stop);
    }

private:
    AdbDiscoverySourceCallback callback_;
};

[[nodiscard]] trigger::TriggerHandler make_handler(
    std::shared_ptr<AdbDiscoverySource> source, const AdbDiscoveryLimits limits)
{
    return [source = std::move(source), limits](
               const trigger::AdmittedTriggerRequest&,
               trigger::TriggerResponseSink& sink,
               const std::stop_token stop) {
        if (stop.stop_requested()) {
            static_cast<void>(sink.cancelled(std::string{cancelled_error}));
            return;
        }
        AdbDiscoverySourceResult result;
        try {
            result = source->detect(stop);
        } catch (const std::bad_alloc&) {
            static_cast<void>(sink.error(std::string{source_capacity_error}));
            return;
        } catch (...) {
            static_cast<void>(sink.error(std::string{source_exception_error}));
            return;
        }
        if (stop.stop_requested()
            || result.error == AdbDiscoverySourceError::cancelled) {
            static_cast<void>(sink.cancelled(std::string{cancelled_error}));
            return;
        }
        if (result.error == AdbDiscoverySourceError::capacity
            || result.addresses.size() > limits.max_addresses) {
            static_cast<void>(sink.error(std::string{source_capacity_error}));
            return;
        }
        if (result.error != AdbDiscoverySourceError::none) {
            static_cast<void>(sink.error(std::string{source_unavailable_error}));
            return;
        }
        try {
            auto data_json = encode_addresses_json(
                result.addresses, limits.max_json_bytes);
            if (!data_json) {
                static_cast<void>(sink.error(std::string{source_capacity_error}));
                return;
            }
            const auto staged = sink.success(std::move(*data_json));
            if (!staged) {
                static_cast<void>(sink.error(std::string{response_rejected_error}));
            }
        } catch (const std::bad_alloc&) {
            static_cast<void>(sink.error(std::string{source_capacity_error}));
        } catch (...) {
            static_cast<void>(sink.error(std::string{source_exception_error}));
        }
    };
}

}  // namespace

#if defined(_WIN32) && defined(BAAS_ADB_DISCOVERY_TEST_HOOKS)
MumuAdbJsonTestResult parse_mumu_adb_json_for_test(
    const std::string_view json) noexcept
{
    auto parsed = parse_mumu_adb_json(json);
    switch (parsed.state) {
        case MumuQueryState::resolved:
            return {MumuAdbJsonTestState::resolved, std::move(parsed.address)};
        case MumuQueryState::fields_missing:
            return {MumuAdbJsonTestState::fields_missing, {}};
        case MumuQueryState::capacity:
            return {MumuAdbJsonTestState::capacity, {}};
        case MumuQueryState::cancelled:
        case MumuQueryState::unsupported_path:
        case MumuQueryState::unavailable:
        case MumuQueryState::malformed:
            return {MumuAdbJsonTestState::malformed, {}};
    }
    return {MumuAdbJsonTestState::malformed, {}};
}

bool local_vendor_file_is_safe_for_test(
    const std::wstring_view path) noexcept
{
    try {
        return local_regular_file_state(std::filesystem::path{path})
            == LocalPathState::safe;
    } catch (...) {
        return false;
    }
}
#endif

std::string_view adb_discovery_source_error_name(
    const AdbDiscoverySourceError error) noexcept
{
    using enum AdbDiscoverySourceError;
    switch (error) {
        case none: return "none";
        case cancelled: return "cancelled";
        case capacity: return "capacity";
        case unavailable: return "unavailable";
    }
    return "unknown";
}

AdbDiscoverySourceResult discover_emulator_adb_addresses(
    const std::vector<EmulatorProcessInfo>& processes,
    BlueStacksPortResolver bluestacks_port,
    const std::stop_token stop,
    const AdbDiscoveryLimits limits) noexcept
{
    if (!valid_limits(limits)) return {{}, AdbDiscoverySourceError::capacity};
    if (processes.size() > limits.max_processes) {
        return {{}, AdbDiscoverySourceError::capacity};
    }
    try {
        std::vector<std::string> addresses;
        addresses.reserve(std::min(processes.size(), limits.max_addresses));
        std::unordered_set<std::string> seen;
        seen.reserve(std::min(processes.size(), limits.max_addresses));
        // Python groups by simulator type in first-seen type order, while
        // retaining PID order within a type.
        constexpr std::array<std::string_view, 5> canonical_types{
            "bluestacks", "nox", "mumu", "ldplayer", "memu"};
        std::vector<std::string_view> type_order;
        std::array<bool, canonical_types.size()> present{};
        auto type_index = [](const std::string_view name) -> std::optional<std::size_t> {
            const auto lowered = lower_ascii(name);
            if (lowered == "hd-player.exe") return 0;
            if (lowered == "nox.exe") return 1;
            if (lowered == "mumuplayer.exe" || lowered == "mumunxdevice.exe") return 2;
            if (lowered == "dnplayer.exe") return 3;
            if (lowered == "memu.exe") return 4;
            return std::nullopt;
        };
        for (const auto& process : processes) {
            if (stop.stop_requested()) return {{}, AdbDiscoverySourceError::cancelled};
            if (process.command_line.size() > limits.max_command_line_bytes
                || process.executable_path.size() > limits.max_command_line_bytes
                || process.resolved_adb_address.size()
                    > limits.max_command_line_bytes) {
                return {{}, AdbDiscoverySourceError::capacity};
            }
            const auto index = type_index(process.executable_name);
            if (index && !present[*index]) {
                present[*index] = true;
                type_order.push_back(canonical_types[*index]);
            }
        }
        for (const auto type : type_order) {
            for (const auto& process : processes) {
                if (stop.stop_requested()) {
                    return {{}, AdbDiscoverySourceError::cancelled};
                }
                const auto index = type_index(process.executable_name);
                if (!index || canonical_types[*index] != type) continue;
                auto address = process_address(process, bluestacks_port);
                if (!address || !seen.emplace(*address).second) continue;
                if (addresses.size() >= limits.max_addresses) {
                    return {{}, AdbDiscoverySourceError::capacity};
                }
                addresses.emplace_back(std::move(*address));
            }
        }
        return {std::move(addresses), AdbDiscoverySourceError::none};
    } catch (const std::bad_alloc&) {
        return {{}, AdbDiscoverySourceError::capacity};
    } catch (...) {
        return {{}, AdbDiscoverySourceError::unavailable};
    }
}

std::shared_ptr<AdbDiscoverySource> make_production_adb_discovery_source(
    const AdbDiscoveryLimits limits) noexcept
{
    if (!valid_limits(limits)) return {};
    try {
        return std::make_shared<ProductionAdbDiscoverySource>(limits);
    } catch (...) {
        return {};
    }
}

std::string_view adb_discovery_trigger_registration_error_name(
    const AdbDiscoveryTriggerRegistrationError error) noexcept
{
    using enum AdbDiscoveryTriggerRegistrationError;
    switch (error) {
        case none: return "none";
        case missing_source: return "missing_source";
        case empty_callback: return "empty_callback";
        case invalid_limits: return "invalid_limits";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

AdbDiscoveryTriggerRegistrationResult make_adb_discovery_trigger_registration(
    std::shared_ptr<AdbDiscoverySource> source,
    const AdbDiscoveryLimits limits) noexcept
{
    if (!source) {
        return {std::nullopt,
                AdbDiscoveryTriggerRegistrationError::missing_source};
    }
    if (!valid_limits(limits)) {
        return {std::nullopt,
                AdbDiscoveryTriggerRegistrationError::invalid_limits};
    }
    try {
        trigger::TriggerHandlerRegistration registration;
        registration.descriptor_name = "detect_adb";
        registration.handler = make_handler(std::move(source), limits);
        return {std::move(registration),
                AdbDiscoveryTriggerRegistrationError::none};
    } catch (...) {
        return {std::nullopt,
                AdbDiscoveryTriggerRegistrationError::resource_exhausted};
    }
}

AdbDiscoveryTriggerRegistrationResult make_adb_discovery_trigger_registration(
    AdbDiscoverySourceCallback callback,
    const AdbDiscoveryLimits limits) noexcept
{
    if (!callback) {
        return {std::nullopt,
                AdbDiscoveryTriggerRegistrationError::empty_callback};
    }
    try {
        return make_adb_discovery_trigger_registration(
            std::make_shared<CallbackAdbDiscoverySource>(std::move(callback)),
            limits);
    } catch (...) {
        return {std::nullopt,
                AdbDiscoveryTriggerRegistrationError::resource_exhausted};
    }
}

}  // namespace baas::service::app
