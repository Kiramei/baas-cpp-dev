#include "service/auth/AuthOwner.h"

#include "service/auth/CanonicalJson.h"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace baas::service::auth {
namespace {

constexpr std::string_view fixed_signing_seed_b64 =
    "SWWTs4OxttQrw_o89jtIM1pj8lhJEomLzfUEbsHjJS4=";

[[nodiscard]] std::span<const std::byte> bytes_of(const std::string_view value) noexcept
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] std::string_view text_of(const std::span<const std::byte> value) noexcept
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

template <typename T>
[[nodiscard]] AuthResult<T> result_error(const AuthError error) noexcept
{
    return {std::nullopt, error};
}

[[nodiscard]] AuthStatus status(const AuthError error = AuthError::none) noexcept
{
    return {error};
}

[[nodiscard]] AuthError crypto_to_auth(const CryptoError error) noexcept
{
    if (error == CryptoError::resource_exhausted) return AuthError::password_derivation_failed;
    return AuthError::crypto_failure;
}

void wipe(PublicBytes& value) noexcept
{
    secure_zero(value);
    value.clear();
}

void wipe(std::string& value) noexcept
{
    secure_zero(std::as_writable_bytes(std::span{value.data(), value.size()}));
    value.clear();
}

class WipingString final {
public:
    ~WipingString() { wipe(value); }
    std::string value;
};

[[nodiscard]] std::filesystem::path path_for(
    const std::filesystem::path& config, const AuthFile file)
{
    switch (file) {
        case AuthFile::password_state: return config / "service_auth.json";
        case AuthFile::ticket_key: return config / "service_ticket.key";
        case AuthFile::remembered_logins: return config / "service_remembered_logins.json";
        case AuthFile::signing_key: return config / "service_signing_key.bin";
    }
    return config / "invalid";
}

[[nodiscard]] const char* filename_for(const AuthFile file) noexcept
{
    switch (file) {
        case AuthFile::password_state: return "service_auth.json";
        case AuthFile::ticket_key: return "service_ticket.key";
        case AuthFile::remembered_logins: return "service_remembered_logins.json";
        case AuthFile::signing_key: return "service_signing_key.bin";
    }
    return "invalid";
}

class FileAuthStorage final : public AuthStorage {
public:
    explicit FileAuthStorage(std::filesystem::path project_root)
        : config_(std::move(project_root) / "config")
    {
        initialize();
    }

    ~FileAuthStorage() override
    {
#if defined(_WIN32)
        if (lock_handle_ != INVALID_HANDLE_VALUE) {
            UnlockFileEx(lock_handle_, 0, MAXDWORD, MAXDWORD, &lock_overlapped_);
            CloseHandle(lock_handle_);
        }
        if (config_handle_ != INVALID_HANDLE_VALUE) CloseHandle(config_handle_);
        if (security_descriptor_ != nullptr) LocalFree(security_descriptor_);
#else
        if (lock_fd_ >= 0) {
            static_cast<void>(flock(lock_fd_, LOCK_UN));
            close(lock_fd_);
        }
        if (config_fd_ >= 0) close(config_fd_);
#endif
    }

    [[nodiscard]] StorageReadResult read(
        const AuthFile file, const std::size_t maximum_bytes) noexcept override
    {
        try {
            if (!ready_) return {StorageReadStatus::failure, {}};
#if defined(_WIN32)
            const auto path = path_for(config_, file);
            HANDLE handle = CreateFileW(
                path.c_str(), GENERIC_READ | READ_CONTROL | WRITE_DAC,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                return GetLastError() == ERROR_FILE_NOT_FOUND
                    ? StorageReadResult{} : StorageReadResult{StorageReadStatus::failure, {}};
            }
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            FILE_STANDARD_INFO standard{};
            const bool valid = GetFileInformationByHandleEx(
                    handle, FileAttributeTagInfo, &attributes, sizeof(attributes))
                && !(attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                && !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                && GetFileInformationByHandleEx(
                    handle, FileStandardInfo, &standard, sizeof(standard))
                && standard.EndOfFile.QuadPart >= 0
                && static_cast<std::uint64_t>(standard.EndOfFile.QuadPart) <= maximum_bytes
                && owned_by_current_user(handle)
                && restrict_handle(handle);
            if (!valid) {
                CloseHandle(handle);
                return {StorageReadStatus::failure, {}};
            }
            SecretBuffer output{static_cast<std::size_t>(standard.EndOfFile.QuadPart)};
            std::size_t offset = 0;
            while (offset < output.size()) {
                const auto amount = static_cast<DWORD>(std::min<std::size_t>(
                    output.size() - offset, std::numeric_limits<DWORD>::max()));
                DWORD consumed{};
                if (!ReadFile(handle, output.mutable_bytes().data() + offset,
                              amount, &consumed, nullptr) || consumed != amount) {
                    CloseHandle(handle);
                    return {StorageReadStatus::failure, {}};
                }
                offset += consumed;
            }
            CloseHandle(handle);
            return {StorageReadStatus::value, std::move(output)};
#else
            const int descriptor = openat(
                config_fd_, filename_for(file), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            if (descriptor < 0) {
                return errno == ENOENT ? StorageReadResult{}
                    : StorageReadResult{StorageReadStatus::failure, {}};
            }
            struct stat state{};
            if (fstat(descriptor, &state) != 0 || !S_ISREG(state.st_mode)
                || state.st_size < 0
                || static_cast<std::uint64_t>(state.st_size) > maximum_bytes
                || state.st_uid != geteuid()
                || fchmod(descriptor, 0600) != 0) {
                close(descriptor);
                return {StorageReadStatus::failure, {}};
            }
            SecretBuffer output{static_cast<std::size_t>(state.st_size)};
            std::size_t offset = 0;
            while (offset < output.size()) {
                const auto consumed = ::read(
                    descriptor, output.mutable_bytes().data() + offset,
                    output.size() - offset);
                if (consumed < 0 && errno == EINTR) continue;
                if (consumed <= 0) {
                    close(descriptor);
                    return {StorageReadStatus::failure, {}};
                }
                offset += static_cast<std::size_t>(consumed);
            }
            close(descriptor);
            return {StorageReadStatus::value, std::move(output)};
#endif
        }
        catch (...) {
            return {StorageReadStatus::failure, {}};
        }
    }

    [[nodiscard]] bool write_atomic(
        const AuthFile file, const std::span<const std::byte> bytes) noexcept override
    {
        try {
            if (!ready_ || !sodium_runtime_ready()) return false;
#if defined(_WIN32)
            const auto target = path_for(config_, file);
            std::filesystem::path temporary;
            HANDLE handle = INVALID_HANDLE_VALUE;
            for (int attempt = 0; attempt < 8 && handle == INVALID_HANDLE_VALUE; ++attempt) {
                std::array<unsigned char, 16> nonce{};
                randombytes_buf(nonce.data(), nonce.size());
                static constexpr wchar_t digits[] = L"0123456789abcdef";
                std::wstring suffix = L".tmp.";
                for (const auto value : nonce) {
                    suffix.push_back(digits[value >> 4U]);
                    suffix.push_back(digits[value & 0x0FU]);
                }
                temporary = target;
                temporary += suffix;
                SECURITY_ATTRIBUTES attributes{
                    sizeof(SECURITY_ATTRIBUTES), security_descriptor_, FALSE};
                handle = CreateFileW(
                    temporary.c_str(), GENERIC_WRITE | READ_CONTROL | WRITE_DAC,
                    0, &attributes,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH
                                    | FILE_FLAG_OPEN_REPARSE_POINT,
                    nullptr);
                if (handle == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) {
                    return false;
                }
            }
            if (handle == INVALID_HANDLE_VALUE) return false;
            if (!restrict_handle(handle)) {
                CloseHandle(handle);
                DeleteFileW(temporary.c_str());
                return false;
            }
            std::size_t offset = 0;
            bool ok = true;
            while (offset < bytes.size()) {
                const auto amount = static_cast<DWORD>(std::min<std::size_t>(
                    bytes.size() - offset, std::numeric_limits<DWORD>::max()));
                DWORD written{};
                if (!WriteFile(handle, bytes.data() + offset, amount, &written, nullptr)
                    || written != amount) {
                    ok = false;
                    break;
                }
                offset += written;
            }
            ok = ok && FlushFileBuffers(handle);
            CloseHandle(handle);
            if (ok) {
                ok = MoveFileExW(
                    temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            }
            if (!ok) DeleteFileW(temporary.c_str());
            return ok;
#else
            std::string temporary;
            int descriptor = -1;
            for (int attempt = 0; attempt < 8 && descriptor < 0; ++attempt) {
                std::array<std::byte, 16> nonce{};
                randombytes_buf(nonce.data(), nonce.size());
                static constexpr char digits[] = "0123456789abcdef";
                temporary = ".service_auth.";
                for (const auto item : nonce) {
                    const auto value = std::to_integer<unsigned int>(item);
                    temporary.push_back(digits[value >> 4U]);
                    temporary.push_back(digits[value & 0x0FU]);
                }
                temporary += ".tmp";
                descriptor = openat(
                    config_fd_, temporary.c_str(),
                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
                if (descriptor < 0 && errno != EEXIST) return false;
            }
            if (descriptor < 0) return false;
            std::size_t offset = 0;
            bool ok = true;
            while (offset < bytes.size()) {
                const auto written = ::write(
                    descriptor, bytes.data() + offset, bytes.size() - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) {
                    ok = false;
                    break;
                }
                offset += static_cast<std::size_t>(written);
            }
            ok = ok && fsync(descriptor) == 0;
            close(descriptor);
            if (!ok || renameat(
                    config_fd_, temporary.c_str(), config_fd_, filename_for(file)) != 0) {
                static_cast<void>(unlinkat(config_fd_, temporary.c_str(), 0));
                return false;
            }
            // renameat is the commit point. Directory fsync improves crash
            // durability, but a failure after commit must not make callers
            // roll memory back while the new file is already visible.
            static_cast<void>(fsync(config_fd_));
            return true;
#endif
        }
        catch (...) {
            return false;
        }
    }

private:
    void initialize() noexcept
    {
        try {
            std::error_code ec;
            std::filesystem::create_directories(config_, ec);
            if (ec) return;
#if defined(_WIN32)
            config_handle_ = CreateFileW(
                config_.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL
                                     | WRITE_DAC | WRITE_OWNER,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (config_handle_ == INVALID_HANDLE_VALUE) return;
            FILE_ATTRIBUTE_TAG_INFO attributes{};
            if (!GetFileInformationByHandleEx(
                    config_handle_, FileAttributeTagInfo, &attributes, sizeof(attributes))
                || !(attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                || (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return;
            if (!load_current_user_sid() || !ensure_owned_by_current_user(config_handle_)) return;
            LPWSTR sid_text{};
            if (!ConvertSidToStringSidW(current_user_sid_.data(), &sid_text)) return;
            const std::wstring sddl = std::wstring{L"O:"} + sid_text
                + L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + sid_text + L")";
            LocalFree(sid_text);
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    sddl.c_str(), SDDL_REVISION_1,
                    &security_descriptor_, nullptr)) return;
            if (!restrict_handle(config_handle_)) return;
            SECURITY_ATTRIBUTES security{
                sizeof(SECURITY_ATTRIBUTES), security_descriptor_, FALSE};
            const auto lock_path = config_ / "service_auth.lock";
            lock_handle_ = CreateFileW(
                lock_path.c_str(), GENERIC_READ | GENERIC_WRITE
                                      | READ_CONTROL | WRITE_DAC,
                FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS,
                FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (lock_handle_ == INVALID_HANDLE_VALUE
                || !owned_by_current_user(lock_handle_)
                || !restrict_handle(lock_handle_)) return;
            FILE_ATTRIBUTE_TAG_INFO lock_attributes{};
            if (!GetFileInformationByHandleEx(
                    lock_handle_, FileAttributeTagInfo, &lock_attributes,
                    sizeof(lock_attributes))
                || (lock_attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                || (lock_attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return;
            if (!LockFileEx(
                    lock_handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, MAXDWORD, MAXDWORD, &lock_overlapped_)) return;
            ready_ = true;
#else
            config_fd_ = open(config_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (config_fd_ < 0) return;
            struct stat config_state{};
            if (fstat(config_fd_, &config_state) != 0 || !S_ISDIR(config_state.st_mode)
                || config_state.st_uid != geteuid() || fchmod(config_fd_, 0700) != 0) return;
            lock_fd_ = openat(
                config_fd_, "service_auth.lock",
                O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (lock_fd_ < 0) return;
            struct stat lock_state{};
            if (fstat(lock_fd_, &lock_state) != 0 || !S_ISREG(lock_state.st_mode)
                || lock_state.st_uid != geteuid()
                || fchmod(lock_fd_, 0600) != 0
                || flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) return;
            ready_ = true;
#endif
        }
        catch (...) {}
    }

#if defined(_WIN32)
    [[nodiscard]] bool load_current_user_sid() noexcept
    {
        HANDLE token{};
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
        DWORD required{};
        static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0, &required));
        if (required == 0) {
            CloseHandle(token);
            return false;
        }
        try {
            std::vector<std::byte> buffer(required);
            if (!GetTokenInformation(
                    token, TokenUser, buffer.data(), required, &required)) {
                CloseHandle(token);
                return false;
            }
            const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
            const auto sid_bytes = GetLengthSid(user->User.Sid);
            current_user_sid_.resize(sid_bytes);
            const bool copied = CopySid(
                sid_bytes, current_user_sid_.data(), user->User.Sid) != FALSE;
            CloseHandle(token);
            return copied;
        }
        catch (...) {
            CloseHandle(token);
            return false;
        }
    }

    [[nodiscard]] bool owned_by_current_user(const HANDLE handle) const noexcept
    {
        if (current_user_sid_.empty()) return false;
        PSID owner{};
        PSECURITY_DESCRIPTOR descriptor{};
        const auto result = GetSecurityInfo(
            handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
            &owner, nullptr, nullptr, nullptr, &descriptor);
        const bool matches = result == ERROR_SUCCESS && owner != nullptr
            && IsValidSid(owner)
            && EqualSid(owner, const_cast<std::byte*>(current_user_sid_.data()));
        if (descriptor != nullptr) LocalFree(descriptor);
        return matches;
    }

    [[nodiscard]] bool ensure_owned_by_current_user(const HANDLE handle) const noexcept
    {
        if (owned_by_current_user(handle)) return true;
        if (current_user_sid_.empty()) return false;
        return SetSecurityInfo(
                   handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                   const_cast<std::byte*>(current_user_sid_.data()),
                   nullptr, nullptr, nullptr) == ERROR_SUCCESS
            && owned_by_current_user(handle);
    }

    [[nodiscard]] bool restrict_handle(const HANDLE handle) const noexcept
    {
        PACL dacl{};
        BOOL present{};
        BOOL defaulted{};
        if (security_descriptor_ == nullptr
            || !GetSecurityDescriptorDacl(
                security_descriptor_, &present, &dacl, &defaulted)
            || !present || dacl == nullptr) return false;
        return SetSecurityInfo(
                   handle, SE_FILE_OBJECT,
                   DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                   nullptr, nullptr, dacl, nullptr) == ERROR_SUCCESS;
    }
#endif

    std::filesystem::path config_;
    bool ready_{};
#if defined(_WIN32)
    HANDLE config_handle_{INVALID_HANDLE_VALUE};
    HANDLE lock_handle_{INVALID_HANDLE_VALUE};
    OVERLAPPED lock_overlapped_{};
    PSECURITY_DESCRIPTOR security_descriptor_{};
    std::vector<std::byte> current_user_sid_;
#else
    int config_fd_{-1};
    int lock_fd_{-1};
#endif
};

class SystemClock final : public AuthClock {
public:
    [[nodiscard]] std::int64_t now_unix_seconds() noexcept override
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
};

class SystemRandom final : public AuthRandom {
public:
    [[nodiscard]] bool fill(const std::span<std::byte> output) noexcept override
    {
        if (!sodium_runtime_ready()) return false;
        if (!output.empty()) randombytes_buf(output.data(), output.size());
        return true;
    }
};

class SodiumPasswordDeriver final : public PasswordDeriver {
public:
    [[nodiscard]] SecretBytesResult derive(
        const std::span<const std::byte> password,
        const std::span<const std::byte> salt) noexcept override
    {
        return argon2id_v1(password, salt);
    }
};

[[nodiscard]] CanonicalJsonValue string_value(std::string value)
{
    return CanonicalJsonValue{std::move(value)};
}

[[nodiscard]] std::optional<std::string> b64(const std::span<const std::byte> value)
{
    auto encoded = encode_base64url_padded(value);
    if (!encoded) return std::nullopt;
    return std::move(*encoded.value);
}

[[nodiscard]] bool exact_object_fields(
    const CanonicalJsonValue::Object& object,
    std::initializer_list<std::string_view> fields) noexcept
{
    if (object.size() != fields.size()) return false;
    for (const auto expected : fields) {
        if (std::none_of(object.begin(), object.end(), [&](const auto& item) {
                return item.first == expected;
            })) return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t> as_epoch(const CanonicalJsonValue* value) noexcept
{
    if (value == nullptr || value->as_integer() == nullptr || *value->as_integer() < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*value->as_integer());
}

[[nodiscard]] std::optional<std::int64_t> as_time(const CanonicalJsonValue* value) noexcept
{
    if (value == nullptr || value->as_integer() == nullptr || *value->as_integer() < 0) {
        return std::nullopt;
    }
    return *value->as_integer();
}

[[nodiscard]] bool valid_identifier(
    const std::string_view value, const std::size_t maximum) noexcept
{
    if (value.empty() || value.size() > maximum) return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
    });
}

[[nodiscard]] bool python_whitespace(const std::uint32_t value) noexcept
{
    return (value >= 0x09U && value <= 0x0DU)
        || (value >= 0x1CU && value <= 0x20U) || value == 0x85U
        || value == 0xA0U || value == 0x1680U
        || (value >= 0x2000U && value <= 0x200AU)
        || value == 0x2028U || value == 0x2029U || value == 0x202FU
        || value == 0x205FU || value == 0x3000U;
}

[[nodiscard]] bool contains_non_python_whitespace(const std::string_view text) noexcept
{
    std::size_t index = 0;
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::uint32_t value{};
        std::size_t width{};
        if (first < 0x80U) {
            value = first;
            width = 1;
        }
        else if ((first & 0xE0U) == 0xC0U) {
            value = first & 0x1FU;
            width = 2;
        }
        else if ((first & 0xF0U) == 0xE0U) {
            value = first & 0x0FU;
            width = 3;
        }
        else {
            value = first & 0x07U;
            width = 4;
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            value = (value << 6U)
                | (static_cast<unsigned char>(text[index + offset]) & 0x3FU);
        }
        if (!python_whitespace(value)) return true;
        index += width;
    }
    return false;
}

[[nodiscard]] std::string hex(const std::span<const std::byte> bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto value = std::to_integer<unsigned int>(bytes[index]);
        output[index * 2] = digits[value >> 4U];
        output[index * 2 + 1] = digits[value & 0xFU];
    }
    return output;
}

void set_uuid_v4_bits(const std::span<std::byte> bytes) noexcept
{
    if (bytes.size() != 16) return;
    bytes[6] = static_cast<std::byte>(
        (std::to_integer<unsigned int>(bytes[6]) & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::byte>(
        (std::to_integer<unsigned int>(bytes[8]) & 0x3FU) | 0x80U);
}

[[nodiscard]] SecretBuffer concatenate(
    const std::span<const std::byte> first,
    const std::span<const std::byte> second)
{
    SecretBuffer output{first.size() + second.size()};
    std::copy(first.begin(), first.end(), output.mutable_bytes().begin());
    std::copy(second.begin(), second.end(), output.mutable_bytes().begin() + first.size());
    return output;
}

[[nodiscard]] std::shared_ptr<const SecretBuffer> environment_secret(
    const char* name, const std::size_t expected)
{
#if defined(_WIN32)
    char* allocated{};
    std::size_t length{};
    if (_dupenv_s(&allocated, &length, name) != 0 || allocated == nullptr
        || length <= 1) {
        if (allocated != nullptr) std::free(allocated);
        return {};
    }
    struct EnvironmentWiper {
        char* value;
        std::size_t length;
        ~EnvironmentWiper()
        {
            if (value == nullptr) return;
            secure_zero(std::as_writable_bytes(std::span{value, length}));
            std::free(value);
        }
    } wiper{allocated, length};
    const std::string_view value{allocated, length - 1};
#else
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') return {};
#endif
    auto decoded = decode_base64url_canonical(value, expected);
    if (!decoded) throw std::invalid_argument{"invalid authentication key override"};
    auto secret = std::make_shared<SecretBuffer>(*decoded.value);
    wipe(*decoded.value);
    return secret;
}

// Python persisted these two wall-clock fields as JSON doubles. Migration
// accepts only their common non-negative decimal form, floors fractional
// seconds, and then returns to the strict integer-only JSON domain.
[[nodiscard]] std::optional<std::string> normalize_python_timestamps(
    const std::string_view input)
{
    std::string output{input};
    for (const std::string_view key : {"\"created_at\"", "\"expires_at\""}) {
        std::size_t search = 0;
        while ((search = output.find(key, search)) != std::string::npos) {
            auto cursor = output.find(':', search + key.size());
            if (cursor == std::string::npos) return std::nullopt;
            ++cursor;
            while (cursor < output.size()
                   && (output[cursor] == ' ' || output[cursor] == '\t'
                       || output[cursor] == '\r' || output[cursor] == '\n')) ++cursor;
            const auto integer_begin = cursor;
            while (cursor < output.size() && output[cursor] >= '0'
                   && output[cursor] <= '9') ++cursor;
            if (cursor == integer_begin) return std::nullopt;
            if (cursor < output.size() && output[cursor] == '.') {
                const auto fraction_begin = cursor++;
                const auto digits_begin = cursor;
                while (cursor < output.size() && output[cursor] >= '0'
                       && output[cursor] <= '9') ++cursor;
                if (cursor == digits_begin || (cursor < output.size()
                        && (output[cursor] == 'e' || output[cursor] == 'E'))) {
                    return std::nullopt;
                }
                output.erase(fraction_begin, cursor - fraction_begin);
                cursor = fraction_begin;
            }
            search = cursor;
        }
    }
    return output;
}

}  // namespace

std::string_view auth_error_name(const AuthError error) noexcept
{
    switch (error) {
        case AuthError::none: return "none";
        case AuthError::invalid_argument: return "invalid_argument";
        case AuthError::invalid_password: return "invalid_password";
        case AuthError::password_too_large: return "password_too_large";
        case AuthError::password_derivation_busy: return "password_derivation_busy";
        case AuthError::password_derivation_failed: return "password_derivation_failed";
        case AuthError::already_initialized: return "already_initialized";
        case AuthError::not_initialized: return "not_initialized";
        case AuthError::authentication_failed: return "authentication_failed";
        case AuthError::unknown_session: return "unknown_session";
        case AuthError::session_expired: return "session_expired";
        case AuthError::stale_epoch: return "stale_epoch";
        case AuthError::invalid_resume_ticket: return "invalid_resume_ticket";
        case AuthError::invalid_remember_proof: return "invalid_remember_proof";
        case AuthError::invalid_remember_token: return "invalid_remember_token";
        case AuthError::capacity_exceeded: return "capacity_exceeded";
        case AuthError::entropy_failure: return "entropy_failure";
        case AuthError::storage_failure: return "storage_failure";
        case AuthError::corrupted_storage: return "corrupted_storage";
        case AuthError::crypto_failure: return "crypto_failure";
        case AuthError::subscription_not_found: return "subscription_not_found";
    }
    return "unknown";
}

std::shared_ptr<AuthStorage> make_file_auth_storage(std::filesystem::path project_root)
{
    return std::make_shared<FileAuthStorage>(std::move(project_root));
}

std::shared_ptr<AuthClock> make_system_auth_clock()
{
    return std::make_shared<SystemClock>();
}

std::shared_ptr<AuthRandom> make_system_auth_random()
{
    return std::make_shared<SystemRandom>();
}

std::shared_ptr<PasswordDeriver> make_sodium_password_deriver()
{
    return std::make_shared<SodiumPasswordDeriver>();
}

class AuthOwner::Impl final {
public:
    friend class AuthOwner;

    struct PasswordState {
        bool initialized{};
        std::uint64_t epoch{};
        PublicBytes salt;
        SecretBuffer hash;
    };

    struct Session {
        std::string id;
        std::int64_t expires_at{};
        std::uint64_t epoch{};
        SecretBuffer master;
        SecretBuffer resume;
    };

    struct Remembered {
        std::string id;
        PublicBytes hash;
        std::int64_t created_at{};
        std::int64_t expires_at{};
        std::uint64_t epoch{};
    };

    struct Subscription {
        std::string session_id;
        std::deque<RevocationEvent> events;
    };

    Impl(AuthOwnerConfig config, AuthDependencies dependencies)
        : config_(std::move(config)), dependencies_(std::move(dependencies))
    {}

    [[nodiscard]] AuthError load() noexcept
    {
        try {
            auto signing = load_or_create_key(
                AuthFile::signing_key, ed25519_seed_bytes,
                config_.signing_seed_policy == SigningSeedPolicy::python_tauri_fixed_compatibility);
            if (!signing) return signing.error;
            signing_seed_ = std::move(*signing.value);
            auto public_key = ed25519_public_key_from_seed(signing_seed_.bytes());
            if (!public_key) return crypto_to_auth(public_key.error);
            signing_public_ = std::move(*public_key.value);

            auto ticket = load_or_create_key(AuthFile::ticket_key, auth_key_bytes, false);
            if (!ticket) return ticket.error;
            ticket_key_ = std::move(*ticket.value);

            const auto password_error = load_password();
            if (password_error != AuthError::none) return password_error;
            const auto remembered_error = load_remembered();
            if (remembered_error != AuthError::none) return remembered_error;
            return AuthError::none;
        }
        catch (...) {
            return AuthError::storage_failure;
        }
    }

    [[nodiscard]] AuthResult<SecretBuffer> load_or_create_key(
        const AuthFile file, const std::size_t expected, const bool fixed)
    {
        const auto& override = file == AuthFile::signing_key
            ? dependencies_.signing_seed_override
            : dependencies_.ticket_key_override;
        if (override) {
            if (override->size() != expected) {
                return result_error<SecretBuffer>(AuthError::invalid_argument);
            }
            return {std::optional<SecretBuffer>{std::in_place, override->bytes()},
                    AuthError::none};
        }
        auto read = dependencies_.storage->read(file, config_.max_file_bytes);
        if (read.status == StorageReadStatus::failure) {
            return result_error<SecretBuffer>(AuthError::storage_failure);
        }
        if (read.status == StorageReadStatus::value) {
            if (read.bytes.size() != expected) {
                return result_error<SecretBuffer>(AuthError::corrupted_storage);
            }
            if (fixed) {
                auto decoded = decode_base64url_canonical(fixed_signing_seed_b64, expected);
                if (!decoded) return result_error<SecretBuffer>(AuthError::crypto_failure);
                const bool matches = constant_time_equal(read.bytes.bytes(), *decoded.value);
                SecretBuffer compatible{*decoded.value};
                wipe(*decoded.value);
                if (!matches && !dependencies_.storage->write_atomic(file, compatible.bytes())) {
                    return result_error<SecretBuffer>(AuthError::storage_failure);
                }
                return {std::optional<SecretBuffer>{std::move(compatible)}, AuthError::none};
            }
            return {std::optional<SecretBuffer>{std::move(read.bytes)}, AuthError::none};
        }

        SecretBuffer key;
        if (fixed) {
            auto decoded = decode_base64url_canonical(fixed_signing_seed_b64, expected);
            if (!decoded) return result_error<SecretBuffer>(AuthError::crypto_failure);
            key = SecretBuffer{*decoded.value};
            wipe(*decoded.value);
        }
        else {
            key = SecretBuffer{expected};
            if (!dependencies_.random->fill(key.mutable_bytes())) {
                return result_error<SecretBuffer>(AuthError::entropy_failure);
            }
        }
        if (!dependencies_.storage->write_atomic(file, key.bytes())) {
            return result_error<SecretBuffer>(AuthError::storage_failure);
        }
        return {std::optional<SecretBuffer>{std::move(key)}, AuthError::none};
    }

    [[nodiscard]] AuthError load_password()
    {
        auto read = dependencies_.storage->read(AuthFile::password_state, config_.max_file_bytes);
        if (read.status == StorageReadStatus::missing) return AuthError::none;
        if (read.status == StorageReadStatus::failure) return AuthError::storage_failure;
        auto parsed = parse_canonical_json_value(
            text_of(read.bytes.bytes()), {.max_input_bytes = config_.max_file_bytes,
                                         .max_output_bytes = config_.max_file_bytes});
        struct ParsedWiper {
            CanonicalJsonValue* value;
            ~ParsedWiper() { if (value != nullptr) value->wipe_strings(); }
        } parsed_wiper{parsed.value ? std::addressof(*parsed.value) : nullptr};
        if (!parsed || parsed.value->as_object() == nullptr) return AuthError::corrupted_storage;
        const auto& object = *parsed.value->as_object();
        if (!exact_object_fields(object, {"initialized", "pwd_epoch", "pwd_salt", "pwd_hash"})) {
            return AuthError::corrupted_storage;
        }
        const auto* initialized = parsed.value->find("initialized");
        const auto epoch = as_epoch(parsed.value->find("pwd_epoch"));
        if (initialized == nullptr || initialized->as_boolean() == nullptr || !epoch) {
            return AuthError::corrupted_storage;
        }
        if (!*initialized->as_boolean()) {
            if (*epoch != 0 || !parsed.value->find("pwd_salt")->is_null()
                || !parsed.value->find("pwd_hash")->is_null()) return AuthError::corrupted_storage;
            return AuthError::none;
        }
        const auto* salt_text = parsed.value->find("pwd_salt")->as_string();
        const auto* hash_text = parsed.value->find("pwd_hash")->as_string();
        if (salt_text == nullptr || hash_text == nullptr || *epoch == 0) {
            return AuthError::corrupted_storage;
        }
        auto salt = decode_base64url_canonical(*salt_text, argon2id_salt_bytes);
        auto hash = decode_base64url_canonical(*hash_text, argon2id_output_bytes);
        if (!salt || !hash) return AuthError::corrupted_storage;
        password_.initialized = true;
        password_.epoch = *epoch;
        password_.salt = std::move(*salt.value);
        password_.hash = SecretBuffer{*hash.value};
        wipe(*hash.value);
        return AuthError::none;
    }

    [[nodiscard]] AuthError load_remembered()
    {
        auto read = dependencies_.storage->read(AuthFile::remembered_logins, config_.max_file_bytes);
        if (read.status == StorageReadStatus::missing) return AuthError::none;
        if (read.status == StorageReadStatus::failure) return AuthError::storage_failure;
        const auto normalized = normalize_python_timestamps(text_of(read.bytes.bytes()));
        if (!normalized) return AuthError::corrupted_storage;
        auto parsed = parse_canonical_json_value(
            *normalized, {.max_input_bytes = config_.max_file_bytes,
                          .max_output_bytes = config_.max_file_bytes});
        if (!parsed || parsed.value->as_object() == nullptr
            || !exact_object_fields(*parsed.value->as_object(), {"logins"})) {
            return AuthError::corrupted_storage;
        }
        const auto* logins = parsed.value->find("logins")->as_array();
        if (logins == nullptr || logins->size() > config_.max_remembered_logins) {
            return AuthError::corrupted_storage;
        }
        const auto now = dependencies_.clock->now_unix_seconds();
        for (const auto& item : *logins) {
            const auto* object = item.as_object();
            if (object == nullptr || !exact_object_fields(
                    *object, {"token_id", "token_hash", "created_at", "expires_at", "pwd_epoch"})) {
                return AuthError::corrupted_storage;
            }
            const auto* id = item.find("token_id")->as_string();
            const auto* hash_text = item.find("token_hash")->as_string();
            const auto created = as_time(item.find("created_at"));
            const auto expires = as_time(item.find("expires_at"));
            const auto epoch = as_epoch(item.find("pwd_epoch"));
            if (id == nullptr || hash_text == nullptr || !created || !expires || !epoch
                || !valid_identifier(*id, config_.max_identifier_bytes)
                || *expires < *created) return AuthError::corrupted_storage;
            auto hash = decode_base64url_canonical(*hash_text, hmac_sha256_bytes);
            if (!hash || remembered_.contains(*id)) return AuthError::corrupted_storage;
            if (*expires >= now && password_.initialized && *epoch == password_.epoch) {
                remembered_.emplace(*id, Remembered{*id, std::move(*hash.value), *created, *expires, *epoch});
            }
        }
        return AuthError::none;
    }

    [[nodiscard]] std::optional<SecretBuffer> password_json(const PasswordState& value) const
    {
        WipingString encoded;
        if (value.initialized) {
            auto salt_result = encode_base64url_padded(value.salt);
            auto hash_result = encode_base64url_padded(value.hash.bytes());
            struct EncodedWiper {
                StringResult* salt;
                StringResult* hash;
                ~EncodedWiper()
                {
                    if (salt->value) wipe(*salt->value);
                    if (hash->value) wipe(*hash->value);
                }
            } encoded_wiper{&salt_result, &hash_result};
            if (!salt_result || !hash_result) return std::nullopt;
            const auto epoch = std::to_string(value.epoch);
            encoded.value.reserve(
                80 + epoch.size() + salt_result.value->size()
                + hash_result.value->size());
            encoded.value.append("{\"initialized\":true,\"pwd_epoch\":");
            encoded.value.append(epoch);
            encoded.value.append(",\"pwd_hash\":\"");
            encoded.value.append(*hash_result.value);
            encoded.value.append("\",\"pwd_salt\":\"");
            encoded.value.append(*salt_result.value);
            encoded.value.append("\"}");
        }
        else {
            encoded.value = "{\"initialized\":false,\"pwd_epoch\":0,"
                            "\"pwd_hash\":null,\"pwd_salt\":null}";
        }
        if (encoded.value.size() > config_.max_file_bytes) return std::nullopt;
        SecretBuffer output{bytes_of(encoded.value)};
        return std::optional<SecretBuffer>{std::move(output)};
    }

    [[nodiscard]] std::optional<std::string> remembered_json(
        const std::unordered_map<std::string, Remembered>& values) const
    {
        CanonicalJsonValue::Array logins;
        logins.reserve(values.size());
        std::vector<std::string_view> ids;
        ids.reserve(values.size());
        for (const auto& [id, ignored] : values) {
            static_cast<void>(ignored);
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end());
        for (const auto id : ids) {
            const auto& value = values.at(std::string{id});
            auto hash_text = b64(value.hash);
            if (!hash_text) return std::nullopt;
            logins.emplace_back(CanonicalJsonValue::Object{
                {"token_id", string_value(value.id)},
                {"token_hash", string_value(std::move(*hash_text))},
                {"created_at", CanonicalJsonValue{value.created_at}},
                {"expires_at", CanonicalJsonValue{value.expires_at}},
                {"pwd_epoch", CanonicalJsonValue{static_cast<std::int64_t>(value.epoch)}},
            });
        }
        auto encoded = encode_canonical_json_value(
            CanonicalJsonValue{CanonicalJsonValue::Object{
                {"logins", CanonicalJsonValue{std::move(logins)}}}},
            {.max_input_bytes = config_.max_file_bytes,
             .max_output_bytes = config_.max_file_bytes});
        if (!encoded) return std::nullopt;
        return std::move(encoded.text);
    }

    [[nodiscard]] AuthError validate_password(const SecretBuffer& password) const noexcept
    {
        if (password.size() > config_.max_password_utf8_bytes) return AuthError::password_too_large;
        const auto text = text_of(password.bytes());
        if (!is_valid_utf8(text)) return AuthError::invalid_password;
        if (text.empty() || !contains_non_python_whitespace(text)) {
            return AuthError::invalid_password;
        }
        return AuthError::none;
    }

    [[nodiscard]] bool acquire_derivation() noexcept
    {
        auto active = active_derivations_.load(std::memory_order_relaxed);
        while (active < config_.max_concurrent_password_derivations) {
            if (active_derivations_.compare_exchange_weak(
                    active, active + 1, std::memory_order_acq_rel,
                    std::memory_order_relaxed)) return true;
        }
        return false;
    }

    void release_derivation() noexcept
    {
        active_derivations_.fetch_sub(1, std::memory_order_release);
    }

    [[nodiscard]] AuthResult<PasswordState> derive_password(
        SecretBuffer password, const std::uint64_t epoch) noexcept
    {
        const auto validation = validate_password(password);
        if (validation != AuthError::none) return result_error<PasswordState>(validation);
        if (!acquire_derivation()) {
            return result_error<PasswordState>(AuthError::password_derivation_busy);
        }
        struct Guard {
            Impl* owner;
            ~Guard() { owner->release_derivation(); }
        } guard{this};
        try {
            PublicBytes salt(argon2id_salt_bytes);
            if (!dependencies_.random->fill(salt)) {
                return result_error<PasswordState>(AuthError::entropy_failure);
            }
            auto hash = dependencies_.password_deriver->derive(password.bytes(), salt);
            if (!hash) return result_error<PasswordState>(AuthError::password_derivation_failed);
            return {std::optional<PasswordState>{std::in_place, true, epoch,
                                                 std::move(salt), std::move(*hash.value)},
                    AuthError::none};
        }
        catch (...) {
            return result_error<PasswordState>(AuthError::password_derivation_failed);
        }
    }

    [[nodiscard]] AuthStatus initialize(SecretBuffer password)
    {
        {
            std::lock_guard lock(mutex_);
            if (password_.initialized) return status(AuthError::already_initialized);
        }
        auto derived = derive_password(std::move(password), 1);
        if (!derived) return status(derived.error);
        auto json = password_json(*derived.value);
        if (!json) return status(AuthError::storage_failure);
        std::lock_guard persistence_lock(persistence_mutex_);
        {
            std::lock_guard lock(mutex_);
            if (password_.initialized) return status(AuthError::already_initialized);
        }
        if (!dependencies_.storage->write_atomic(
                AuthFile::password_state, json->bytes())) {
            return status(AuthError::storage_failure);
        }
        {
            std::lock_guard lock(mutex_);
            password_ = std::move(*derived.value);
        }
        return status();
    }

    [[nodiscard]] AuthResult<ControlSessionMaterial> initialize_control(
        HandshakeMaterial handshake, SecretBuffer password) noexcept
    {
        try {
            if (handshake.shared_key.size() != x25519_key_bytes
                || handshake.transcript_hash.size() != sha256_bytes) {
                return result_error<ControlSessionMaterial>(AuthError::invalid_argument);
            }
            {
                std::lock_guard lock(mutex_);
                if (password_.initialized) {
                    return result_error<ControlSessionMaterial>(AuthError::already_initialized);
                }
            }
            auto derived = derive_password(std::move(password), 1);
            if (!derived) return result_error<ControlSessionMaterial>(derived.error);
            auto serialized = password_json(*derived.value);
            if (!serialized) {
                return result_error<ControlSessionMaterial>(AuthError::storage_failure);
            }
            auto combined = concatenate(
                handshake.shared_key.bytes(), derived.value->hash.bytes());
            auto master = hkdf_sha256(
                combined.bytes(), handshake.transcript_hash,
                bytes_of("master-secret"), auth_key_bytes);
            if (!master) {
                return result_error<ControlSessionMaterial>(crypto_to_auth(master.error));
            }
            auto resume = hkdf_sha256(
                master.value->bytes(), handshake.transcript_hash,
                bytes_of("resume-secret"), auth_key_bytes);
            if (!resume) {
                return result_error<ControlSessionMaterial>(crypto_to_auth(resume.error));
            }
            // Finish the only allocating public snapshot before persistence
            // or session insertion. Once the password file is committed, the
            // remaining moves must not turn success into a hidden live session
            // reported as failure under memory pressure.
            PasswordPublicState committed_state{
                derived.value->initialized,
                derived.value->epoch,
                derived.value->salt};

            std::lock_guard persistence_lock(persistence_mutex_);
            std::lock_guard lock(mutex_);
            if (password_.initialized) {
                return result_error<ControlSessionMaterial>(AuthError::already_initialized);
            }
            prune_sessions_locked();
            if (sessions_.size() >= config_.max_sessions) {
                return result_error<ControlSessionMaterial>(AuthError::capacity_exceeded);
            }
            auto session = insert_session_locked(
                std::move(*master.value), std::move(*resume.value), 1, false);
            if (!session) return session;
            if (!dependencies_.storage->write_atomic(
                    AuthFile::password_state, serialized->bytes())) {
                sessions_.erase(session->session_id);
                return result_error<ControlSessionMaterial>(AuthError::storage_failure);
            }
            password_ = std::move(*derived.value);
            session->password_state = std::move(committed_state);
            return session;
        }
        catch (...) {
            return result_error<ControlSessionMaterial>(AuthError::storage_failure);
        }
    }

    [[nodiscard]] AuthStatus rotate(
        std::optional<std::string_view> session_id,
        SecretBuffer password,
        const RevocationReason reason)
    {
        std::uint64_t expected_epoch{};
        {
            std::lock_guard lock(mutex_);
            if (session_id) {
                const auto required = require_session_locked(*session_id);
                if (required != AuthError::none) return status(required);
                expected_epoch = sessions_.at(std::string{*session_id}).epoch;
            }
            else {
                expected_epoch = password_.epoch;
            }
        }
        if (expected_epoch >= static_cast<std::uint64_t>(maximum_safe_json_integer)) {
            return status(AuthError::capacity_exceeded);
        }
        auto derived = derive_password(std::move(password), expected_epoch + 1);
        if (!derived) return status(derived.error);

        std::lock_guard persistence_lock(persistence_mutex_);
        std::lock_guard lock(mutex_);
        if (password_.epoch != expected_epoch) return status(AuthError::stale_epoch);
        if (session_id && require_session_locked(*session_id) != AuthError::none) {
            return status(AuthError::unknown_session);
        }
        const std::unordered_map<std::string, Remembered> empty;
        auto empty_json = remembered_json(empty);
        auto state_json = password_json(*derived.value);
        if (!empty_json || !state_json) return status(AuthError::storage_failure);
        auto staged_subscriptions = subscriptions_;
        publish_revocations(
            staged_subscriptions, reason, derived.value->epoch);
        // Clear persisted bearer tokens first. If the second write fails, the
        // in-memory old state remains valid but restart fails closed to no tokens.
        if (!dependencies_.storage->write_atomic(
                AuthFile::remembered_logins, bytes_of(*empty_json))
            || !dependencies_.storage->write_atomic(
                AuthFile::password_state, state_json->bytes())) {
            return status(AuthError::storage_failure);
        }
        password_ = std::move(*derived.value);
        remembered_.clear();
        sessions_.clear();
        subscriptions_.swap(staged_subscriptions);
        return status();
    }

    [[nodiscard]] AuthError require_session_locked(const std::string_view id)
    {
        if (!valid_identifier(id, config_.max_identifier_bytes)) return AuthError::unknown_session;
        const auto it = sessions_.find(std::string{id});
        if (it == sessions_.end()) return AuthError::unknown_session;
        const auto now = dependencies_.clock->now_unix_seconds();
        if (it->second.expires_at < now) {
            erase_subscriptions_for_session_locked(id);
            sessions_.erase(it);
            return AuthError::session_expired;
        }
        if (it->second.epoch != password_.epoch) {
            erase_subscriptions_for_session_locked(id);
            sessions_.erase(it);
            return AuthError::stale_epoch;
        }
        return AuthError::none;
    }

    [[nodiscard]] AuthResult<ControlSessionMaterial> password_session(
        HandshakeMaterial handshake,
        const std::span<const std::byte> proof) noexcept
    {
        try {
            if (handshake.shared_key.size() != x25519_key_bytes
                || handshake.transcript_hash.size() != sha256_bytes) {
                return result_error<ControlSessionMaterial>(AuthError::invalid_argument);
            }
            std::lock_guard lock(mutex_);
            if (!password_.initialized) return result_error<ControlSessionMaterial>(AuthError::not_initialized);
            if (sessions_.size() >= config_.max_sessions) {
                prune_sessions_locked();
                if (sessions_.size() >= config_.max_sessions) {
                    return result_error<ControlSessionMaterial>(AuthError::capacity_exceeded);
                }
            }
            if (proof.size() != hmac_sha256_bytes) {
                return result_error<ControlSessionMaterial>(AuthError::authentication_failed);
            }
            const auto info = std::string{"auth-proof:"} + std::to_string(password_.epoch);
            auto context = hkdf_sha256(
                handshake.shared_key.bytes(), handshake.transcript_hash,
                bytes_of(info), hmac_sha256_bytes);
            if (!context) return result_error<ControlSessionMaterial>(crypto_to_auth(context.error));
            auto expected = hmac_sha256(password_.hash.bytes(), context.value->bytes());
            if (!expected || !constant_time_equal(*expected.value, proof)) {
                return result_error<ControlSessionMaterial>(AuthError::authentication_failed);
            }
            auto combined = concatenate(handshake.shared_key.bytes(), password_.hash.bytes());
            auto master = hkdf_sha256(
                combined.bytes(), handshake.transcript_hash,
                bytes_of("master-secret"), auth_key_bytes);
            if (!master) return result_error<ControlSessionMaterial>(crypto_to_auth(master.error));
            auto resume = hkdf_sha256(
                master.value->bytes(), handshake.transcript_hash,
                bytes_of("resume-secret"), auth_key_bytes);
            if (!resume) return result_error<ControlSessionMaterial>(crypto_to_auth(resume.error));
            return insert_session_locked(
                std::move(*master.value), std::move(*resume.value), password_.epoch, false);
        }
        catch (...) {
            return result_error<ControlSessionMaterial>(AuthError::crypto_failure);
        }
    }

    [[nodiscard]] AuthResult<ControlSessionMaterial> insert_session_locked(
        SecretBuffer master, SecretBuffer resume, const std::uint64_t epoch,
        const bool disclose) noexcept
    {
        try {
            std::array<std::byte, 16> random_id{};
            if (!dependencies_.random->fill(random_id)) {
                return result_error<ControlSessionMaterial>(AuthError::entropy_failure);
            }
            set_uuid_v4_bits(random_id);
            auto id = hex(random_id);
            if (sessions_.contains(id)) return result_error<ControlSessionMaterial>(AuthError::entropy_failure);
            const auto now = dependencies_.clock->now_unix_seconds();
            if (now < 0 || config_.session_ttl_seconds > maximum_safe_json_integer - now) {
                return result_error<ControlSessionMaterial>(AuthError::invalid_argument);
            }
            const auto expires = now + config_.session_ttl_seconds;
            auto control_salt = sha256(bytes_of(id));
            if (!control_salt) return result_error<ControlSessionMaterial>(crypto_to_auth(control_salt.error));
            auto tx = hkdf_sha256(master.bytes(), *control_salt.value,
                                  bytes_of("control:server-tx"), auth_key_bytes);
            auto rx = hkdf_sha256(master.bytes(), *control_salt.value,
                                  bytes_of("control:server-rx"), auth_key_bytes);
            if (!tx || !rx) return result_error<ControlSessionMaterial>(AuthError::crypto_failure);

            Session stored{id, expires, epoch, SecretBuffer{master.bytes()}, SecretBuffer{resume.bytes()}};
            auto ticket = issue_ticket(stored);
            if (!ticket) return result_error<ControlSessionMaterial>(ticket.error);

            ControlSessionMaterial material;
            material.session_id = id;
            material.expires_at = expires;
            material.pwd_epoch = epoch;
            material.resume_ticket = std::move(*ticket.value);
            material.control_server_tx = std::move(*tx.value);
            material.control_server_rx = std::move(*rx.value);
            material.password_state = {
                password_.initialized, password_.epoch, password_.salt};
            if (disclose) {
                material.disclosed_master_secret.emplace(master.bytes());
                material.disclosed_resume_secret.emplace(resume.bytes());
            }
            sessions_.emplace(id, std::move(stored));
            return {std::optional<ControlSessionMaterial>{std::move(material)}, AuthError::none};
        }
        catch (...) {
            return result_error<ControlSessionMaterial>(AuthError::crypto_failure);
        }
    }

    [[nodiscard]] AuthResult<SecretBuffer> issue_ticket(const Session& session) const noexcept
    {
        try {
            auto encoded = encode_canonical_json_value(CanonicalJsonValue{
                CanonicalJsonValue::Object{
                    {"session_id", string_value(session.id)},
                    {"pwd_epoch", CanonicalJsonValue{static_cast<std::int64_t>(session.epoch)}},
                    {"expires_at", CanonicalJsonValue{session.expires_at}},
                }});
            if (!encoded) return result_error<SecretBuffer>(AuthError::crypto_failure);
            auto signature = hmac_sha256(ticket_key_.bytes(), bytes_of(encoded.text));
            auto payload_b64 = b64(bytes_of(encoded.text));
            auto signature_b64 = signature ? b64(*signature.value) : std::nullopt;
            if (!signature || !payload_b64 || !signature_b64) {
                return result_error<SecretBuffer>(AuthError::crypto_failure);
            }
            std::string token = *payload_b64 + "." + *signature_b64;
            if (token.size() > config_.max_token_bytes) {
                wipe(token);
                return result_error<SecretBuffer>(AuthError::capacity_exceeded);
            }
            SecretBuffer output{bytes_of(token)};
            wipe(token);
            return {std::optional<SecretBuffer>{std::move(output)}, AuthError::none};
        }
        catch (...) {
            return result_error<SecretBuffer>(AuthError::crypto_failure);
        }
    }

    void prune_sessions_locked() noexcept
    {
        const auto now = dependencies_.clock->now_unix_seconds();
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->second.expires_at < now || it->second.epoch != password_.epoch) {
                erase_subscriptions_for_session_locked(it->first);
                it = sessions_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void erase_subscriptions_for_session_locked(const std::string_view id) noexcept
    {
        std::erase_if(subscriptions_, [&](const auto& item) {
            return item.second.session_id == id;
        });
    }

    void publish_revocations(
        std::unordered_map<SubscriptionId, Subscription>& subscriptions,
        const RevocationReason reason, const std::uint64_t epoch) const
    {
        for (auto& [ignored, subscription] : subscriptions) {
            static_cast<void>(ignored);
            while (subscription.events.size() >= config_.max_revocations_per_subscription) {
                subscription.events.pop_front();
            }
            subscription.events.push_back({subscription.session_id, reason, epoch});
        }
    }

    AuthOwnerConfig config_;
    AuthDependencies dependencies_;
    mutable std::mutex persistence_mutex_;
    mutable std::mutex mutex_;
    std::atomic<std::size_t> active_derivations_{0};
    PasswordState password_;
    SecretBuffer signing_seed_;
    PublicBytes signing_public_;
    SecretBuffer ticket_key_;
    std::unordered_map<std::string, Session> sessions_;
    std::unordered_map<std::string, Remembered> remembered_;
    std::unordered_map<SubscriptionId, Subscription> subscriptions_;
    SubscriptionId next_subscription_{1};
};

AuthOwner::AuthOwner(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
AuthOwner::~AuthOwner() = default;

AuthResult<std::unique_ptr<AuthOwner>> AuthOwner::open(
    AuthOwnerConfig config, AuthDependencies dependencies) noexcept
{
    try {
        if (!dependencies.storage || !dependencies.clock || !dependencies.random
            || !dependencies.password_deriver || config.session_ttl_seconds <= 0
            || config.remember_ttl_seconds <= 0 || config.max_password_utf8_bytes == 0
            || config.max_sessions == 0 || config.max_remembered_logins == 0
            || config.max_subscriptions == 0 || config.max_revocations_per_subscription == 0
            || config.max_file_bytes == 0 || config.max_token_bytes < 256
            || config.max_identifier_bytes < 32
            || config.max_concurrent_password_derivations == 0) {
            return result_error<std::unique_ptr<AuthOwner>>(AuthError::invalid_argument);
        }
        if (!dependencies.signing_seed_override) {
            dependencies.signing_seed_override = environment_secret(
                "BAAS_SERVICE_SIGN_SEED_B64", ed25519_seed_bytes);
        }
        if (!dependencies.ticket_key_override) {
            dependencies.ticket_key_override = environment_secret(
                "BAAS_SERVICE_TICKET_KEY_B64", auth_key_bytes);
        }
        auto impl = std::make_unique<Impl>(std::move(config), std::move(dependencies));
        const auto loaded = impl->load();
        if (loaded != AuthError::none) {
            return result_error<std::unique_ptr<AuthOwner>>(loaded);
        }
        return {std::optional<std::unique_ptr<AuthOwner>>{
                    std::unique_ptr<AuthOwner>{new AuthOwner(std::move(impl))}},
                AuthError::none};
    }
    catch (const std::invalid_argument&) {
        return result_error<std::unique_ptr<AuthOwner>>(AuthError::invalid_argument);
    }
    catch (...) {
        return result_error<std::unique_ptr<AuthOwner>>(AuthError::storage_failure);
    }
}

PasswordPublicState AuthOwner::password_state() const
{
    std::lock_guard lock(impl_->mutex_);
    return {impl_->password_.initialized, impl_->password_.epoch, impl_->password_.salt};
}

PublicBytes AuthOwner::signing_public_key() const
{
    std::lock_guard lock(impl_->mutex_);
    return impl_->signing_public_;
}

AuthResult<ControlHandshakeMaterial> AuthOwner::begin_control_handshake(
    ControlClientHello hello) noexcept
{
    try {
        if (hello.timestamp < 0
            || hello.timestamp > maximum_safe_json_integer
            || hello.client_nonce.size() != x25519_key_bytes
            || hello.client_kx_public.size() != x25519_key_bytes) {
            return result_error<ControlHandshakeMaterial>(AuthError::invalid_argument);
        }
        std::lock_guard lock(impl_->mutex_);
        SecretBuffer server_private{x25519_key_bytes};
        PublicBytes server_nonce(x25519_key_bytes);
        if (!impl_->dependencies_.random->fill(server_private.mutable_bytes())
            || !impl_->dependencies_.random->fill(server_nonce)) {
            return result_error<ControlHandshakeMaterial>(AuthError::entropy_failure);
        }
        auto server_public = x25519_public_key(server_private.bytes());
        auto shared = x25519_shared_secret(
            server_private.bytes(), hello.client_kx_public);
        auto client_nonce_b64 = b64(hello.client_nonce);
        auto client_public_b64 = b64(hello.client_kx_public);
        auto server_nonce_b64 = b64(server_nonce);
        auto server_public_b64 = server_public ? b64(*server_public.value) : std::nullopt;
        std::optional<std::string> salt_b64;
        if (impl_->password_.initialized) salt_b64 = b64(impl_->password_.salt);
        if (!shared) {
            return result_error<ControlHandshakeMaterial>(
                AuthError::authentication_failed);
        }
        if (!server_public || !client_nonce_b64 || !client_public_b64
            || !server_nonce_b64 || !server_public_b64
            || (impl_->password_.initialized && !salt_b64)) {
            return result_error<ControlHandshakeMaterial>(AuthError::crypto_failure);
        }

        CanonicalJsonValue client{CanonicalJsonValue::Object{
            {"type", string_value("client_hello")},
            {"kind", string_value("control")},
            {"channel", string_value("control")},
            {"version", CanonicalJsonValue{std::int64_t{1}}},
            {"timestamp", CanonicalJsonValue{hello.timestamp}},
            {"client_nonce", string_value(std::move(*client_nonce_b64))},
            {"client_kx_pub", string_value(std::move(*client_public_b64))},
        }};
        CanonicalJsonValue argon2{CanonicalJsonValue::Object{
            {"algorithm", string_value("argon2id")},
            {"hash_bytes", CanonicalJsonValue{
                static_cast<std::int64_t>(argon2id_output_bytes)}},
            {"memlimit", CanonicalJsonValue{
                static_cast<std::int64_t>(argon2id_v1_memlimit)}},
            {"opslimit", CanonicalJsonValue{
                static_cast<std::int64_t>(argon2id_v1_opslimit)}},
            {"salt_bytes", CanonicalJsonValue{
                static_cast<std::int64_t>(argon2id_salt_bytes)}},
        }};
        CanonicalJsonValue server_core{CanonicalJsonValue::Object{
            {"type", string_value("server_hello")},
            {"kind", string_value("control")},
            {"channel", string_value("control")},
            {"version", CanonicalJsonValue{std::int64_t{1}}},
            {"initialized", CanonicalJsonValue{impl_->password_.initialized}},
            {"pwd_epoch", CanonicalJsonValue{
                static_cast<std::int64_t>(impl_->password_.epoch)}},
            {"pwd_salt", impl_->password_.initialized
                ? string_value(std::move(*salt_b64)) : CanonicalJsonValue{}},
            {"argon2", std::move(argon2)},
            {"server_nonce", string_value(std::move(*server_nonce_b64))},
            {"server_kx_pub", string_value(std::move(*server_public_b64))},
        }};
        CanonicalJsonValue transcript{CanonicalJsonValue::Object{
            {"kind", string_value("control")},
            {"channel", string_value("control")},
            {"client", std::move(client)},
            {"server", server_core},
        }};
        auto encoded_transcript = encode_canonical_json_value(transcript);
        if (!encoded_transcript) {
            return result_error<ControlHandshakeMaterial>(AuthError::crypto_failure);
        }
        auto signature = ed25519_sign(
            impl_->signing_seed_.bytes(), bytes_of(encoded_transcript.text));
        auto transcript_hash = sha256(bytes_of(encoded_transcript.text));
        auto signature_b64 = signature ? b64(*signature.value) : std::nullopt;
        auto sign_public_b64 = b64(impl_->signing_public_);
        if (!signature || !transcript_hash || !signature_b64 || !sign_public_b64) {
            return result_error<ControlHandshakeMaterial>(AuthError::crypto_failure);
        }
        auto response_object = *server_core.as_object();
        response_object.emplace_back(
            "signature", string_value(std::move(*signature_b64)));
        response_object.emplace_back(
            "server_sign_pub", string_value(std::move(*sign_public_b64)));
        auto response = encode_canonical_json_value(
            CanonicalJsonValue{std::move(response_object)});
        if (!response) {
            return result_error<ControlHandshakeMaterial>(AuthError::crypto_failure);
        }
        ControlHandshakeMaterial material{
            std::move(response.text),
            HandshakeMaterial{
                std::move(*shared.value), std::move(*transcript_hash.value)}};
        return {
            std::optional<ControlHandshakeMaterial>{std::move(material)},
            AuthError::none};
    }
    catch (...) {
        return result_error<ControlHandshakeMaterial>(AuthError::crypto_failure);
    }
}

AuthStatus AuthOwner::initialize_password(SecretBuffer password) noexcept
{
    try {
        return impl_->initialize(std::move(password));
    }
    catch (...) {
        return status(AuthError::storage_failure);
    }
}

AuthStatus AuthOwner::change_password(
    const std::string_view session_id, SecretBuffer password) noexcept
{
    try {
        return impl_->rotate(
            session_id, std::move(password), RevocationReason::password_changed);
    }
    catch (...) {
        return status(AuthError::storage_failure);
    }
}

AuthStatus AuthOwner::reset_password(SecretBuffer password) noexcept
{
    try {
        return impl_->rotate(
            std::nullopt, std::move(password), RevocationReason::password_reset);
    }
    catch (...) {
        return status(AuthError::storage_failure);
    }
}

AuthResult<ControlSessionMaterial> AuthOwner::initialize_control(
    HandshakeMaterial handshake, SecretBuffer password) noexcept
{
    return impl_->initialize_control(std::move(handshake), std::move(password));
}

AuthResult<ControlSessionMaterial> AuthOwner::authenticate_control(
    HandshakeMaterial handshake, const std::span<const std::byte> proof) noexcept
{
    return impl_->password_session(std::move(handshake), proof);
}

AuthResult<ControlSessionMaterial> AuthOwner::resume_control(
    HandshakeMaterial handshake, SecretBuffer remember_token) noexcept
{
    try {
        if (handshake.shared_key.size() != x25519_key_bytes
            || handshake.transcript_hash.size() != sha256_bytes) {
            return result_error<ControlSessionMaterial>(AuthError::invalid_argument);
        }
        if (remember_token.empty() || remember_token.size() > impl_->config_.max_token_bytes) {
            return result_error<ControlSessionMaterial>(AuthError::invalid_remember_token);
        }
        const auto token = text_of(remember_token.bytes());
        const auto first = token.find('.');
        const auto second = first == std::string_view::npos
            ? std::string_view::npos : token.find('.', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos
            || token.find('.', second + 1) != std::string_view::npos
            || token.substr(0, first) != "v1") {
            return result_error<ControlSessionMaterial>(AuthError::invalid_remember_token);
        }
        const auto id = token.substr(first + 1, second - first - 1);
        if (!valid_identifier(id, impl_->config_.max_identifier_bytes)) {
            return result_error<ControlSessionMaterial>(AuthError::invalid_remember_token);
        }
        auto decoded = decode_base64url_canonical(token.substr(second + 1), auth_key_bytes);
        if (!decoded) return result_error<ControlSessionMaterial>(AuthError::invalid_remember_token);
        SecretBuffer secret{*decoded.value};
        wipe(*decoded.value);

        std::lock_guard lock(impl_->mutex_);
        const auto now = impl_->dependencies_.clock->now_unix_seconds();
        std::erase_if(impl_->remembered_, [&](const auto& item) {
            return item.second.expires_at < now || item.second.epoch != impl_->password_.epoch;
        });
        const auto found = impl_->remembered_.find(std::string{id});
        if (found == impl_->remembered_.end()) {
            return result_error<ControlSessionMaterial>(AuthError::invalid_remember_token);
        }
        auto context = concatenate(bytes_of("remember-token:"), bytes_of(id));
        auto full_context = concatenate(context.bytes(), bytes_of(":"));
        auto with_secret = concatenate(full_context.bytes(), secret.bytes());
        auto expected = hmac_sha256(impl_->ticket_key_.bytes(), with_secret.bytes());
        if (!expected || !constant_time_equal(*expected.value, found->second.hash)) {
            return result_error<ControlSessionMaterial>(AuthError::invalid_remember_token);
        }
        if (found->second.epoch != impl_->password_.epoch) {
            return result_error<ControlSessionMaterial>(AuthError::stale_epoch);
        }
        if (impl_->sessions_.size() >= impl_->config_.max_sessions) {
            impl_->prune_sessions_locked();
            if (impl_->sessions_.size() >= impl_->config_.max_sessions) {
                return result_error<ControlSessionMaterial>(AuthError::capacity_exceeded);
            }
        }
        SecretBuffer master{auth_key_bytes};
        if (!impl_->dependencies_.random->fill(master.mutable_bytes())) {
            return result_error<ControlSessionMaterial>(AuthError::entropy_failure);
        }
        auto resume = hkdf_sha256(
            master.bytes(), handshake.transcript_hash,
            bytes_of("resume-secret"), auth_key_bytes);
        if (!resume) return result_error<ControlSessionMaterial>(crypto_to_auth(resume.error));
        return impl_->insert_session_locked(
            std::move(master), std::move(*resume.value), impl_->password_.epoch, true);
    }
    catch (...) {
        return result_error<ControlSessionMaterial>(AuthError::crypto_failure);
    }
}

AuthResult<RememberTokenMaterial> AuthOwner::issue_remember_token(
    const std::string_view session_id,
    const std::span<const std::byte> proof) noexcept
{
    try {
        std::lock_guard persistence_lock(impl_->persistence_mutex_);
        std::unique_lock lock(impl_->mutex_);
        const auto required = impl_->require_session_locked(session_id);
        if (required != AuthError::none) return result_error<RememberTokenMaterial>(required);
        const auto& session = impl_->sessions_.at(std::string{session_id});
        if (proof.size() != hmac_sha256_bytes) {
            return result_error<RememberTokenMaterial>(AuthError::invalid_remember_proof);
        }
        auto proof_payload = encode_canonical_json_value(CanonicalJsonValue{
            CanonicalJsonValue::Object{
                {"type", string_value("remember_session")},
                {"session_id", string_value(session.id)},
                {"pwd_epoch", CanonicalJsonValue{static_cast<std::int64_t>(session.epoch)}},
            }});
        if (!proof_payload) return result_error<RememberTokenMaterial>(AuthError::crypto_failure);
        auto expected = hmac_sha256(session.resume.bytes(), bytes_of(proof_payload.text));
        if (!expected || !constant_time_equal(*expected.value, proof)) {
            return result_error<RememberTokenMaterial>(AuthError::invalid_remember_proof);
        }
        const auto now = impl_->dependencies_.clock->now_unix_seconds();
        std::erase_if(impl_->remembered_, [&](const auto& item) {
            return item.second.expires_at < now || item.second.epoch != impl_->password_.epoch;
        });
        if (impl_->remembered_.size() >= impl_->config_.max_remembered_logins) {
            return result_error<RememberTokenMaterial>(AuthError::capacity_exceeded);
        }
        std::array<std::byte, 16> id_bytes{};
        SecretBuffer token_secret{auth_key_bytes};
        if (!impl_->dependencies_.random->fill(id_bytes)
            || !impl_->dependencies_.random->fill(token_secret.mutable_bytes())) {
            return result_error<RememberTokenMaterial>(AuthError::entropy_failure);
        }
        set_uuid_v4_bits(id_bytes);
        const auto id = hex(id_bytes);
        if (impl_->remembered_.contains(id)) {
            return result_error<RememberTokenMaterial>(AuthError::entropy_failure);
        }
        if (now < 0 || impl_->config_.remember_ttl_seconds > maximum_safe_json_integer - now) {
            return result_error<RememberTokenMaterial>(AuthError::invalid_argument);
        }
        const auto expires = now + impl_->config_.remember_ttl_seconds;
        auto prefix = concatenate(bytes_of("remember-token:"), bytes_of(id));
        auto separator = concatenate(prefix.bytes(), bytes_of(":"));
        auto hash_input = concatenate(separator.bytes(), token_secret.bytes());
        auto hash = hmac_sha256(impl_->ticket_key_.bytes(), hash_input.bytes());
        if (!hash) {
            return result_error<RememberTokenMaterial>(AuthError::crypto_failure);
        }
        auto encoded_secret = b64(token_secret.bytes());
        if (!encoded_secret) {
            return result_error<RememberTokenMaterial>(AuthError::crypto_failure);
        }
        WipingString secret_b64;
        secret_b64.value.swap(*encoded_secret);
        WipingString token;
        token.value = "v1." + id + "." + secret_b64.value;
        if (token.value.size() > impl_->config_.max_token_bytes) {
            return result_error<RememberTokenMaterial>(AuthError::capacity_exceeded);
        }
        RememberTokenMaterial material{SecretBuffer{bytes_of(token.value)}, expires};
        Impl::Remembered value{id, *hash.value, now, expires, session.epoch};
        auto staged = impl_->remembered_;
        staged.emplace(id, value);
        lock.unlock();
        auto json = impl_->remembered_json(staged);
        if (!json || !impl_->dependencies_.storage->write_atomic(
                AuthFile::remembered_logins, bytes_of(*json))) {
            return result_error<RememberTokenMaterial>(AuthError::storage_failure);
        }
        lock.lock();
        impl_->remembered_.emplace(id, std::move(value));
        return {std::optional<RememberTokenMaterial>{std::move(material)}, AuthError::none};
    }
    catch (...) {
        return result_error<RememberTokenMaterial>(AuthError::crypto_failure);
    }
}

AuthStatus AuthOwner::logout_remember_token(SecretBuffer token) noexcept
{
    try {
        if (token.empty() || token.size() > impl_->config_.max_token_bytes) {
            return status(AuthError::invalid_remember_token);
        }
        const auto text = text_of(token.bytes());
        const auto first = text.find('.');
        const auto second = first == std::string_view::npos
            ? std::string_view::npos : text.find('.', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos
            || text.find('.', second + 1) != std::string_view::npos
            || text.substr(0, first) != "v1") return status(AuthError::invalid_remember_token);
        const auto id = text.substr(first + 1, second - first - 1);
        if (!valid_identifier(id, impl_->config_.max_identifier_bytes)) {
            return status(AuthError::invalid_remember_token);
        }
        auto decoded = decode_base64url_canonical(text.substr(second + 1), auth_key_bytes);
        if (!decoded) return status(AuthError::invalid_remember_token);
        SecretBuffer secret{*decoded.value};
        wipe(*decoded.value);

        std::lock_guard persistence_lock(impl_->persistence_mutex_);
        std::unique_lock lock(impl_->mutex_);
        const auto found = impl_->remembered_.find(std::string{id});
        if (found == impl_->remembered_.end()) return status();
        auto prefix = concatenate(bytes_of("remember-token:"), bytes_of(id));
        auto separator = concatenate(prefix.bytes(), bytes_of(":"));
        auto input = concatenate(separator.bytes(), secret.bytes());
        auto expected = hmac_sha256(impl_->ticket_key_.bytes(), input.bytes());
        if (!expected || !constant_time_equal(*expected.value, found->second.hash)) {
            return status(AuthError::invalid_remember_token);
        }
        auto staged = impl_->remembered_;
        staged.erase(std::string{id});
        lock.unlock();
        auto json = impl_->remembered_json(staged);
        if (!json || !impl_->dependencies_.storage->write_atomic(
                AuthFile::remembered_logins, bytes_of(*json))) return status(AuthError::storage_failure);
        lock.lock();
        impl_->remembered_.erase(std::string{id});
        return status();
    }
    catch (...) {
        return status(AuthError::crypto_failure);
    }
}

AuthStatus AuthOwner::verify_resume_ticket(
    const std::string_view session_id,
    const std::span<const std::byte> ticket) noexcept
{
    try {
        if (!valid_identifier(session_id, impl_->config_.max_identifier_bytes)
            || ticket.empty() || ticket.size() > impl_->config_.max_token_bytes) {
            return status(AuthError::invalid_resume_ticket);
        }
        const auto text = text_of(ticket);
        const auto dot = text.find('.');
        if (dot == std::string_view::npos || text.find('.', dot + 1) != std::string_view::npos) {
            return status(AuthError::invalid_resume_ticket);
        }
        auto payload = decode_base64url_canonical(text.substr(0, dot));
        auto signature = decode_base64url_canonical(text.substr(dot + 1), hmac_sha256_bytes);
        if (!payload || !signature || payload.value->size() > impl_->config_.max_file_bytes) {
            return status(AuthError::invalid_resume_ticket);
        }
        auto expected = hmac_sha256(impl_->ticket_key_.bytes(), *payload.value);
        if (!expected || !constant_time_equal(*expected.value, *signature.value)) {
            return status(AuthError::invalid_resume_ticket);
        }
        const auto payload_text = text_of(*payload.value);
        const auto parsed = parse_canonical_json_value(payload_text);
        if (!parsed || parsed.value->as_object() == nullptr
            || !exact_object_fields(*parsed.value->as_object(), {"session_id", "pwd_epoch", "expires_at"})) {
            return status(AuthError::invalid_resume_ticket);
        }
        auto canonical = encode_canonical_json_value(*parsed.value);
        const auto* id = parsed.value->find("session_id")->as_string();
        const auto epoch = as_epoch(parsed.value->find("pwd_epoch"));
        const auto expires = as_time(parsed.value->find("expires_at"));
        if (!canonical || canonical.text != payload_text || id == nullptr || !epoch || !expires
            || *id != session_id) return status(AuthError::invalid_resume_ticket);

        std::lock_guard lock(impl_->mutex_);
        const auto required = impl_->require_session_locked(session_id);
        if (required != AuthError::none) return status(required);
        const auto& session = impl_->sessions_.at(std::string{session_id});
        if (*epoch != session.epoch || *expires != session.expires_at) {
            return status(AuthError::invalid_resume_ticket);
        }
        return status();
    }
    catch (...) {
        return status(AuthError::invalid_resume_ticket);
    }
}

AuthStatus AuthOwner::validate_session(const std::string_view session_id) noexcept
{
    try {
        std::lock_guard lock(impl_->mutex_);
        return status(impl_->require_session_locked(session_id));
    }
    catch (...) {
        return status(AuthError::crypto_failure);
    }
}

void AuthOwner::close_session(const std::string_view session_id) noexcept
{
    try {
        std::lock_guard lock(impl_->mutex_);
        impl_->sessions_.erase(std::string{session_id});
        std::erase_if(impl_->subscriptions_, [&](const auto& item) {
            return item.second.session_id == session_id;
        });
    }
    catch (...) {}
}

std::size_t AuthOwner::active_session_count() const noexcept
{
    try {
        std::lock_guard lock(impl_->mutex_);
        impl_->prune_sessions_locked();
        return impl_->sessions_.size();
    }
    catch (...) { return 0; }
}

std::size_t AuthOwner::remembered_login_count() const noexcept
{
    try {
        std::lock_guard lock(impl_->mutex_);
        const auto now = impl_->dependencies_.clock->now_unix_seconds();
        std::erase_if(impl_->remembered_, [&](const auto& item) {
            return item.second.expires_at < now || item.second.epoch != impl_->password_.epoch;
        });
        return impl_->remembered_.size();
    }
    catch (...) { return 0; }
}

AuthResult<SubscriptionId> AuthOwner::subscribe_revocations(
    const std::string_view session_id) noexcept
{
    try {
        std::lock_guard lock(impl_->mutex_);
        const auto required = impl_->require_session_locked(session_id);
        if (required != AuthError::none) return result_error<SubscriptionId>(required);
        if (impl_->subscriptions_.size() >= impl_->config_.max_subscriptions
            || impl_->next_subscription_ == 0) {
            return result_error<SubscriptionId>(AuthError::capacity_exceeded);
        }
        const auto id = impl_->next_subscription_++;
        impl_->subscriptions_.emplace(id, Impl::Subscription{std::string{session_id}, {}});
        return {id, AuthError::none};
    }
    catch (...) {
        return result_error<SubscriptionId>(AuthError::capacity_exceeded);
    }
}

AuthResult<std::vector<RevocationEvent>> AuthOwner::drain_revocations(
    const SubscriptionId subscription, const std::size_t maximum) noexcept
{
    try {
        if (maximum == 0 || maximum > impl_->config_.max_revocations_per_subscription) {
            return result_error<std::vector<RevocationEvent>>(AuthError::invalid_argument);
        }
        std::lock_guard lock(impl_->mutex_);
        const auto found = impl_->subscriptions_.find(subscription);
        if (found == impl_->subscriptions_.end()) {
            return result_error<std::vector<RevocationEvent>>(AuthError::subscription_not_found);
        }
        std::vector<RevocationEvent> events;
        const auto count = std::min(maximum, found->second.events.size());
        events.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            events.push_back(std::move(found->second.events.front()));
            found->second.events.pop_front();
        }
        return {std::move(events), AuthError::none};
    }
    catch (...) {
        return result_error<std::vector<RevocationEvent>>(AuthError::capacity_exceeded);
    }
}

void AuthOwner::unsubscribe_revocations(const SubscriptionId subscription) noexcept
{
    try {
        std::lock_guard lock(impl_->mutex_);
        impl_->subscriptions_.erase(subscription);
    }
    catch (...) {}
}

}  // namespace baas::service::auth
