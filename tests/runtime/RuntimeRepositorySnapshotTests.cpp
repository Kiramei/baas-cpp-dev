#include "runtime/repository/RuntimeRepositorySnapshot.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#else
#include <unistd.h>
#endif

namespace repository = baas::runtime::repository;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

class TempDirectory final {
public:
    TempDirectory()
    {
        static std::atomic<unsigned long long> next{};
        const auto process =
#ifdef _WIN32
            static_cast<unsigned long long>(GetCurrentProcessId());
#else
            static_cast<unsigned long long>(getpid());
#endif
        for (unsigned int attempt = 0; attempt != 128; ++attempt) {
            const auto tick = static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            path_ = std::filesystem::temp_directory_path() /
                ("baas-runtime-repository-" + std::to_string(process) + "-" +
                 std::to_string(tick) + "-" + std::to_string(++next));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error(
                    "test temporary directory creation failed", path_, error);
        }
        throw std::runtime_error("test temporary directory name retries exhausted");
    }
    ~TempDirectory() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void write(const std::filesystem::path& path, const std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("test file write failed");
}

std::filesystem::path swap_target;
std::atomic<bool> swap_performed{};
std::filesystem::path counted_target;
std::atomic<unsigned int> counted_reads{};
std::filesystem::path view_swap_target;
std::filesystem::path view_swap_archive;
std::atomic<bool> view_swap_attempted{};
std::stop_source* view_stop_source{};

void count_validated_reads(const std::filesystem::path& path)
{
    if (path == counted_target) ++counted_reads;
}

void swap_after_handle_validation(const std::filesystem::path& path)
{
    if (path != swap_target || swap_performed.exchange(true)) return;
    auto pinned = path;
    pinned += ".pinned";
    std::filesystem::rename(path, pinned);
    write(path, "{}");
}

void swap_after_view_handle_open(
    const repository::RuntimeRepositoryReadHookPoint point,
    const std::string_view,
    const std::string_view logical_path)
{
    if ((point != repository::RuntimeRepositoryReadHookPoint::manifest_handle_opened &&
         point != repository::RuntimeRepositoryReadHookPoint::payload_handle_opened) ||
        !view_swap_target.generic_string().ends_with(std::string(logical_path)) ||
        view_swap_attempted.exchange(true)) return;
    std::error_code error;
    std::filesystem::rename(view_swap_target, view_swap_archive, error);
    if (!error) write(view_swap_target, "tampered");
}

void cancel_after_payload_handle_open(
    const repository::RuntimeRepositoryReadHookPoint point,
    const std::string_view,
    const std::string_view)
{
    if (point == repository::RuntimeRepositoryReadHookPoint::payload_handle_opened &&
        view_stop_source) view_stop_source->request_stop();
}

std::filesystem::path blocking_target;
std::mutex blocking_mutex;
std::condition_variable blocking_condition;
bool blocking_entered{};
bool blocking_release{};

void block_after_current_handle_validation(const std::filesystem::path& path)
{
    if (path != blocking_target) return;
    std::unique_lock lock(blocking_mutex);
    if (blocking_entered) return;
    blocking_entered = true;
    blocking_condition.notify_all();
    blocking_condition.wait(lock, [] { return blocking_release; });
}

#ifdef _WIN32
[[nodiscard]] bool create_junction(
    const std::filesystem::path& target, const std::filesystem::path& junction)
{
    std::error_code error;
    std::filesystem::create_directory(junction, error);
    if (error) return false;

    const auto absolute_target = std::filesystem::absolute(target).wstring();
    const auto substitute = L"\\??\\" + absolute_target;
    const auto substitute_bytes = static_cast<unsigned short>(
        substitute.size() * sizeof(wchar_t));
    const auto print_bytes = static_cast<unsigned short>(
        absolute_target.size() * sizeof(wchar_t));
    const auto path_bytes = static_cast<std::size_t>(substitute_bytes) + sizeof(wchar_t) +
        static_cast<std::size_t>(print_bytes) + sizeof(wchar_t);

    struct MountPointBuffer final {
        unsigned long tag;
        unsigned short data_length;
        unsigned short reserved;
        unsigned short substitute_offset;
        unsigned short substitute_length;
        unsigned short print_offset;
        unsigned short print_length;
        wchar_t paths[1];
    };
    const auto total = offsetof(MountPointBuffer, paths) + path_bytes;
    std::vector<std::byte> storage(total);
    auto* buffer = reinterpret_cast<MountPointBuffer*>(storage.data());
    buffer->tag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer->data_length = static_cast<unsigned short>(total - 8U);
    buffer->reserved = 0;
    buffer->substitute_offset = 0;
    buffer->substitute_length = substitute_bytes;
    buffer->print_offset = static_cast<unsigned short>(substitute_bytes + sizeof(wchar_t));
    buffer->print_length = print_bytes;
    std::memcpy(buffer->paths, substitute.c_str(), substitute_bytes);
    buffer->paths[substitute.size()] = L'\0';
    std::memcpy(
        reinterpret_cast<std::byte*>(buffer->paths) + buffer->print_offset,
        absolute_target.c_str(), print_bytes);
    *reinterpret_cast<wchar_t*>(
        reinterpret_cast<std::byte*>(buffer->paths) + buffer->print_offset + print_bytes) = L'\0';

    const auto handle = CreateFileW(
        junction.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD returned{};
    const auto success = DeviceIoControl(
        handle, FSCTL_SET_REPARSE_POINT, buffer,
        static_cast<DWORD>(total), nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return success;
}
#endif

[[nodiscard]] bool create_directory_reparse(
    const std::filesystem::path& target, const std::filesystem::path& link)
{
#ifdef _WIN32
    return create_junction(target, link);
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(target, link, error);
    return !error;
#endif
}

[[nodiscard]] std::array<repository::RuntimeRepository, 2> records(const char seed = '1')
{
    const std::string resource_commit(40, seed);
    const std::string script_commit(64, static_cast<char>(seed + 2));
    return {{
        {"resources", resource_commit, "objects/resources/" + resource_commit,
         "manifest.json", std::string(64, static_cast<char>(seed + 1))},
        {"scripts", script_commit, "objects/scripts/" + script_commit,
         "scripts.json", std::string(64, static_cast<char>(seed + 3))},
    }};
}

[[nodiscard]] std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& values,
    const std::string_view generation)
{
    std::string result = "{\"schema\":\"baas.runtime-repositories.snapshot/v1\",\"generation\":\"";
    result += generation;
    result += "\",\"repositories\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) result += ',';
        const auto& item = values[index];
        result += "{\"id\":\"" + item.id + "\",\"commit\":\"" + item.commit +
            "\",\"root\":\"" + item.root + "\",\"manifest\":\"" + item.manifest +
            "\",\"manifest_sha256\":\"" + item.manifest_sha256 + "\"}";
    }
    result += "]}";
    return result;
}

[[nodiscard]] std::string current_json(const std::string_view generation)
{
    return "{\"schema\":\"baas.runtime-repositories.current/v1\",\"generation\":\"" +
        std::string(generation) + "\",\"snapshot\":\"snapshots/" +
        std::string(generation) + ".json\"}";
}

[[nodiscard]] std::string install(
    const std::filesystem::path& root,
    const std::array<repository::RuntimeRepository, 2>& values,
    const bool publish = true)
{
    const auto generation = repository::runtime_repository_generation(values);
    write(root / "snapshots" / (generation + ".json"), snapshot_json(values, generation));
    if (publish) write(root / "current.json", current_json(generation));
    return generation;
}

[[nodiscard]] std::array<repository::RuntimeRepository, 2> readable_records(
    const char seed = '1')
{
    constexpr std::string_view manifest_hash =
        "737a1b1bcb0b033b9d8a969ac87271ee6dc4ad2f1659a49488413b5a2986b270";
    const std::string resource_commit(40, seed);
    const std::string script_commit(64, static_cast<char>(seed + 2));
    return {{
        {"resources", resource_commit, "objects/resources/" + resource_commit,
         "manifest.json", std::string(manifest_hash)},
        {"scripts", script_commit, "objects/scripts/" + script_commit,
         "scripts.json", std::string(manifest_hash)},
    }};
}

constexpr std::string_view readable_manifest =
    "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[{\"path\":\"nested/payload.bin\",\"size\":\"7\",\"sha256\":\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\",\"mode\":\"file\"}]}";

[[nodiscard]] std::string install_readable(
    const std::filesystem::path& root,
    const std::array<repository::RuntimeRepository, 2>& values,
    const bool publish = true)
{
    for (const auto& item : values) {
        const auto object = root / item.root;
        write(object / item.manifest, readable_manifest);
        write(object / "nested" / "payload.bin", "payload");
    }
    return install(root, values, publish);
}

template <typename Callback>
void expect_read(const repository::RuntimeRepositoryReadErrorCode code,
                 Callback&& callback, const std::string_view message)
{
    try {
        callback();
        check(false, message);
    } catch (const repository::RuntimeRepositoryReadError& error) {
        check(error.code() == code, message);
    }
}

[[nodiscard]] std::string bytes_as_string(const std::vector<std::byte>& bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

template <typename Callback>
void expect(const repository::RuntimeRepositoryErrorCode code, Callback&& callback,
            const std::string_view message)
{
    try {
        callback();
        check(false, message);
    } catch (const repository::RuntimeRepositoryError& error) {
        check(error.code() == code, message);
    }
}

void replace_current(
    const std::filesystem::path& root, const std::string_view contents,
    const unsigned long long sequence, std::atomic<bool>* contention = nullptr,
    std::atomic<bool>* attempting = nullptr)
{
    const auto temporary = root / ("current." + std::to_string(sequence) + ".tmp");
    write(temporary, contents);
    if (attempting) attempting->store(true, std::memory_order_release);
#ifdef _WIN32
    const auto target = root / "current.json";
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (MoveFileExW(temporary.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        const auto error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED)
            throw std::runtime_error("atomic current replacement failed");
        if (contention) contention->store(true, std::memory_order_release);
        Sleep(1);
    }
    throw std::runtime_error("atomic current replacement remained busy");
#else
    static_cast<void>(contention);
    std::filesystem::rename(temporary, root / "current.json");
#endif
}

void test_contract_vector_and_no_object_reads()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    const auto values = records();
    const auto generation = install(root, values);
    check(generation == "e9893efbc754f1e0da2df1e398d0f4e75575d4dc6d8e9471aa4b1d449c6430c3",
          "generation must use the specified domain and big-endian length prefixes");
    const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
    check(snapshot->generation() == generation &&
              snapshot->resources().commit == values[0].commit &&
              snapshot->scripts().manifest == "scripts.json",
          "activation must publish the exact canonical repository descriptors");
    check(!std::filesystem::exists(root / "objects"),
          "activation must not require or read repository object data");

    const std::string resource_commit(40, '1');
    const std::string script_commit(40, '2');
    const std::array<repository::RuntimeRepository, 2> tauri_fixture{{
        {"resources", resource_commit, "objects/resources/" + resource_commit,
         "resources.json", std::string(64, 'a')},
        {"scripts", script_commit, "objects/scripts/" + script_commit,
         "scripts.json", std::string(64, 'b')},
    }};
    check(repository::runtime_repository_generation(tauri_fixture) ==
              "24f74e7ebac7bb8920e775edb8fa377384c6bf2317afa4dd9a85b42b7695e1ae",
          "C++ generation must match the Tauri canonical fixture");

    const auto normalized = repository::RuntimeRepositorySnapshot::activate(root / ".");
    check(normalized->generation() == generation,
          "activation must normalize the supplied state root before containment checks");
#ifdef _WIN32
    auto differently_cased = root.wstring();
    const auto marker = differently_cased.find(L"baas-runtime-repository-");
    if (marker != std::wstring::npos) differently_cased[marker] = L'B';
    const auto case_alias = repository::RuntimeRepositorySnapshot::activate(differently_cased);
    check(case_alias->generation() == generation,
          "Windows containment must compare canonical roots case-insensitively");
#endif
}

void test_malformed_strict_fields_and_bounds()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    const auto values = records();
    const auto generation = install(root, values);

    write(root / "current.json", "{\"schema\":\"baas.runtime-repositories.current/v1\","
        "\"schema\":\"baas.runtime-repositories.current/v1\",\"generation\":\"" +
        generation + "\",\"snapshot\":\"snapshots/" + generation + ".json\"}");
    counted_target = root / "current.json";
    counted_reads.store(0);
    repository::set_runtime_repository_read_hook(count_validated_reads);
    expect(repository::RuntimeRepositoryErrorCode::InvalidJson,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "duplicate JSON fields must be rejected");
    repository::set_runtime_repository_read_hook(nullptr);
    check(counted_reads.load() == 1,
          "current policy failures must not enter the Windows I/O retry loop");

    write(root / "current.json", current_json(generation));
    auto unknown = snapshot_json(values, generation);
    unknown.insert(unknown.size() - 1, ",\"extra\":\"x\"");
    write(root / "snapshots" / (generation + ".json"), unknown);
    expect(repository::RuntimeRepositoryErrorCode::InvalidFieldSet,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "unknown snapshot fields must be rejected");

    write(root / "current.json", std::string(5000, 'x'));
    expect(repository::RuntimeRepositoryErrorCode::FileLimitExceeded,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "current JSON input must be bounded before parsing");
}

void test_traversal_identity_and_descriptor_rejection()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    auto values = records();
    const auto generation = install(root, values);
    write(root / "current.json",
          "{\"schema\":\"baas.runtime-repositories.current/v1\",\"generation\":\"" +
          generation + "\",\"snapshot\":\"../" + generation + ".json\"}");
    expect(repository::RuntimeRepositoryErrorCode::PathViolation,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "current snapshot traversal must be rejected");

    write(root / "current.json", current_json(generation));
    values[0].manifest = "../manifest.json";
    write(root / "snapshots" / (generation + ".json"), snapshot_json(values, generation));
    expect(repository::RuntimeRepositoryErrorCode::InvalidRepository,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "manifest names must remain one canonical path segment");

    values = records();
    values[0].manifest_sha256 = std::string(64, 'a');
    write(root / "snapshots" / (generation + ".json"), snapshot_json(values, generation));
    expect(repository::RuntimeRepositoryErrorCode::IdentityMismatch,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "descriptor changes must invalidate the snapshot generation");
}

void test_reparse_component_rejection()
{
    const auto values = records();
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        const auto generation = repository::runtime_repository_generation(values);
        write(root / "current.json", current_json(generation));
        const auto outside = temporary.path() / "outside-snapshots";
        write(outside / (generation + ".json"), snapshot_json(values, generation));
        const auto linked = create_directory_reparse(outside, root / "snapshots");
        check(linked, "the platform test must create an intermediate reparse component");
        if (linked)
            expect(repository::RuntimeRepositoryErrorCode::PathViolation,
                   [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
                   "reparse snapshot directories must not cross the activation root");
    }
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        const auto generation = repository::runtime_repository_generation(values);
        write(root / "current.json", current_json(generation));
        const auto outside = temporary.path() / "outside-snapshot";
        std::filesystem::create_directories(outside);
        std::filesystem::create_directories(root / "snapshots");
        const auto linked = create_directory_reparse(
            outside, root / "snapshots" / (generation + ".json"));
        check(linked, "the platform test must create a final reparse component");
        if (linked)
            expect(repository::RuntimeRepositoryErrorCode::PathViolation,
                   [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
                   "final reparse snapshot files must be rejected");
    }
}

void test_validated_handle_is_the_read_object()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    const auto values = records();
    const auto generation = install(root, values);
    swap_target = root / "snapshots" / (generation + ".json");
    swap_performed.store(false);
    repository::set_runtime_repository_read_hook(swap_after_handle_validation);
    const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
    repository::set_runtime_repository_read_hook(nullptr);
    check(swap_performed.load() && snapshot->generation() == generation &&
              snapshot->resources().commit == values[0].commit,
          "activation must read the object held by the validated handle after a path swap");
}

void test_old_snapshot_pin_and_concurrent_current()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    const auto old_records = records('1');
    const auto new_records = records('5');
    const auto old_generation = install(root, old_records);
    const auto pinned = repository::RuntimeRepositorySnapshot::activate(root);
    const auto new_generation = install(root, new_records, false);
    blocking_target = root / "current.json";
    {
        std::lock_guard lock(blocking_mutex);
        blocking_entered = false;
        blocking_release = false;
    }
    repository::set_runtime_repository_read_hook(block_after_current_handle_validation);
    std::atomic<bool> stop{};
    std::atomic<unsigned int> old_observed{};
    std::atomic<unsigned int> new_observed{};
    std::atomic<unsigned int> unexpected{};
    auto reader = std::async(std::launch::async, [&] {
        while (!stop.load(std::memory_order_acquire)) {
            try {
                const auto loaded = repository::RuntimeRepositorySnapshot::activate(root);
                if (loaded->generation() == old_generation) ++old_observed;
                else if (loaded->generation() == new_generation) ++new_observed;
                else ++unexpected;
            } catch (...) {
                ++unexpected;
            }
        }
    });

    {
        std::unique_lock lock(blocking_mutex);
        const auto entered = blocking_condition.wait_for(
            lock, std::chrono::seconds(5), [] { return blocking_entered; });
        check(entered, "reader must reach the validated-current barrier");
    }
    std::atomic<bool> writer_attempting{};
    std::atomic<bool> writer_contended{};
    auto first_writer = std::async(std::launch::async, [&] {
        replace_current(
            root, current_json(new_generation), 0,
            &writer_contended, &writer_attempting);
    });
    const auto contention_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
#ifdef _WIN32
    while (!writer_contended.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < contention_deadline)
        std::this_thread::yield();
    check(writer_contended.load(std::memory_order_acquire),
          "writer must overlap the reader's validated current handle");
#else
    while (!writer_attempting.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < contention_deadline)
        std::this_thread::yield();
    check(writer_attempting.load(std::memory_order_acquire) &&
              first_writer.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
          "Unix replacement must finish while the reader pins the old current descriptor");
#endif
    {
        std::lock_guard lock(blocking_mutex);
        blocking_release = true;
    }
    blocking_condition.notify_all();
    first_writer.get();

    const auto observation_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((old_observed.load() == 0 || new_observed.load() == 0) &&
           std::chrono::steady_clock::now() < observation_deadline)
        std::this_thread::yield();
    for (unsigned long long index = 1; index <= 80; ++index) {
        const auto selected = index % 2 == 0 ? new_generation : old_generation;
        replace_current(root, current_json(selected), index);
    }
    replace_current(root, current_json(new_generation), 81);
    stop.store(true, std::memory_order_release);
    reader.get();
    repository::set_runtime_repository_read_hook(nullptr);

    const auto latest = repository::RuntimeRepositorySnapshot::activate(root);
    check(pinned->generation() == old_generation && latest->generation() == new_generation &&
              pinned->resources().commit == old_records[0].commit,
          "published snapshots must remain pinned across current changes");
    check(old_observed.load() != 0 && new_observed.load() != 0 && unexpected.load() == 0,
          "barrier-overlapped current replacement must expose only complete old/new generations");
}

void test_read_bundle_contract_and_sealing()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    const auto values = readable_records();
    const auto generation = install_readable(root, values);
    const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
    const auto bundle = snapshot->open_read_bundle();
    check(bundle->generation() == generation &&
              bundle->resources().generation() == generation &&
              bundle->scripts().generation() == generation &&
              bundle->resources().repository_id() == "resources" &&
              bundle->resources().entries().size() == 1,
          "a bundle must expose two views from one selected generation");
    check(bytes_as_string(bundle->resources().read("nested/payload.bin", 7)) == "payload",
          "manifested payload reads must return owned verified bytes");
    expect_read(repository::RuntimeRepositoryReadErrorCode::entry_not_found,
                [&] { (void)bundle->resources().read("manifest.json", 1024); },
                "the manifest itself must not be exposed as a payload");
    expect_read(repository::RuntimeRepositoryReadErrorCode::file_limit_exceeded,
                [&] { (void)bundle->resources().read("nested/payload.bin", 6); },
                "each payload read must enforce its caller byte budget");
    std::stop_source stopped;
    stopped.request_stop();
    expect_read(repository::RuntimeRepositoryReadErrorCode::cancelled,
                [&] { (void)bundle->resources().read(
                    "nested/payload.bin", 7, stopped.get_token()); },
                "payload reads must observe pre-requested cancellation");
    expect_read(repository::RuntimeRepositoryReadErrorCode::cancelled,
                [&] { (void)snapshot->open_read_bundle({}, stopped.get_token()); },
                "full-tree sealing must observe pre-requested cancellation");
    repository::RuntimeRepositoryReadLimits small_file_limit;
    small_file_limit.max_file_bytes = 6;
    expect_read(repository::RuntimeRepositoryReadErrorCode::invalid_manifest,
                [&] { (void)snapshot->open_read_bundle(small_file_limit); },
                "manifested files must remain within the configured per-file budget");
    repository::RuntimeRepositoryReadLimits small_manifest_limit;
    small_manifest_limit.max_manifest_bytes = 32;
    expect_read(repository::RuntimeRepositoryReadErrorCode::file_limit_exceeded,
                [&] { (void)snapshot->open_read_bundle(small_manifest_limit); },
                "manifest bytes must be bounded before JSON parsing");
    repository::RuntimeRepositoryReadLimits inconsistent_limits;
    inconsistent_limits.max_files = 2;
    inconsistent_limits.max_entries = 1;
    expect_read(repository::RuntimeRepositoryReadErrorCode::file_limit_exceeded,
                [&] { (void)snapshot->open_read_bundle(inconsistent_limits); },
                "tree entry limits must be large enough to account for every file");
    std::stop_source during_hash;
    view_stop_source = &during_hash;
    repository::set_runtime_repository_read_view_hook(cancel_after_payload_handle_open);
    expect_read(repository::RuntimeRepositoryReadErrorCode::cancelled,
                [&] { (void)snapshot->open_read_bundle({}, during_hash.get_token()); },
                "streaming payload sealing must observe cancellation after opening a file");
    repository::set_runtime_repository_read_view_hook(nullptr);
    view_stop_source = nullptr;

    write(root / values[0].root / "nested" / "payload.bin", "tamper!");
    expect_read(repository::RuntimeRepositoryReadErrorCode::payload_mismatch,
                [&] { (void)snapshot->open_read_bundle(); },
                "open must fail closed when any payload digest is damaged");
    expect_read(repository::RuntimeRepositoryReadErrorCode::payload_mismatch,
                [&] { (void)bundle->resources().read("nested/payload.bin", 7); },
                "an existing view must revalidate payload bytes on every read");
}

void test_exact_tree_and_link_rejection()
{
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        const auto values = readable_records();
        (void)install_readable(root, values);
        write(root / values[0].root / "not-manifested.bin", "x");
        const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
        expect_read(repository::RuntimeRepositoryReadErrorCode::invalid_manifest,
                    [&] { (void)snapshot->open_read_bundle(); },
                    "unmanifested payloads must reject the complete bundle");
    }
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        const auto values = readable_records();
        (void)install_readable(root, values);
        std::filesystem::create_directories(root / values[0].root / "empty");
        const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
        expect_read(repository::RuntimeRepositoryReadErrorCode::invalid_manifest,
                    [&] { (void)snapshot->open_read_bundle(); },
                    "empty directories must reject the exact immutable tree");
    }
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        const auto values = readable_records();
        (void)install_readable(root, values);
        std::filesystem::create_hard_link(
            root / values[0].root / "nested" / "payload.bin",
            root / values[0].root / "nested" / "payload-hardlink.bin");
        const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
        expect_read(repository::RuntimeRepositoryReadErrorCode::path_violation,
                    [&] { (void)snapshot->open_read_bundle(); },
                    "multi-link payload identities must be rejected");
    }
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        const auto values = readable_records();
        (void)install_readable(root, values);
        const auto repository_root = root / values[0].root;
        const auto outside = temporary.path() / "outside-repository";
        std::filesystem::rename(repository_root, outside);
        const auto linked = create_directory_reparse(outside, repository_root);
        check(linked, "the platform test must link a repository object root");
        if (linked) {
            const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
            expect_read(repository::RuntimeRepositoryReadErrorCode::path_violation,
                        [&] { (void)snapshot->open_read_bundle(); },
                        "repository object roots must be opened without following links");
        }
    }
}

void test_read_bundle_retains_generation_and_open_handles()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    const auto old_values = readable_records('1');
    const auto old_generation = install_readable(root, old_values);
    const auto pinned = repository::RuntimeRepositorySnapshot::activate(root);
    const auto new_values = readable_records('5');
    const auto new_generation = install_readable(root, new_values);
    const auto old_bundle = pinned->open_read_bundle();
    const auto latest = repository::RuntimeRepositorySnapshot::activate(root)->open_read_bundle();
    check(old_bundle->generation() == old_generation &&
              latest->generation() == new_generation &&
              bytes_as_string(old_bundle->scripts().read("nested/payload.bin", 7)) == "payload",
          "a pinned snapshot must open only its retained old generation after current advances");

    view_swap_target = root / old_values[0].root / "nested" / "payload.bin";
    view_swap_archive = temporary.path() / "payload.pinned";
    view_swap_attempted.store(false);
    repository::set_runtime_repository_read_view_hook(swap_after_view_handle_open);
    const auto swapped_read = old_bundle->resources().read("nested/payload.bin", 7);
    repository::set_runtime_repository_read_view_hook(nullptr);
    check(view_swap_attempted.load() && bytes_as_string(swapped_read) == "payload",
          "payload reads must hash and return the already validated native file handle");
    expect_read(repository::RuntimeRepositoryReadErrorCode::payload_mismatch,
                [&] { (void)old_bundle->resources().read("nested/payload.bin", 32); },
                "later reads must reject a payload path replaced after view sealing");
    write(view_swap_target, "payload");

    view_swap_target = root / old_values[0].root / old_values[0].manifest;
    view_swap_archive = temporary.path() / "manifest.pinned";
    view_swap_attempted.store(false);
    repository::set_runtime_repository_read_view_hook(swap_after_view_handle_open);
    const auto replaced_manifest_bundle = pinned->open_read_bundle();
    repository::set_runtime_repository_read_view_hook(nullptr);
    check(view_swap_attempted.load() &&
              replaced_manifest_bundle->resources().entries().size() == 1,
          "manifest verification must consume the already validated native handle");
}

void test_utf8_paths_and_portable_aliases()
{
    constexpr std::string_view cjk_manifest =
        "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[{\"path\":\"nested/日本.bin\",\"size\":\"7\",\"sha256\":\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\",\"mode\":\"file\"}]}";
    constexpr std::string_view alias_manifest =
        "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[{\"path\":\"A.bin\",\"size\":\"7\",\"sha256\":\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\",\"mode\":\"file\"},{\"path\":\"a.bin\",\"size\":\"7\",\"sha256\":\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\",\"mode\":\"file\"}]}";
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        auto values = readable_records();
        for (auto& item : values)
            item.manifest_sha256 =
                "6215916b50d80ec23d7a2e149a8df0875afb039de48dca940a5d78005f2e2e80";
        for (const auto& item : values) {
            write(root / item.root / item.manifest, cjk_manifest);
            write(root / item.root / "nested" / std::filesystem::u8path(u8"日本.bin"),
                  "payload");
        }
        (void)install(root, values);
        const auto bundle = repository::RuntimeRepositorySnapshot::activate(root)->open_read_bundle();
        check(bytes_as_string(bundle->resources().read("nested/日本.bin", 7)) == "payload",
              "canonical UTF-8 logical paths must round-trip through the read capability");
    }
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        auto values = readable_records();
        for (auto& item : values)
            item.manifest_sha256 =
                "a404ffc94f3e51e2d2b4309fe48ca24a6f4a5c920095548716e01b1fd37db279";
        for (const auto& item : values)
            write(root / item.root / item.manifest, alias_manifest);
        (void)install(root, values);
        const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
        expect_read(repository::RuntimeRepositoryReadErrorCode::invalid_manifest,
                    [&] { (void)snapshot->open_read_bundle(); },
                    "case-folded UTF-8 portable aliases must be rejected before publication");
    }
}

void test_tree_manifest_parser_budgets()
{
    const auto rejected = [](const std::string_view manifest,
                             const std::string_view manifest_hash,
                             repository::RuntimeRepositoryReadLimits limits,
                             const std::string_view message) {
        TempDirectory temporary;
        const auto root = temporary.path() / "runtime-repositories";
        auto values = readable_records();
        for (auto& item : values) item.manifest_sha256 = manifest_hash;
        for (const auto& item : values) write(root / item.root / item.manifest, manifest);
        (void)install(root, values);
        const auto snapshot = repository::RuntimeRepositorySnapshot::activate(root);
        expect_read(repository::RuntimeRepositoryReadErrorCode::invalid_manifest,
                    [&] { (void)snapshot->open_read_bundle(limits); }, message);
    };
    repository::RuntimeRepositoryReadLimits node_limits;
    node_limits.max_files = 1;
    node_limits.max_entries = 1;
    rejected(
        "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"\"]}",
        "3b42163713c0ec50e1c6336d085643c135360ee61a5b8b2b7cb67e468d08ea70",
        node_limits, "manifest JSON nodes must be bounded during parsing");
    repository::RuntimeRepositoryReadLimits string_limits;
    string_limits.max_relative_path_bytes = 16;
    rejected(
        "{\"schema\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"entries\":[]}",
        "1e04af9d771af0c8b4a79875e214e668d0b955b511cd98bc0e60587970b9fb2c",
        string_limits, "decoded manifest JSON strings must be bounded during parsing");
    rejected(
        "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[[[[[[[[]]]]]]]]}",
        "8dd01ed15a6beb24bd437fdb46c888fce488cfb277cbe931de5db5ef0075eea5",
        {}, "manifest JSON nesting must be bounded during parsing");
}

}  // namespace

int main()
{
    try {
        test_contract_vector_and_no_object_reads();
        test_malformed_strict_fields_and_bounds();
        test_traversal_identity_and_descriptor_rejection();
        test_reparse_component_rejection();
        test_validated_handle_is_the_read_object();
        test_old_snapshot_pin_and_concurrent_current();
        test_read_bundle_contract_and_sealing();
        test_exact_tree_and_link_rejection();
        test_read_bundle_retains_generation_and_open_handles();
        test_utf8_paths_and_portable_aliases();
        test_tree_manifest_parser_budgets();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "runtime repository snapshot tests passed\n";
    return EXIT_SUCCESS;
}
