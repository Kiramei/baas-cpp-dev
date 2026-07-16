#include <miniz.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string_view>

int main()
{
    constexpr std::string_view input = "BAAS miniz archive dependency";
    std::array<unsigned char, 128> compressed{};
    mz_ulong compressed_size = compressed.size();
    if (mz_compress2(
            compressed.data(),
            &compressed_size,
            reinterpret_cast<const unsigned char*>(input.data()),
            static_cast<mz_ulong>(input.size()),
            MZ_BEST_COMPRESSION) != MZ_OK) {
        return EXIT_FAILURE;
    }

    std::array<unsigned char, 128> restored{};
    mz_ulong restored_size = restored.size();
    if (mz_uncompress(
            restored.data(),
            &restored_size,
            compressed.data(),
            compressed_size) != MZ_OK) {
        return EXIT_FAILURE;
    }

    const auto restored_view = std::string_view{
        reinterpret_cast<const char*>(restored.data()),
        static_cast<std::size_t>(restored_size),
    };
    if (restored_view != input) {
        return EXIT_FAILURE;
    }

    mz_zip_archive writer{};
    if (!mz_zip_writer_init_heap(&writer, 0, 0) ||
        !mz_zip_writer_add_mem(
            &writer,
            "config.json",
            input.data(),
            input.size(),
            MZ_BEST_COMPRESSION)) {
        mz_zip_writer_end(&writer);
        return EXIT_FAILURE;
    }
    void* archive_data = nullptr;
    std::size_t archive_size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&writer, &archive_data, &archive_size)) {
        mz_zip_writer_end(&writer);
        return EXIT_FAILURE;
    }
    mz_zip_writer_end(&writer);

    mz_zip_archive reader{};
    if (!mz_zip_reader_init_mem(&reader, archive_data, archive_size, 0)) {
        mz_free(archive_data);
        return EXIT_FAILURE;
    }
    std::size_t extracted_size = 0;
    void* extracted = mz_zip_reader_extract_file_to_heap(
        &reader, "config.json", &extracted_size, 0);
    const bool archive_matches = extracted != nullptr &&
        std::string_view{static_cast<const char*>(extracted), extracted_size} == input;
    mz_free(extracted);
    mz_zip_reader_end(&reader);
    mz_free(archive_data);
    return archive_matches ? EXIT_SUCCESS : EXIT_FAILURE;
}
