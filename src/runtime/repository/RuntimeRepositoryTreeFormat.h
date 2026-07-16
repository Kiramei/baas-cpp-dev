#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::repository::detail {

using Sha256Digest = std::array<std::byte, 32>;

class Sha256 final {
public:
    void update(std::span<const std::byte> input) noexcept;
    void update(std::string_view input) noexcept;
    [[nodiscard]] Sha256Digest finish() noexcept;

private:
    void transform() noexcept;
    std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
        0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::byte, 64> block_{};
    std::size_t block_size_{};
    std::uint64_t total_bytes_{};
};

[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);
[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);
[[nodiscard]] bool lower_hex(std::string_view value, std::size_t length) noexcept;
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view value);

class TreeFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct TreeFormatLimits final {
    std::size_t max_entries{16'383};
    std::uintmax_t max_file_bytes{256ULL * 1024ULL * 1024ULL};
    std::uintmax_t max_total_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
    std::size_t max_relative_path_bytes{1'024};
    std::size_t max_relative_path_depth{32};
};

struct TreeManifestEntry final {
    std::string path;
    std::uintmax_t size{};
    std::string sha256;
};

// Returns the portable comparison key or throws TreeFormatError.
[[nodiscard]] std::string portable_path_key(
    std::string_view value, const TreeFormatLimits& limits);

[[nodiscard]] std::vector<TreeManifestEntry> parse_tree_manifest(
    std::string_view bytes,
    std::string_view manifest_name,
    const TreeFormatLimits& limits);

}  // namespace baas::runtime::repository::detail
