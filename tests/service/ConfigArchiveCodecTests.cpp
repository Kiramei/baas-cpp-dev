#include "ConfigArchiveCodec.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace archive = baas::service::adapters::config_archive;

namespace {

struct CheckFailure final : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(expression) do { if (!(expression)) throw CheckFailure(#expression); } while (false)

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view value)
{
    std::vector<std::byte> result(value.size());
    if (!value.empty()) {
        std::memcpy(result.data(), value.data(), value.size());
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> raw_archive(
    const std::span<const archive::Entry> entries)
{
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 1'024)) {
        throw CheckFailure("mz_zip_writer_init_heap");
    }
    struct ZipEnd final {
        mz_zip_archive* zip;
        ~ZipEnd() { static_cast<void>(mz_zip_writer_end(zip)); }
    } end{&zip};

    for (const auto& entry : entries) {
        if (!mz_zip_writer_add_mem(
                &zip, entry.path.c_str(), entry.bytes.data(),
                entry.bytes.size(),
                static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
            throw CheckFailure(
                "mz_zip_writer_add_mem(" + entry.path + "): "
                + mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        }
    }
    void* raw{};
    std::size_t size{};
    if (!mz_zip_writer_finalize_heap_archive(&zip, &raw, &size) || raw == nullptr) {
        throw CheckFailure("mz_zip_writer_finalize_heap_archive");
    }
    const std::unique_ptr<void, decltype(&mz_free)> owned(raw, &mz_free);
    std::vector<std::byte> result(size);
    std::memcpy(result.data(), owned.get(), size);
    return result;
}

[[nodiscard]] std::size_t signature_offset(
    const std::span<const std::byte> input, const std::uint32_t signature)
{
    for (std::size_t offset{}; offset + 4 <= input.size(); ++offset) {
        const auto value = std::to_integer<std::uint32_t>(input[offset])
            | (std::to_integer<std::uint32_t>(input[offset + 1]) << 8U)
            | (std::to_integer<std::uint32_t>(input[offset + 2]) << 16U)
            | (std::to_integer<std::uint32_t>(input[offset + 3]) << 24U);
        if (value == signature) return offset;
    }
    throw CheckFailure("ZIP signature not found");
}

void put_u16(
    const std::span<std::byte> output, const std::size_t offset,
    const std::uint16_t value)
{
    CHECK(offset + 2 <= output.size());
    output[offset] = static_cast<std::byte>(value & 0xffU);
    output[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xffU);
}

void put_u32(
    const std::span<std::byte> output, const std::size_t offset,
    const std::uint32_t value)
{
    CHECK(offset + 4 <= output.size());
    for (std::size_t index{}; index < 4; ++index) {
        output[offset + index] = static_cast<std::byte>(
            (value >> static_cast<unsigned>(index * 8U)) & 0xffU);
    }
}

[[nodiscard]] archive::Entry entry(
    std::string path, const std::string_view content)
{
    return {std::move(path), bytes(content)};
}

void round_trip_including_empty_file()
{
    const std::array input{
        entry("config.json", R"({"name":"test"})"),
        entry("resource/空.txt", ""),
        entry("resource/data.bin", std::string_view{"a\0b", 3}),
    };
    const auto encoded = archive::encode(input, {});
    CHECK(encoded);
    CHECK(!encoded.bytes.empty());
    const auto decoded = archive::decode(encoded.bytes, {});
    CHECK(decoded);
    CHECK(decoded.entries.size() == input.size());
    for (std::size_t index{}; index < input.size(); ++index) {
        CHECK(decoded.entries[index].path == input[index].path);
        CHECK(decoded.entries[index].bytes == input[index].bytes);
    }

    for (int iteration{}; iteration < 32; ++iteration) {
        const auto repeated = archive::encode(input, {});
        CHECK(repeated);
        CHECK(archive::decode(repeated.bytes, {}));
    }
}

void directory_entries_are_bounded_and_ignored()
{
    const std::array input{
        entry("empty/", ""),
        entry("empty/config.json", "{}"),
    };
    const auto decoded = archive::decode(raw_archive(input), {});
    CHECK(decoded);
    CHECK(decoded.entries.size() == 1);
    CHECK(decoded.entries.front().path == "empty/config.json");

    const std::array only_directory{entry("empty/", "")};
    CHECK(archive::decode(raw_archive(only_directory), {}).error
        == archive::Error::invalid_archive);

    const std::array payload_directory_source{
        entry("hidden_", "payload"),
        entry("config.json", "{}"),
    };
    auto payload_directory = raw_archive(payload_directory_source);
    const auto payload_local = signature_offset(payload_directory, 0x04034b50U);
    const auto payload_central = signature_offset(payload_directory, 0x02014b50U);
    payload_directory[payload_local + 30 + 6] = static_cast<std::byte>('/');
    payload_directory[payload_central + 46 + 6] = static_cast<std::byte>('/');
    CHECK(archive::decode(payload_directory, {}).error
        == archive::Error::unsupported_entry);

    archive::Limits one_entry;
    one_entry.max_entries = 1;
    CHECK(archive::decode(raw_archive(input), {}, one_entry).error
        == archive::Error::capacity);
}

void unsafe_and_conflicting_paths_are_rejected()
{
    constexpr std::array unsafe{
        "../config.json", "/config.json", "\\config.json", "C:/config.json",
        "a//b", "a/./b", "a/../b", "a:b", "a./b", "a /b ",
        "CON.txt", "dir/NUL", "bad<name", "bad>name", "bad\"name",
        "bad|name", "bad?name", "bad*name", "file/",
    };
    for (const auto path : unsafe) {
        const std::array input{entry(path, "x")};
        CHECK(archive::encode(input, {}).error == archive::Error::unsafe_path);
    }
    std::string control{"bad"};
    control.push_back(static_cast<char>(0x7f));
    const std::array control_input{entry(control, "x")};
    CHECK(archive::encode(control_input, {}).error
        == archive::Error::unsafe_path);
    const std::array invalid_utf8{
        archive::Entry{std::string{"bad\xc3", 4}, bytes("x")},
    };
    CHECK(archive::encode(invalid_utf8, {}).error
        == archive::Error::unsafe_path);

    const std::array case_duplicate{
        entry("Config.json", "a"), entry("config.JSON", "b")};
    CHECK(archive::encode(case_duplicate, {}).error
        == archive::Error::duplicate_path);
    CHECK(archive::decode(raw_archive(case_duplicate), {}).error
        == archive::Error::duplicate_path);

    const std::array separator_duplicate{
        entry("a/b", "a"), entry("A\\B", "b")};
    CHECK(archive::encode(separator_duplicate, {}).error
        == archive::Error::duplicate_path);

    const std::array parent_first{
        entry("resource", "file"), entry("resource/icon.png", "child")};
    const std::array child_first{
        entry("resource/icon.png", "child"), entry("RESOURCE", "file")};
    CHECK(archive::encode(parent_first, {}).error
        == archive::Error::duplicate_path);
    CHECK(archive::encode(child_first, {}).error
        == archive::Error::duplicate_path);

    const std::array explicit_directory_collision{
        entry("resource/", ""), entry("RESOURCE", "file")};
    CHECK(archive::decode(raw_archive(explicit_directory_collision), {}).error
        == archive::Error::duplicate_path);
}

void limits_and_compression_ratio_are_enforced()
{
    const std::array one{entry("config.json", "1234")};
    archive::Limits entry_limit;
    entry_limit.max_entry_bytes = 3;
    entry_limit.max_total_bytes = 3;
    CHECK(archive::encode(one, {}, entry_limit).error
        == archive::Error::capacity);

    const std::array two{entry("a", "12"), entry("b", "34")};
    archive::Limits total_limit;
    total_limit.max_entry_bytes = 3;
    total_limit.max_total_bytes = 3;
    CHECK(archive::encode(two, {}, total_limit).error
        == archive::Error::capacity);
    archive::Limits count_limit;
    count_limit.max_entries = 1;
    CHECK(archive::encode(two, {}, count_limit).error
        == archive::Error::capacity);

    archive::Limits path_limit;
    path_limit.max_path_bytes = 3;
    CHECK(archive::encode(one, {}, path_limit).error
        == archive::Error::capacity);
    archive::Limits depth_limit;
    depth_limit.max_depth = 1;
    const std::array nested{entry("a/b", "x")};
    CHECK(archive::encode(nested, {}, depth_limit).error
        == archive::Error::capacity);

    archive::Limits output_limit;
    output_limit.max_archive_bytes = 8;
    CHECK(archive::encode(one, {}, output_limit).error
        == archive::Error::capacity);
    const auto normal = archive::encode(one, {});
    CHECK(normal);
    archive::Limits input_limit;
    input_limit.max_archive_bytes = normal.bytes.size() - 1;
    CHECK(archive::decode(normal.bytes, {}, input_limit).error
        == archive::Error::capacity);

    archive::Entry compressible{"large.bin", std::vector<std::byte>(16'384)};
    const std::array bomb_input{std::move(compressible)};
    const auto compressed = archive::encode(bomb_input, {});
    CHECK(compressed);
    archive::Limits ratio_limit;
    ratio_limit.max_compression_ratio = 2;
    CHECK(archive::decode(compressed.bytes, {}, ratio_limit).error
        == archive::Error::capacity);
}

void unsupported_entries_and_crc_corruption_are_rejected()
{
    const std::array input{entry("config.json", "payload")};

    auto encrypted = raw_archive(input);
    const auto encrypted_local = signature_offset(encrypted, 0x04034b50U);
    const auto encrypted_central = signature_offset(encrypted, 0x02014b50U);
    put_u16(encrypted, encrypted_local + 6, 1U);
    put_u16(encrypted, encrypted_central + 8, 1U);
    CHECK(archive::decode(encrypted, {}).error
        == archive::Error::unsupported_entry);

    auto method = raw_archive(input);
    const auto method_local = signature_offset(method, 0x04034b50U);
    const auto method_central = signature_offset(method, 0x02014b50U);
    put_u16(method, method_local + 8, 99U);
    put_u16(method, method_central + 10, 99U);
    CHECK(archive::decode(method, {}).error
        == archive::Error::unsupported_entry);

    auto symlink = raw_archive(input);
    const auto symlink_central = signature_offset(symlink, 0x02014b50U);
    put_u16(symlink, symlink_central + 4, 0x0314U);
    put_u32(symlink, symlink_central + 38, 0120777U << 16U);
    CHECK(archive::decode(symlink, {}).error
        == archive::Error::unsupported_entry);

    auto crc = raw_archive(input);
    const auto crc_central = signature_offset(crc, 0x02014b50U);
    put_u32(crc, crc_central + 16, 0xdeadbeefU);
    CHECK(archive::decode(crc, {}).error == archive::Error::invalid_archive);

    const std::array empty_input{entry("empty.bin", "")};
    auto empty_crc = raw_archive(empty_input);
    const auto empty_central = signature_offset(empty_crc, 0x02014b50U);
    put_u32(empty_crc, empty_central + 16, 1U);
    CHECK(archive::decode(empty_crc, {}).error
        == archive::Error::invalid_archive);
}

void cancellation_and_invalid_input_are_deterministic()
{
    const std::array input{entry("config.json", "{}")};
    const auto encoded = archive::encode(input, {});
    CHECK(encoded);

    std::stop_source cancelled;
    cancelled.request_stop();
    CHECK(archive::encode(input, cancelled.get_token()).error
        == archive::Error::cancelled);
    CHECK(archive::decode(encoded.bytes, cancelled.get_token()).error
        == archive::Error::cancelled);

    archive::Limits invalid;
    invalid.max_entries = 0;
    CHECK(!archive::valid_limits(invalid));
    CHECK(archive::encode(input, {}, invalid).error
        == archive::Error::internal_error);
    CHECK(archive::decode(encoded.bytes, {}, invalid).error
        == archive::Error::internal_error);

    CHECK(archive::decode({}, {}).error == archive::Error::invalid_archive);
    const std::array garbage{
        std::byte{0x50}, std::byte{0x4b}, std::byte{0x03}, std::byte{0x04}};
    CHECK(archive::decode(garbage, {}).error
        == archive::Error::invalid_archive);
}

}  // namespace

int main()
{
    const std::array tests{
        round_trip_including_empty_file,
        directory_entries_are_bounded_and_ignored,
        unsafe_and_conflicting_paths_are_rejected,
        limits_and_compression_ratio_are_enforced,
        unsupported_entries_and_crc_corruption_are_rejected,
        cancellation_and_invalid_input_are_deterministic,
    };
    try {
        for (const auto test : tests) test();
        std::cout << tests.size() << " ConfigArchiveCodec tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ConfigArchiveCodec test failed: " << error.what() << '\n';
        return 1;
    }
}
