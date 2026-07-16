#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace baas::service::adapters::config_archive {

struct Limits {
    std::size_t max_archive_bytes{64U * 1'024U * 1'024U};
    std::size_t max_entries{4'096};
    std::size_t max_entry_bytes{16U * 1'024U * 1'024U};
    std::size_t max_total_bytes{64U * 1'024U * 1'024U};
    std::size_t max_path_bytes{1'024};
    std::size_t max_depth{32};
    std::uint64_t max_compression_ratio{1'024};
};

enum class Error {
    none,
    cancelled,
    invalid_archive,
    unsafe_path,
    unsupported_entry,
    duplicate_path,
    capacity,
    internal_error,
};

struct Entry {
    std::string path;
    std::vector<std::byte> bytes;
};

struct DecodeResult {
    std::vector<Entry> entries;
    Error error{Error::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == Error::none;
    }
};

struct EncodeResult {
    std::vector<std::byte> bytes;
    Error error{Error::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == Error::none;
    }
};

[[nodiscard]] bool valid_limits(const Limits& limits) noexcept;
[[nodiscard]] DecodeResult decode(
    std::span<const std::byte> archive,
    std::stop_token stop,
    Limits limits = {});
[[nodiscard]] EncodeResult encode(
    std::span<const Entry> entries,
    std::stop_token stop,
    Limits limits = {});

}  // namespace baas::service::adapters::config_archive
