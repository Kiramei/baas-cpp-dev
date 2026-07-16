#include "runtime/repository/RuntimeRepositorySnapshot.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
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
        path_ = std::filesystem::temp_directory_path() /
            ("baas-runtime-repository-" + std::to_string(++next));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
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
    const unsigned long long sequence)
{
    const auto temporary = root / ("current." + std::to_string(sequence) + ".tmp");
    write(temporary, contents);
#ifdef _WIN32
    const auto target = root / "current.json";
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (MoveFileExW(temporary.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return;
        const auto error = GetLastError();
        if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED)
            throw std::runtime_error("atomic current replacement failed");
        Sleep(1);
    }
    throw std::runtime_error("atomic current replacement remained busy");
#else
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
    expect(repository::RuntimeRepositoryErrorCode::InvalidJson,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "duplicate JSON fields must be rejected");

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

void test_symlink_rejection_when_supported()
{
    TempDirectory temporary;
    const auto root = temporary.path() / "runtime-repositories";
    const auto values = records();
    const auto generation = repository::runtime_repository_generation(values);
    write(root / "current.json", current_json(generation));
    const auto outside = temporary.path() / "outside.json";
    write(outside, snapshot_json(values, generation));
    std::filesystem::create_directories(root / "snapshots");
    std::error_code error;
    std::filesystem::create_symlink(
        outside, root / "snapshots" / (generation + ".json"), error);
    if (error) return; // Windows runners may not grant symlink creation privilege.
    expect(repository::RuntimeRepositoryErrorCode::PathViolation,
           [&] { (void)repository::RuntimeRepositorySnapshot::activate(root); },
           "symlinked snapshot files must not cross the activation root");
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
    replace_current(root, current_json(new_generation), 0);
    const auto latest = repository::RuntimeRepositorySnapshot::activate(root);
    check(pinned->generation() == old_generation && latest->generation() == new_generation &&
              pinned->resources().commit == old_records[0].commit,
          "published snapshots must remain pinned across current changes");

    std::atomic<bool> start{};
    auto reader = std::async(std::launch::async, [&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for (int index = 0; index < 80; ++index) {
            const auto loaded = repository::RuntimeRepositorySnapshot::activate(root);
            if (loaded->generation() != old_generation &&
                loaded->generation() != new_generation) return false;
        }
        return true;
    });
    start.store(true, std::memory_order_release);
    for (unsigned long long index = 1; index <= 40; ++index) {
        const auto selected = index % 2 == 0 ? new_generation : old_generation;
        replace_current(root, current_json(selected), index);
    }
    check(reader.get(), "concurrent atomic current replacement must yield an old-or-new snapshot");
}

}  // namespace

int main()
{
    try {
        test_contract_vector_and_no_object_reads();
        test_malformed_strict_fields_and_bounds();
        test_traversal_identity_and_descriptor_rejection();
        test_symlink_rejection_when_supported();
        test_old_snapshot_pin_and_concurrent_current();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "runtime repository snapshot tests passed\n";
    return EXIT_SUCCESS;
}
