#include "runtime/repository/RuntimeRepositoryGit2.h"
#include "RuntimeRepositoryGit2TestHooks.h"

#include <git2.h>
#include <miniz.h>

#include <atomic>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <optional>
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
    explicit RepositoryFixture(const git_filemode_t payload_mode = GIT_FILEMODE_BLOB,
                               const std::string_view extra_name = {}) {
        repository_path_ = temporary_.path() / "remote.git";
        git_repository* raw_repository{};
        const auto repository_path_utf8 = repository::testing::git_path_bytes(repository_path_);
        require_git(git_repository_init(&raw_repository, repository_path_utf8.c_str(), 1),
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
        if (!extra_name.empty())
            require_git(git_treebuilder_insert(nullptr, builder.get(), std::string(extra_name).c_str(),
                                               &payload_oid, GIT_FILEMODE_BLOB),
                        "fixture special-path insertion failed");
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

    repository::Libgit2RuntimeRepositoryFetchOptions invalid_ca;
    invalid_ca.trusted_ca_bundle = "relative-ca.pem";
    check_throws(
        [&] { repository::Libgit2RuntimeRepositoryFetchBackend rejected(invalid_ca); },
        "relative trusted CA bundle paths must be rejected");

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

void test_non_ascii_staging_path_uses_utf8_for_libgit2() {
    RepositoryFixture fixture;
    TempDirectory owner;
    const auto staging = owner.path() / std::filesystem::path(u8"运行时资源");
    std::filesystem::create_directory(staging);
    const auto encoded = repository::testing::git_path_bytes(staging);
    check(encoded.find("\xE8\xBF\x90\xE8\xA1\x8C\xE6\x97\xB6\xE8\xB5\x84\xE6\xBA\x90") !=
              std::string::npos,
          "libgit2 filesystem paths must use UTF-8 bytes");
    auto fetcher = backend();
    static_cast<void>(fetcher.stage_exact(spec_for(fixture), staging, {}));
    check(std::filesystem::is_regular_file(staging / "payload.bin"),
          "non-ASCII staging paths must support a complete local fetch");
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
        RepositoryFixture special(GIT_FILEMODE_BLOB, "payload.bin:unmanifested");
        auto fetcher = backend();
        TempDirectory staging;
        check_throws(
            [&] { static_cast<void>(fetcher.stage_exact(spec_for(special), staging.path(), {})); },
            "portable path contract must reject an NTFS ADS name before materialization");
        check(std::filesystem::is_empty(staging.path()),
              "rejected special Git paths must leave no materialized filesystem side effect");
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

void append_pkt_length(std::vector<char>& response, std::size_t size);
[[nodiscard]] std::vector<unsigned char>
ref_delta_pack(const std::vector<unsigned char>& delta);

[[nodiscard]] std::vector<unsigned char> compressed(const std::vector<unsigned char>& input) {
    mz_ulong compressed_size = mz_compressBound(static_cast<mz_ulong>(input.size()));
    std::vector<unsigned char> output(compressed_size);
    if (mz_compress2(output.data(), &compressed_size, input.data(),
                     static_cast<mz_ulong>(input.size()), MZ_BEST_COMPRESSION) != MZ_OK)
        throw std::runtime_error("pack fixture compression failed");
    output.resize(compressed_size);
    return output;
}

void append_ofs_distance(std::vector<unsigned char>& bytes, std::uintmax_t distance) {
    std::array<unsigned char, 16> encoded{};
    std::size_t count{1};
    encoded[0] = static_cast<unsigned char>(distance & 0x7fU);
    while ((distance >>= 7U) != 0) {
        --distance;
        encoded[count++] = static_cast<unsigned char>(0x80U | (distance & 0x7fU));
    }
    while (count != 0)
        bytes.push_back(encoded[--count]);
}

[[nodiscard]] std::vector<unsigned char> ofs_delta_chain_pack(
    const std::size_t delta_count, const std::optional<std::uintmax_t> first_distance = {}) {
    std::vector<unsigned char> pack{'P', 'A', 'C', 'K'};
    append_big_endian_u32(pack, 2);
    append_big_endian_u32(pack, static_cast<std::uint32_t>(delta_count + 1));
    std::vector<std::size_t> starts;
    starts.push_back(pack.size());
    append_pack_header(pack, 3, 1);
    const auto base = compressed(std::vector<unsigned char>{'a'});
    pack.insert(pack.end(), base.begin(), base.end());
    const std::vector<unsigned char> delta{1, 1, 0x90U, 1};
    const auto compressed_delta = compressed(delta);
    for (std::size_t index = 0; index < delta_count; ++index) {
        const auto start = pack.size();
        append_pack_header(pack, 6, delta.size());
        const auto distance = index == 0 && first_distance
                                  ? *first_distance
                                  : static_cast<std::uintmax_t>(start - starts.back());
        append_ofs_distance(pack, distance);
        pack.insert(pack.end(), compressed_delta.begin(), compressed_delta.end());
        starts.push_back(start);
    }
    pack.insert(pack.end(), GIT_OID_SHA1_SIZE, 0);
    return pack;
}

void test_delta_result_bomb_is_rejected_before_libgit2() {
    repository::Libgit2RuntimeRepositoryFetchLimits limits;
    std::vector<unsigned char> delta;
    append_delta_varint(delta, 1);
    append_delta_varint(delta, limits.max_odb_object_bytes + 1);
    delta.push_back(1);
    delta.push_back('x');
    const auto compressed_delta = compressed(delta);

    std::vector<unsigned char> pack{'P', 'A', 'C', 'K'};
    append_big_endian_u32(pack, 2);
    append_big_endian_u32(pack, 2);
    const auto base_start = pack.size();
    append_pack_header(pack, 3, 1);
    const auto compressed_base = compressed(std::vector<unsigned char>{'a'});
    pack.insert(pack.end(), compressed_base.begin(), compressed_base.end());
    const auto delta_start = pack.size();
    append_pack_header(pack, 6, delta.size());
    append_ofs_distance(pack, delta_start - base_start);
    pack.insert(pack.end(), compressed_delta.begin(), compressed_delta.end());
    pack.insert(pack.end(), GIT_OID_SHA1_SIZE, 0);

    check(!repository::testing::pack_is_safe(pack, limits),
          "delta result-size bomb must be rejected by transport preflight");
}

void test_advertisement_protocol_state_is_strict() {
    const auto packet = [](const std::string_view packet_payload) {
        std::vector<char> result;
        append_pkt_length(result, packet_payload.size() + 4);
        result.insert(result.end(), packet_payload.begin(), packet_payload.end());
        return result;
    };
    auto valid = packet("# service=git-upload-pack\n");
    valid.insert(valid.end(), {'0', '0', '0', '0'});
    const auto reference = packet("0123456789012345678901234567890123456789 refs/heads/main\n");
    valid.insert(valid.end(), reference.begin(), reference.end());
    valid.insert(valid.end(), {'0', '0', '0', '0'});
    check(repository::testing::advertisement_is_safe(valid, 1),
          "canonical v0 advertisement must be accepted");
    check(!repository::testing::advertisement_is_safe(valid, 0),
          "every v0 record must consume the reference budget");

    auto duplicate_service = valid;
    const auto service = packet("# service=git-upload-pack\n");
    duplicate_service.insert(duplicate_service.end() - 4, service.begin(), service.end());
    check(!repository::testing::advertisement_is_safe(duplicate_service, 8),
          "service markers outside the first packet must be rejected");

    auto v2 = packet("version 2\n");
    const auto capability = packet("ls-refs=unborn\n");
    v2.insert(v2.end(), capability.begin(), capability.end());
    v2.insert(v2.end(), {'0', '0', '0', '0'});
    check(repository::testing::advertisement_is_safe(v2, 1),
          "canonical v2 capability advertisement must be accepted");
    check(!repository::testing::advertisement_is_safe(v2, 0),
          "v2 records must not bypass the record budget");
}

void test_https_endpoint_parsing_is_exact() {
    check(repository::testing::https_endpoint_is_valid("https://example.com/repository.git"),
          "default HTTPS port must be accepted");
    check(repository::testing::https_endpoint_is_valid(
              "https://example.com:8443/repository.git"),
          "explicit HTTPS port must be accepted");
    check(repository::testing::https_endpoint_is_valid("https://[::1]:443/repository.git"),
          "bracketed IPv6 HTTPS authority must be accepted");
    check(!repository::testing::https_endpoint_is_valid(
              "https://example.com:443junk/repository.git"),
          "HTTPS port parsing must consume every character");
    check(!repository::testing::https_endpoint_is_valid(
              "https://example.com:0/repository.git"),
          "zero HTTPS port must be rejected");
    check(!repository::testing::https_endpoint_is_valid(
              "https://example.com:65536/repository.git"),
          "out-of-range HTTPS port must be rejected");
    check(!repository::testing::https_endpoint_is_valid("https://::1/repository.git"),
          "unbracketed IPv6 authority must be rejected");
}

void test_upload_pack_negotiation_and_operation_budgets() {
    std::vector<char> negotiation;
    append_pkt_length(negotiation, 8);
    negotiation.insert(negotiation.end(), {'N', 'A', 'K', '\n'});
    check(!repository::testing::negotiation_response_is_safe(negotiation),
          "negotiation-only response must not end at arbitrary EOF");
    negotiation.insert(negotiation.end(), {'0', '0', '0', '0'});
    check(repository::testing::negotiation_response_is_safe(negotiation),
          "canonical NAK plus flush negotiation response must be accepted");

    const std::string shallow =
        "shallow 338e6fb681369ff0537719095e22ce9dc602dbf0";
    std::vector<char> shallow_negotiation;
    append_pkt_length(shallow_negotiation, shallow.size() + 4);
    shallow_negotiation.insert(shallow_negotiation.end(), shallow.begin(), shallow.end());
    shallow_negotiation.insert(shallow_negotiation.end(), {'0', '0', '0', '0'});
    check(repository::testing::negotiation_response_is_safe(shallow_negotiation),
          "a terminated shallow-info-only response must be accepted for another request round");
    append_pkt_length(shallow_negotiation, 8);
    shallow_negotiation.insert(shallow_negotiation.end(), {'N', 'A', 'K', '\n'});
    shallow_negotiation.insert(shallow_negotiation.end(), {'0', '0', '0', '0'});
    check(repository::testing::negotiation_response_is_safe(shallow_negotiation),
          "shallow-info flush must not terminate the following ACK/NAK negotiation section");

    auto record_after_flush = negotiation;
    const std::string ack =
        "ACK 338e6fb681369ff0537719095e22ce9dc602dbf0 ready\n";
    append_pkt_length(record_after_flush, ack.size() + 4);
    record_after_flush.insert(record_after_flush.end(), ack.begin(), ack.end());
    check(!repository::testing::negotiation_response_is_safe(record_after_flush),
          "negotiation records after response termination must be rejected");

    check(repository::testing::upload_pack_budget_accepts({8, 12, 80}, 100, 3),
          "all upload-pack rounds must share one exact byte budget");
    check(!repository::testing::upload_pack_budget_accepts({8, 12, 81}, 100, 3),
          "cumulative upload-pack responses beyond the byte budget must fail closed");
    check(!repository::testing::upload_pack_budget_accepts({1, 1, 1, 1}, 100, 3),
          "upload-pack negotiations beyond the round limit must fail closed");
}

void test_delta_dependency_depth_and_base_validation() {
    auto limits = repository::Libgit2RuntimeRepositoryFetchLimits{};
    limits.max_delta_depth = 2;
    check(repository::testing::pack_is_safe(ofs_delta_chain_pack(2), limits),
          "delta chain exactly at the configured depth must be accepted");
    check(!repository::testing::pack_is_safe(ofs_delta_chain_pack(3), limits),
          "delta chain beyond the configured depth must be rejected before libgit2");
    check(!repository::testing::pack_is_safe(ofs_delta_chain_pack(1, 1), limits),
          "OFS_DELTA base must identify an earlier object start");
    check(!repository::testing::pack_is_safe(ofs_delta_chain_pack(1, 0), limits),
          "OFS_DELTA must not form a self-cycle");
    check(!repository::testing::pack_is_safe(ref_delta_pack({1, 1, 0x90U, 1}), limits),
          "unprovable REF_DELTA dependencies must be rejected before libgit2");

    limits.max_delta_depth = 1'024;
    limits.max_received_objects = 2'000;
    check(repository::testing::pack_is_safe(ofs_delta_chain_pack(1'000), limits),
          "large delta dependency graphs must remain bounded and accepted");
}

void test_trusted_https_when_requested() {
    std::string url;
#ifdef _WIN32
    char* raw_url{};
    std::size_t raw_url_size{};
    if (_dupenv_s(&raw_url, &raw_url_size, "BAAS_TEST_TRUSTED_HTTPS_URL") == 0 &&
        raw_url != nullptr) {
        url.assign(raw_url);
        std::free(raw_url);
    }
#else
    if (const auto* raw_url = std::getenv("BAAS_TEST_TRUSTED_HTTPS_URL"); raw_url != nullptr)
        url.assign(raw_url);
#endif
    if (url.empty())
        return;
    auto limits = repository::Libgit2RuntimeRepositoryFetchLimits{};
    limits.connect_timeout = std::chrono::seconds(15);
    limits.stall_timeout = std::chrono::seconds(15);
    limits.absolute_timeout = std::chrono::seconds(60);
    limits.max_advertisement_bytes = 8U * 1024U * 1024U;
    limits.max_advertised_refs = 100'000;
    check(repository::testing::trusted_https_advertisement_is_reachable(url, limits),
          "trusted HTTPS Git advertisement must validate the platform certificate chain");

    TempDirectory staging;
    check(repository::testing::trusted_https_roundtrip_resolves(
              url, "refs/tags/v1.9.0", "338e6fb681369ff0537719095e22ce9dc602dbf0",
              staging.path(), limits),
          "trusted HTTPS roundtrip must GET, POST, preflight, replay, and resolve the exact tag");
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
    const auto fixture_path_utf8 = repository::testing::git_path_bytes(fixture.path());
    require_git(git_repository_open_bare(&raw_repository, fixture_path_utf8.c_str()),
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
        test_non_ascii_staging_path_uses_utf8_for_libgit2();
        test_real_local_fetch_and_tag_peel();
        test_fail_closed_limits_modes_and_cleanup();
        test_typed_cancellation();
        test_delta_result_bomb_is_rejected_before_libgit2();
        test_advertisement_protocol_state_is_strict();
        test_https_endpoint_parsing_is_exact();
        test_upload_pack_negotiation_and_operation_budgets();
        test_delta_dependency_depth_and_base_validation();
        test_trusted_https_when_requested();
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
