#include "ConfigArchiveCodec.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace baas::service::adapters::config_archive {
namespace {

struct NormalizedPath {
    std::string value;
    std::string key;
    bool directory{};
    Error error{Error::none};
};

struct PathSet {
    std::unordered_set<std::string> all;
    std::unordered_set<std::string> files;

    [[nodiscard]] bool insert(const NormalizedPath& path)
    {
        if (!all.insert(path.key).second) return false;

        std::size_t separator = path.key.find('/');
        while (separator != std::string::npos) {
            if (files.contains(path.key.substr(0, separator))) return false;
            separator = path.key.find('/', separator + 1);
        }

        if (!path.directory) {
            const auto prefix = path.key + '/';
            if (std::ranges::any_of(all, [&](const std::string& existing) {
                    return existing.starts_with(prefix);
                })) {
                return false;
            }
            files.insert(path.key);
        }
        return true;
    }
};

[[nodiscard]] bool valid_utf8(const std::string_view input) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
    std::size_t index{};
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
        for (std::size_t offset{}; offset < trailing; ++offset) {
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

[[nodiscard]] bool reserved_component(const std::string_view component) noexcept
{
    auto base = component.substr(0, component.find('.'));
    std::array<char, 8> upper{};
    if (base.size() > upper.size()) return false;
    for (std::size_t index{}; index < base.size(); ++index) {
        const auto byte = static_cast<unsigned char>(base[index]);
        if (byte > 0x7fU) return false;
        upper[index] = static_cast<char>(std::toupper(byte));
    }
    const std::string_view value{upper.data(), base.size()};
    if (value == "CON" || value == "PRN" || value == "AUX"
        || value == "NUL") {
        return true;
    }
    if (value.size() == 4
        && (value.starts_with("COM") || value.starts_with("LPT"))
        && value[3] >= '1' && value[3] <= '9') {
        return true;
    }
    return false;
}

[[nodiscard]] NormalizedPath normalize_path(
    std::string input, const bool directory, const Limits& limits)
{
    NormalizedPath result;
    result.directory = directory;
    if (input.empty() || input.size() > limits.max_path_bytes
        || !valid_utf8(input)
        || input.find('\0') != std::string::npos) {
        result.error = input.size() > limits.max_path_bytes
            ? Error::capacity : Error::unsafe_path;
        return result;
    }
    std::replace(input.begin(), input.end(), '\\', '/');
    if (input.front() == '/' || input.starts_with("//")) {
        result.error = Error::unsafe_path;
        return result;
    }
    if (directory) {
        while (!input.empty() && input.back() == '/') input.pop_back();
    } else if (input.back() == '/') {
        result.error = Error::unsafe_path;
        return result;
    }
    if (input.empty()) {
        result.error = Error::unsafe_path;
        return result;
    }
    std::size_t depth{};
    std::size_t begin{};
    while (begin <= input.size()) {
        const auto end = input.find('/', begin);
        const auto component = input.substr(
            begin, end == std::string::npos ? input.size() - begin : end - begin);
        if (component.empty() || component == "." || component == ".."
            || component.find(':') != std::string::npos
            || component.back() == '.' || component.back() == ' '
            || reserved_component(component)) {
            result.error = Error::unsafe_path;
            return result;
        }
        for (const auto byte : component) {
            constexpr std::string_view forbidden{"<>\"|?*"};
            const auto value = static_cast<unsigned char>(byte);
            if (value < 0x20U || value == 0x7fU
                || forbidden.find(byte) != std::string_view::npos) {
                result.error = Error::unsafe_path;
                return result;
            }
        }
        if (++depth > limits.max_depth) {
            result.error = Error::capacity;
            return result;
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    result.value = std::move(input);
    result.key.reserve(result.value.size());
    for (const auto byte : result.value) {
        const auto value = static_cast<unsigned char>(byte);
        result.key.push_back(value <= 0x7fU
            ? static_cast<char>(std::tolower(value)) : byte);
    }
    return result;
}

[[nodiscard]] bool supported_physical_type(
    const mz_zip_archive_file_stat& stat) noexcept
{
    // The upper 16 bits carry POSIX mode when made-by OS is Unix. Reject every
    // explicit special type, especially symlinks. A zero type is common for
    // DOS-created archives and is classified by miniz's directory flag.
    constexpr std::uint32_t type_mask = 0170000U;
    constexpr std::uint32_t directory_type = 0040000U;
    constexpr std::uint32_t regular_type = 0100000U;
    constexpr std::uint16_t unix_creator = 3U;
    const auto creator = static_cast<std::uint16_t>(stat.m_version_made_by >> 8U);
    if (creator != unix_creator) return true;
    const auto mode = (stat.m_external_attr >> 16U) & 0xffffU;
    const auto type = mode & type_mask;
    if (type == 0) return true;
    return stat.m_is_directory ? type == directory_type : type == regular_type;
}

class Reader final {
public:
    Reader(std::span<const std::byte> bytes)
    {
        initialized_ = mz_zip_reader_init_mem(
            &archive_, bytes.data(), bytes.size(), 0) == MZ_TRUE;
    }
    ~Reader()
    {
        if (initialized_) static_cast<void>(mz_zip_reader_end(&archive_));
    }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return initialized_; }
    [[nodiscard]] mz_zip_archive* get() noexcept { return &archive_; }
    [[nodiscard]] mz_zip_error error() noexcept
    {
        return mz_zip_get_last_error(&archive_);
    }
private:
    mz_zip_archive archive_{};
    bool initialized_{};
};

class Writer final {
public:
    Writer()
    {
        initialized_ = mz_zip_writer_init_heap(&archive_, 0, 128U * 1'024U)
            == MZ_TRUE;
    }
    ~Writer()
    {
        if (initialized_) static_cast<void>(mz_zip_writer_end(&archive_));
    }
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return initialized_; }
    [[nodiscard]] mz_zip_archive* get() noexcept { return &archive_; }
    [[nodiscard]] mz_zip_error error() noexcept
    {
        return mz_zip_get_last_error(&archive_);
    }
private:
    mz_zip_archive archive_{};
    bool initialized_{};
};

[[nodiscard]] Error miniz_error(
    const mz_zip_error error, const Error fallback) noexcept
{
    return error == MZ_ZIP_ALLOC_FAILED ? Error::capacity : fallback;
}

[[nodiscard]] Error charge_entry(
    const std::uint64_t bytes, std::uint64_t& total, const Limits& limits) noexcept
{
    if (bytes > limits.max_entry_bytes
        || total > limits.max_total_bytes
        || bytes > limits.max_total_bytes - total) {
        return Error::capacity;
    }
    total += bytes;
    return Error::none;
}

}  // namespace

bool valid_limits(const Limits& limits) noexcept
{
    return limits.max_archive_bytes != 0 && limits.max_entries != 0
        && limits.max_entry_bytes != 0 && limits.max_total_bytes != 0
        && limits.max_path_bytes != 0 && limits.max_depth != 0
        && limits.max_compression_ratio != 0
        && limits.max_entry_bytes <= limits.max_total_bytes
        && limits.max_archive_bytes <= 256U * 1'024U * 1'024U
        && limits.max_total_bytes <= 256U * 1'024U * 1'024U
        && limits.max_entries <= 65'536 && limits.max_path_bytes <= 16'384
        && limits.max_depth <= 256;
}

DecodeResult decode(
    const std::span<const std::byte> archive, const std::stop_token stop,
    const Limits limits) try
{
    if (!valid_limits(limits)) return {{}, Error::internal_error};
    if (stop.stop_requested()) return {{}, Error::cancelled};
    if (archive.empty()) return {{}, Error::invalid_archive};
    if (archive.size() > limits.max_archive_bytes) return {{}, Error::capacity};
    Reader reader(archive);
    if (!reader) {
        return {{}, miniz_error(reader.error(), Error::invalid_archive)};
    }
    const auto count = mz_zip_reader_get_num_files(reader.get());
    if (count == 0) return {{}, Error::invalid_archive};
    if (count > limits.max_entries) return {{}, Error::capacity};

    DecodeResult result;
    result.entries.reserve(count);
    PathSet paths;
    paths.all.reserve(count);
    paths.files.reserve(count);
    std::uint64_t total{};
    for (mz_uint index{}; index < count; ++index) {
        if (stop.stop_requested()) return {{}, Error::cancelled};
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(reader.get(), index, &stat)) {
            return {{}, Error::invalid_archive};
        }
        const auto needed = mz_zip_reader_get_filename(reader.get(), index, nullptr, 0);
        if (needed < 2) return {{}, Error::unsafe_path};
        if (needed - 1 > limits.max_path_bytes) return {{}, Error::capacity};
        std::string filename(needed, '\0');
        if (mz_zip_reader_get_filename(
                reader.get(), index, filename.data(), needed) != needed) {
            return {{}, Error::invalid_archive};
        }
        filename.resize(needed - 1);
        if (filename.find('\0') != std::string::npos) {
            return {{}, Error::unsafe_path};
        }
        auto normalized = normalize_path(
            std::move(filename), stat.m_is_directory == MZ_TRUE, limits);
        if (normalized.error != Error::none) return {{}, normalized.error};
        if (!paths.insert(normalized)) {
            return {{}, Error::duplicate_path};
        }
        if (stat.m_is_encrypted || !stat.m_is_supported
            || !supported_physical_type(stat)) {
            return {{}, Error::unsupported_entry};
        }
        if (stat.m_is_directory) {
            if (stat.m_comp_size != 0 || stat.m_uncomp_size != 0
                || stat.m_crc32 != 0) {
                return {{}, Error::unsupported_entry};
            }
            continue;
        }
        const auto charged = charge_entry(stat.m_uncomp_size, total, limits);
        if (charged != Error::none) return {{}, charged};
        if (stat.m_uncomp_size != 0) {
            if (stat.m_comp_size == 0
                || stat.m_comp_size > std::numeric_limits<std::uint64_t>::max()
                        / limits.max_compression_ratio
                || stat.m_uncomp_size
                    > stat.m_comp_size * limits.max_compression_ratio) {
                return {{}, Error::capacity};
            }
        }
        Entry entry;
        entry.path = std::move(normalized.value);
        entry.bytes.resize(static_cast<std::size_t>(stat.m_uncomp_size));
        if (!mz_zip_reader_extract_to_mem(
                reader.get(), index, entry.bytes.data(), entry.bytes.size(), 0)) {
            return {{}, miniz_error(reader.error(), Error::invalid_archive)};
        }
        if (stat.m_uncomp_size == 0 && stat.m_crc32 != 0) {
            return {{}, Error::invalid_archive};
        }
        if (stop.stop_requested()) return {{}, Error::cancelled};
        result.entries.push_back(std::move(entry));
    }
    if (result.entries.empty()) return {{}, Error::invalid_archive};
    return result;
} catch (const std::bad_alloc&) {
    return {{}, Error::capacity};
} catch (...) {
    return {{}, Error::internal_error};
}

EncodeResult encode(
    const std::span<const Entry> entries, const std::stop_token stop,
    const Limits limits) try
{
    if (!valid_limits(limits)) return {{}, Error::internal_error};
    if (stop.stop_requested()) return {{}, Error::cancelled};
    if (entries.empty()) return {{}, Error::invalid_archive};
    if (entries.size() > limits.max_entries) return {{}, Error::capacity};
    PathSet paths;
    paths.all.reserve(entries.size());
    paths.files.reserve(entries.size());
    std::uint64_t total{};
    Writer writer;
    if (!writer) {
        return {{}, miniz_error(writer.error(), Error::internal_error)};
    }
    for (const auto& entry : entries) {
        if (stop.stop_requested()) return {{}, Error::cancelled};
        auto normalized = normalize_path(entry.path, false, limits);
        if (normalized.error != Error::none) return {{}, normalized.error};
        if (!paths.insert(normalized)) {
            return {{}, Error::duplicate_path};
        }
        const auto charged = charge_entry(entry.bytes.size(), total, limits);
        if (charged != Error::none) return {{}, charged};
        if (!mz_zip_writer_add_mem(
                writer.get(), normalized.value.c_str(), entry.bytes.data(),
                entry.bytes.size(),
                static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
            return {{}, miniz_error(writer.error(), Error::internal_error)};
        }
        if (stop.stop_requested()) return {{}, Error::cancelled};
    }
    void* raw{};
    std::size_t size{};
    if (!mz_zip_writer_finalize_heap_archive(writer.get(), &raw, &size)
        || raw == nullptr) {
        return {{}, miniz_error(writer.error(), Error::internal_error)};
    }
    const std::unique_ptr<void, decltype(&mz_free)> owned(raw, &mz_free);
    if (size > limits.max_archive_bytes) {
        return {{}, Error::capacity};
    }
    if (stop.stop_requested()) return {{}, Error::cancelled};
    EncodeResult result;
    result.bytes.resize(size);
    std::memcpy(result.bytes.data(), owned.get(), size);
    return result;
} catch (const std::bad_alloc&) {
    return {{}, Error::capacity};
} catch (...) {
    return {{}, Error::internal_error};
}

}  // namespace baas::service::adapters::config_archive
