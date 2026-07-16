#include "runtime/repository/RuntimeRepositoryGit2.h"
#include "RuntimeRepositoryGit2TestHooks.h"

#include <git2.h>
#include <miniz.h>

#include <atomic>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace repository = baas::runtime::repository;

namespace {

int failures{};

void check(const bool condition, const std::string_view message) {
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

template <typename Callable>
void check_throws(Callable&& callable, const std::string_view message) {
    try {
        callable();
    } catch (...) {
        return;
    }
    check(false, message);
}

void require_git(const int result, const std::string_view operation) {
    if (result >= 0)
        return;
    std::string detail(operation);
    if (const auto* error = git_error_last(); error != nullptr && error->message != nullptr)
        detail += ": " + std::string(error->message);
    throw std::runtime_error(detail);
}

template <typename Type, void (*Free)(Type*)>
using GitOwner = std::unique_ptr<Type, decltype(Free)>;

class TempDirectory final {
  public:
    TempDirectory() {
        static std::atomic<unsigned long long> next{};
        std::random_device random;
#ifdef _WIN32
        const auto process = static_cast<unsigned long long>(GetCurrentProcessId());
#else
        const auto process = static_cast<unsigned long long>(getpid());
#endif
        for (std::size_t attempt = 0; attempt < 128; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                    ("baas-git2-backend-" + std::to_string(process) + "-" +
                     std::to_string(++next) + "-" + std::to_string(random()));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
                return;
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error("temporary directory creation failed",
                                                        path_, error);
        }
        throw std::runtime_error("temporary directory allocation exhausted");
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

constexpr std::string_view payload = "payload";
constexpr std::string_view manifest =
    "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[{"
    "\"path\":\"payload.bin\",\"size\":\"7\",\"sha256\":"
    "\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\","
    "\"mode\":\"file\"}]}\n";
constexpr std::string_view manifest_sha256 =
    "11888349804e7ade124720093248848fa5fa6077a5c9daaec656024e40225f29";

[[nodiscard]] std::string oid_text(const git_oid& oid) {
    char buffer[GIT_OID_SHA1_HEXSIZE + 1]{};
    if (git_oid_tostr(buffer, sizeof(buffer), &oid) == nullptr)
        throw std::runtime_error("fixture oid formatting failed");
    return buffer;
}

[[nodiscard]] std::string file_url(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    const std::string generic(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    return generic.starts_with('/') ? "file://" + generic : "file:///" + generic;
}

class RepositoryFixture final {
  public:
    explicit RepositoryFixture(const git_filemode_t payload_mode = GIT_FILEMODE_BLOB) {
        repository_path_ = temporary_.path() / "remote.git";
        git_repository* raw_repository{};
        require_git(git_repository_init(&raw_repository, repository_path_.string().c_str(), 1),
                    "fixture repository initialization failed");
        GitOwner<git_repository, git_repository_free> repository(raw_repository,
                                                                 &git_repository_free);

        git_oid manifest_oid{};
        git_oid payload_oid{};
        require_git(git_blob_create_from_buffer(&manifest_oid, repository.get(), manifest.data(),
                                                manifest.size()),
                    "fixture manifest blob creation failed");
        require_git(git_blob_create_from_buffer(&payload_oid, repository.get(), payload.data(),
                                                payload.size()),
                    "fixture payload blob creation failed");

        git_treebuilder* raw_builder{};
        require_git(git_treebuilder_new(&raw_builder, repository.get(), nullptr),
                    "fixture tree builder creation failed");
        GitOwner<git_treebuilder, git_treebuilder_free> builder(raw_builder,
                                                                &git_treebuilder_free);
        require_git(git_treebuilder_insert(nullptr, builder.get(), "manifest.json", &manifest_oid,
                                           GIT_FILEMODE_BLOB),
                    "fixture manifest insertion failed");
        require_git(git_treebuilder_insert(nullptr, builder.get(), "payload.bin", &payload_oid,
                                           payload_mode),
                    "fixture payload insertion failed");
        git_oid tree_oid{};
        require_git(git_treebuilder_write(&tree_oid, builder.get()),
                    "fixture tree write failed");
        git_tree* raw_tree{};
        require_git(git_tree_lookup(&raw_tree, repository.get(), &tree_oid),
                    "fixture tree lookup failed");
        GitOwner<git_tree, git_tree_free> tree(raw_tree, &git_tree_free);

        git_signature* raw_signature{};
        require_git(git_signature_new(&raw_signature, "BAAS Test", "test@example.invalid", 0, 0),
                    "fixture signature creation failed");
        GitOwner<git_signature, git_signature_free> signature(raw_signature, &git_signature_free);
        require_git(git_commit_create(&commit_oid_, repository.get(), "refs/heads/main",
                                      signature.get(), signature.get(), nullptr, "fixture", tree.get(),
                                      0, nullptr),
                    "fixture commit creation failed");

        git_object* raw_commit{};
        require_git(git_object_lookup(&raw_commit, repository.get(), &commit_oid_, GIT_OBJECT_COMMIT),
                    "fixture tag target lookup failed");
        GitOwner<git_object, git_object_free> commit(raw_commit, &git_object_free);
        git_oid tag_oid{};
        require_git(git_tag_create(&tag_oid, repository.get(), "release", commit.get(),
                                   signature.get(), "fixture tag", 0),
                    "fixture annotated tag creation failed");
    }

    [[nodiscard]] std::string url() const { return file_url(repository_path_); }
    [[nodiscard]] std::string commit() const { return oid_text(commit_oid_); }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return repository_path_; }
    [[nodiscard]] const git_oid& commit_oid() const noexcept { return commit_oid_; }

  private:
    TempDirectory temporary_;
    std::filesystem::path repository_path_;
    git_oid commit_oid_{};
};

[[nodiscard]] repository::RepositoryFetchSpec spec_for(const RepositoryFixture& fixture,
                                                       std::string reference = "refs/heads/main") {
    return {repository::RuntimeRepositoryId::Resources,
            fixture.url(),
            std::move(reference),
            fixture.commit(),
            "manifest.json",
            std::string(manifest_sha256)};
}

[[nodiscard]] repository::Libgit2RuntimeRepositoryFetchBackend backend(
    repository::Libgit2RuntimeRepositoryFetchLimits limits = {}) {
    repository::testing::set_file_transport_enabled(true);
    repository::Libgit2RuntimeRepositoryFetchOptions options;
    options.limits = limits;
    return repository::Libgit2RuntimeRepositoryFetchBackend(std::move(options));
}

void test_policy_rejections() {
    repository::testing::set_file_transport_enabled(false);
    repository::Libgit2RuntimeRepositoryFetchBackend production;
    TempDirectory staging;
    repository::RepositoryFetchSpec spec{repository::RuntimeRepositoryId::Resources,
                                         "file:///not-production.git", "refs/heads/main",
                                         std::string(40, 'a'), "manifest.json", std::string(64, 'b')};
    check_throws([&] { static_cast<void>(production.stage_exact(spec, staging.path(), {})); },
                 "production backend must reject file transport");

    auto local = backend();
    for (const std::string invalid_url : {"http://example.invalid/repo.git",
                                          "https://user@example.invalid/repo.git",
                                          "https://example.invalid/repo.git?x=1",
                                          "ssh://example.invalid/repo.git"}) {
        spec.remote_url = invalid_url;
        check_throws([&] { static_cast<void>(local.stage_exact(spec, staging.path(), {})); },
                     "backend must reject unsafe remote URL");
    }
    spec.remote_url = "file:///not-found.git";
    spec.advertised_reference = "main";
    check_throws([&] { static_cast<void>(local.stage_exact(spec, staging.path(), {})); },
                 "backend must require a full advertised ref");
    spec.advertised_reference = "refs/heads/main";
    spec.exact_commit = std::string(40, 'A');
    check_throws([&] { static_cast<void>(local.stage_exact(spec, staging.path(), {})); },
                 "backend must require lowercase exact SHA-1");

    repository::Libgit2RuntimeRepositoryFetchLimits limits;
    limits.absolute_timeout = std::chrono::hours(25);
    check_throws([&] { static_cast<void>(backend(limits)); },
                 "backend must bound its absolute timeout");
    limits = {};
    limits.absolute_timeout = std::chrono::seconds(1);
    limits.connect_timeout = std::chrono::seconds(2);
    check_throws([&] { static_cast<void>(backend(limits)); },
                 "connect timeout must not exceed absolute timeout");
    limits = {};
    limits.absolute_timeout = std::chrono::seconds(1);
    limits.stall_timeout = std::chrono::seconds(2);
    check_throws([&] { static_cast<void>(backend(limits)); },
                 "stall timeout must not exceed absolute timeout");
    limits = {};
    limits.max_pack_bytes = 256U * 1024U * 1024U + 1U;
    check_throws([&] { static_cast<void>(backend(limits)); },
                 "pack limit must not exceed its implementation hard maximum");
}

void test_real_local_fetch_and_tag_peel() {
    RepositoryFixture fixture;
    for (const std::string reference : {"refs/heads/main", "refs/tags/release"}) {
        TempDirectory staging;
        auto fetcher = backend();
        const auto result = fetcher.stage_exact(spec_for(fixture, reference), staging.path(), {});
        check(result.resolved_commit == fixture.commit(), "real fetch must resolve exact commit");
        check(std::filesystem::is_regular_file(staging.path() / "manifest.json"),
              "real fetch must materialize manifest");
        check(std::filesystem::is_regular_file(staging.path() / "payload.bin"),
              "real fetch must materialize payload");
        check(!std::filesystem::exists(staging.path() / ".baas-git-transport"),
              "real fetch must remove private transport ODB");
    }
}

void test_fail_closed_limits_modes_and_cleanup() {
    RepositoryFixture fixture;
    {
        auto limits = repository::Libgit2RuntimeRepositoryFetchLimits{};
        limits.max_odb_objects = 1;
        auto fetcher = backend(limits);
        TempDirectory staging;
        check_throws(
            [&] { static_cast<void>(fetcher.stage_exact(spec_for(fixture), staging.path(), {})); },
            "ODB object limit must fail closed");
        check(!std::filesystem::exists(staging.path() / ".baas-git-transport"),
              "failure must remove private transport ODB");
    }
    {
        auto limits = repository::Libgit2RuntimeRepositoryFetchLimits{};
        limits.tree.max_files = 1;
        auto fetcher = backend(limits);
        TempDirectory staging;
        check_throws(
            [&] { static_cast<void>(fetcher.stage_exact(spec_for(fixture), staging.path(), {})); },
            "tree file limit must fail closed");
    }
    {
        RepositoryFixture executable(GIT_FILEMODE_BLOB_EXECUTABLE);
        auto fetcher = backend();
        TempDirectory staging;
        check_throws(
            [&] { static_cast<void>(fetcher.stage_exact(spec_for(executable), staging.path(), {})); },
            "executable Git mode must be rejected");
    }
    {
        auto wrong = spec_for(fixture, "refs/tags/release");
        wrong.exact_commit[0] = wrong.exact_commit[0] == '0' ? '1' : '0';
        auto fetcher = backend();
        TempDirectory staging;
        check_throws([&] { static_cast<void>(fetcher.stage_exact(wrong, staging.path(), {})); },
                     "peeled commit mismatch must fail closed");
    }
}

void test_typed_cancellation() {
    RepositoryFixture fixture;
    auto fetcher = backend();
    TempDirectory staging;
    std::stop_source source;
    source.request_stop();
    bool typed{};
    try {
        static_cast<void>(fetcher.stage_exact(spec_for(fixture), staging.path(), source.get_token()));
    } catch (const repository::RuntimeRepositoryFetchCancelled&) {
        typed = true;
    }
    check(typed, "pre-cancelled fetch must raise typed cancellation");
}

void append_big_endian_u32(std::vector<unsigned char>& bytes, const std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>(value >> 24U));
    bytes.push_back(static_cast<unsigned char>(value >> 16U));
    bytes.push_back(static_cast<unsigned char>(value >> 8U));
    bytes.push_back(static_cast<unsigned char>(value));
}

void append_pack_header(std::vector<unsigned char>& bytes, const unsigned type,
                        std::uintmax_t size) {
    auto first = static_cast<unsigned char>((type << 4U) | (size & 0x0fU));
    size >>= 4U;
    if (size != 0)
        first |= 0x80U;
    bytes.push_back(first);
    while (size != 0) {
        auto next = static_cast<unsigned char>(size & 0x7fU);
        size >>= 7U;
        if (size != 0)
            next |= 0x80U;
        bytes.push_back(next);
    }
}

void append_delta_varint(std::vector<unsigned char>& bytes, std::uintmax_t value) {
    do {
        auto next = static_cast<unsigned char>(value & 0x7fU);
        value >>= 7U;
        if (value != 0)
            next |= 0x80U;
        bytes.push_back(next);
    } while (value != 0);
}

void test_delta_result_bomb_is_rejected_before_libgit2() {
    repository::Libgit2RuntimeRepositoryFetchLimits limits;
    std::vector<unsigned char> delta;
    append_delta_varint(delta, 1);
    append_delta_varint(delta, limits.max_odb_object_bytes + 1);
    delta.push_back(0);

    mz_ulong compressed_size = mz_compressBound(static_cast<mz_ulong>(delta.size()));
    std::vector<unsigned char> compressed(compressed_size);
    check(mz_compress2(compressed.data(), &compressed_size, delta.data(),
                       static_cast<mz_ulong>(delta.size()), MZ_BEST_COMPRESSION) == MZ_OK,
          "delta-bomb fixture compression must succeed");
    compressed.resize(compressed_size);

    std::vector<unsigned char> pack{'P', 'A', 'C', 'K'};
    append_big_endian_u32(pack, 2);
    append_big_endian_u32(pack, 1);
    append_pack_header(pack, 7, delta.size());
    pack.insert(pack.end(), GIT_OID_SHA1_SIZE, 0);
    pack.insert(pack.end(), compressed.begin(), compressed.end());
    pack.insert(pack.end(), GIT_OID_SHA1_SIZE, 0);

    check(!repository::testing::pack_is_safe(pack, limits),
          "delta result-size bomb must be rejected by transport preflight");
}

int collect_pack(void* bytes, const std::size_t size, void* context) {
    auto& pack = *static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<const unsigned char*>(bytes);
    pack.insert(pack.end(), begin, begin + size);
    return 0;
}

void test_libgit2_generated_pack_is_accepted() {
    RepositoryFixture fixture;
    git_repository* raw_repository{};
    require_git(git_repository_open_bare(&raw_repository, fixture.path().string().c_str()),
                "pack fixture repository open failed");
    GitOwner<git_repository, git_repository_free> repo(raw_repository, &git_repository_free);
    git_packbuilder* raw_builder{};
    require_git(git_packbuilder_new(&raw_builder, repo.get()),
                "pack fixture builder creation failed");
    GitOwner<git_packbuilder, git_packbuilder_free> builder(raw_builder, &git_packbuilder_free);
    require_git(git_packbuilder_insert_recur(builder.get(), &fixture.commit_oid(), nullptr),
                "pack fixture recursive insertion failed");
    std::vector<unsigned char> pack;
    require_git(git_packbuilder_foreach(builder.get(), &collect_pack, &pack),
                "pack fixture generation failed");
    check(repository::testing::pack_is_safe(
              pack, repository::Libgit2RuntimeRepositoryFetchLimits{}),
          "preflight must accept a real libgit2-generated pack");
}

[[nodiscard]] std::vector<unsigned char> safe_blob_pack() {
    mz_ulong compressed_size = mz_compressBound(static_cast<mz_ulong>(payload.size()));
    std::vector<unsigned char> compressed(compressed_size);
    if (mz_compress2(compressed.data(), &compressed_size,
                     reinterpret_cast<const unsigned char*>(payload.data()),
                     static_cast<mz_ulong>(payload.size()), MZ_BEST_COMPRESSION) != MZ_OK)
        throw std::runtime_error("safe pack fixture compression failed");
    compressed.resize(compressed_size);
    std::vector<unsigned char> pack{'P', 'A', 'C', 'K'};
    append_big_endian_u32(pack, 2);
    append_big_endian_u32(pack, 1);
    append_pack_header(pack, 3, payload.size());
    pack.insert(pack.end(), compressed.begin(), compressed.end());
    pack.insert(pack.end(), GIT_OID_SHA1_SIZE, 0);
    return pack;
}

void append_pkt_length(std::vector<char>& response, const std::size_t size) {
    constexpr char hex[] = "0123456789abcdef";
    response.push_back(hex[(size >> 12U) & 0x0fU]);
    response.push_back(hex[(size >> 8U) & 0x0fU]);
    response.push_back(hex[(size >> 4U) & 0x0fU]);
    response.push_back(hex[size & 0x0fU]);
}

void test_sideband_and_raw_pack_responses_are_supported() {
    const auto pack = safe_blob_pack();
    const repository::Libgit2RuntimeRepositoryFetchLimits limits;
    const std::string_view negotiation = "0008NAK\n0000";

    std::vector<char> raw(negotiation.begin(), negotiation.end());
    raw.insert(raw.end(), pack.begin(), pack.end());
    check(repository::testing::smart_response_is_safe(raw, limits),
          "v0 raw PACK response after negotiation flush must be accepted");

    std::vector<char> sideband(negotiation.begin(), negotiation.end());
    append_pkt_length(sideband, pack.size() + 5);
    sideband.push_back(1);
    sideband.insert(sideband.end(), pack.begin(), pack.end());
    sideband.insert(sideband.end(), {'0', '0', '0', '0'});
    check(repository::testing::smart_response_is_safe(sideband, limits),
          "side-band channel-1 PACK response must be accepted");
}

void test_pack_preflight_observes_cancel_and_deadline() {
    const auto pack = safe_blob_pack();
    const repository::Libgit2RuntimeRepositoryFetchLimits limits;
    check(repository::testing::pack_preflight_is_interrupted(pack, limits, true, false),
          "pack preflight object loop must observe cancellation");
    check(repository::testing::pack_preflight_is_interrupted(pack, limits, false, true),
          "pack preflight object loop must observe absolute deadline");
}

[[nodiscard]] std::vector<unsigned char>
ref_delta_pack(const std::vector<unsigned char>& delta) {
    mz_ulong compressed_size = mz_compressBound(static_cast<mz_ulong>(delta.size()));
    std::vector<unsigned char> compressed(compressed_size);
    if (mz_compress2(compressed.data(), &compressed_size, delta.data(),
                     static_cast<mz_ulong>(delta.size()), MZ_BEST_COMPRESSION) != MZ_OK)
        throw std::runtime_error("overflow delta fixture compression failed");
    compressed.resize(compressed_size);
    std::vector<unsigned char> pack{'P', 'A', 'C', 'K'};
    append_big_endian_u32(pack, 2);
    append_big_endian_u32(pack, 1);
    append_pack_header(pack, 7, delta.size());
    pack.insert(pack.end(), GIT_OID_SHA1_SIZE, 0);
    pack.insert(pack.end(), compressed.begin(), compressed.end());
    pack.insert(pack.end(), GIT_OID_SHA1_SIZE, 0);
    return pack;
}

void test_pack_size_varint_overflow_is_rejected() {
    const repository::Libgit2RuntimeRepositoryFetchLimits limits;
    std::vector<unsigned char> object_header_overflow{'P', 'A', 'C', 'K'};
    append_big_endian_u32(object_header_overflow, 2);
    append_big_endian_u32(object_header_overflow, 1);
    object_header_overflow.push_back(0xb0U);
    object_header_overflow.insert(object_header_overflow.end(), 10, 0xffU);
    object_header_overflow.insert(object_header_overflow.end(), GIT_OID_SHA1_SIZE, 0);
    check(!repository::testing::pack_is_safe(object_header_overflow, limits),
          "overlong pack object-size header must be rejected without truncation");

    std::vector<unsigned char> delta_varint_overflow(9, 0x80U);
    delta_varint_overflow.push_back(0x7fU);
    delta_varint_overflow.push_back(1U);
    delta_varint_overflow.push_back(0U);
    check(!repository::testing::pack_is_safe(ref_delta_pack(delta_varint_overflow), limits),
          "10-byte overflowing delta varint must be rejected without truncation");
}

} // namespace

int main() {
    if (git_libgit2_init() < 0) {
        std::cerr << "libgit2 initialization failed\n";
        return 1;
    }
    try {
        test_policy_rejections();
        test_real_local_fetch_and_tag_peel();
        test_fail_closed_limits_modes_and_cleanup();
        test_typed_cancellation();
        test_delta_result_bomb_is_rejected_before_libgit2();
        test_libgit2_generated_pack_is_accepted();
        test_sideband_and_raw_pack_responses_are_supported();
        test_pack_preflight_observes_cancel_and_deadline();
        test_pack_size_varint_overflow_is_rejected();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        ++failures;
    }
    git_libgit2_shutdown();
    if (failures != 0)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
