#include "service/app/ServiceRuntimeRepositoryOwner.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <process.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace app = baas::service::app;
namespace repository = baas::runtime::repository;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::uint64_t process_id() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(::_getpid());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

class TemporaryRoot final {
public:
    explicit TemporaryRoot(const std::string_view suffix)
    {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path()
            / ("baas-service-runtime-repository-owner-" + std::string{suffix} + "-"
               + std::to_string(process_id()) + "-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count())
               + "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }

    ~TemporaryRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    std::filesystem::path path;
};

class HeldWriterLock final {
public:
    explicit HeldWriterLock(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        handle_ = CreateFileW(
            path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE
            || !LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped_)) {
            throw std::runtime_error("fixture writer lock failed");
        }
#else
        descriptor_ = open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor_ < 0 || flock(descriptor_, LOCK_EX) != 0)
            throw std::runtime_error("fixture writer lock failed");
#endif
    }

    ~HeldWriterLock()
    {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            UnlockFileEx(handle_, 0, 1, 0, &overlapped_);
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0) {
            static_cast<void>(flock(descriptor_, LOCK_UN));
            close(descriptor_);
        }
#endif
    }

    HeldWriterLock(const HeldWriterLock&) = delete;
    HeldWriterLock& operator=(const HeldWriterLock&) = delete;

private:
#if defined(_WIN32)
    HANDLE handle_{INVALID_HANDLE_VALUE};
    OVERLAPPED overlapped_{};
#else
    int descriptor_{-1};
#endif
};

void write_file(const std::filesystem::path& path, const std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) throw std::runtime_error("fixture write failed");
}

[[nodiscard]] std::array<repository::RuntimeRepository, 2> descriptors(
    const char resource_digit, const char script_digit)
{
    const std::string resource_commit(40, resource_digit);
    const std::string script_commit(40, script_digit);
    return {{
        {"resources", resource_commit, "objects/resources/" + resource_commit,
         "manifest.json", std::string(64, 'a')},
        {"scripts", script_commit, "objects/scripts/" + script_commit,
         "manifest.json", std::string(64, 'b')},
    }};
}

[[nodiscard]] std::string snapshot_json(
    const std::array<repository::RuntimeRepository, 2>& values,
    const std::string_view generation)
{
    std::string json =
        "{\"schema\":\"baas.runtime-repositories.snapshot/v1\",\"generation\":\""
        + std::string{generation} + "\",\"repositories\":[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) json += ',';
        const auto& item = values[index];
        json += "{\"id\":\"" + item.id + "\",\"commit\":\"" + item.commit
            + "\",\"root\":\"" + item.root + "\",\"manifest\":\""
            + item.manifest + "\",\"manifest_sha256\":\""
            + item.manifest_sha256 + "\"}";
    }
    return json + "]}";
}

[[nodiscard]] std::string current_json(const std::string_view generation)
{
    return "{\"schema\":\"baas.runtime-repositories.current/v1\",\"generation\":\""
        + std::string{generation} + "\",\"snapshot\":\"snapshots/"
        + std::string{generation} + ".json\"}";
}

void install_trusted_state(
    const std::filesystem::path& project_root,
    const std::string_view generation)
{
    const auto state_root =
        project_root / ".baas-updater" / "runtime-repositories";
    write_file(state_root / ".trusted-plan-writer.lock", "");
    write_file(
        state_root / ".trusted-plan-owner",
        "baas.runtime-repositories.trusted-plan-owner/v1\ninitialized\n");
    write_file(
        state_root / ".trusted-plan-state.json",
        "{\"schema\":\"baas.runtime-repositories.trusted-plan-state/v1\","
        "\"generation\":\"" + std::string{generation}
            + "\",\"sequence\":\"1\",\"payload_sha256\":\""
            + std::string(64, 'c') + "\"}");
}

[[nodiscard]] std::string install_generation(
    const std::filesystem::path& project_root,
    const std::array<repository::RuntimeRepository, 2>& values,
    const bool publish = true)
{
    const auto state_root =
        project_root / ".baas-updater" / "runtime-repositories";
    const auto generation = repository::runtime_repository_generation(values);
    write_file(
        state_root / "snapshots" / (generation + ".json"),
        snapshot_json(values, generation));
    if (publish) {
        write_file(state_root / "current.json", current_json(generation));
        install_trusted_state(project_root, generation);
    }
    return generation;
}

void test_missing_current_fails_expected_generation()
{
    TemporaryRoot root{"missing"};
    std::filesystem::create_directories(
        root.path / ".baas-updater" / "runtime-repositories" / "snapshots");
    const auto opened = app::open_service_runtime_repository_owner(
        root.path, std::string(64, 'a'));
    check(!opened
              && opened.error
                  == app::ServiceRuntimeRepositoryOpenError::generation_mismatch,
          "missing current must fail the expected generation contract");
}

void test_valid_activation_is_pinned_for_owner_lifetime()
{
    TemporaryRoot root{"lifetime"};
    const auto generation = install_generation(root.path, descriptors('1', '2'));
    auto opened = app::open_service_runtime_repository_owner(root.path, generation);
    check(static_cast<bool>(opened), "valid activation must open");
    if (!opened) return;
    auto owner = std::move(opened.owner);
    const auto pin = owner->pin();
    const auto trust = owner->script_trust_evidence();
    check(owner->phase() == app::ServiceRuntimeRepositoryPhase::pinned
              && owner->generation() == generation && pin
              && pin->generation() == generation && trust
              && trust->covers(generation, std::string(40, '2'))
              && !trust->covers(generation, std::string(40, '3'))
              && !trust->covers(std::string(64, 'f'), std::string(40, '2')),
          "valid activation must publish exact immutable native trust evidence");

    app::ServiceRuntimeRepositoryOwner untrusted_embedding{pin};
    check(!untrusted_embedding.script_trust_evidence(),
          "a snapshot-only owner must never manufacture production trust evidence");

    std::error_code ignored;
    std::filesystem::remove_all(
        root.path / ".baas-updater" / "runtime-repositories", ignored);
    check(pin && pin->generation() == generation
              && pin->resources().commit == std::string(40, '1')
              && owner->generation() == generation && trust
              && trust->covers(generation, std::string(40, '2')),
          "owner, retained pins, and trust evidence must outlive mutable activation files");
}

void test_trusted_state_is_required_exact_and_recovery_free()
{
    {
        TemporaryRoot root{"missing-trust"};
        const auto generation = install_generation(root.path, descriptors('1', '2'));
        std::filesystem::remove(
            root.path / ".baas-updater" / "runtime-repositories"
                / ".trusted-plan-state.json");
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, generation);
        check(!opened
                  && opened.error == app::ServiceRuntimeRepositoryOpenError::
                      trusted_state_invalid,
              "a repository pin without durable signed-plan state must fail closed");
    }
    {
        TemporaryRoot root{"mismatched-trust"};
        const auto generation = install_generation(root.path, descriptors('2', '3'));
        install_trusted_state(root.path, std::string(64, 'f'));
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, generation);
        check(!opened
                  && opened.error == app::ServiceRuntimeRepositoryOpenError::
                      trusted_state_generation_mismatch
                  && opened.trusted_state_error ==
                      app::RuntimeRepositoryTrustedPlanStateError::
                          inconsistent_generation,
              "trusted policy state must name the exact immutable generation");
    }
    {
        TemporaryRoot root{"pending-trust"};
        const auto generation = install_generation(root.path, descriptors('3', '4'));
        write_file(
            root.path / ".baas-updater" / "runtime-repositories"
                / ".trusted-plan-journal.json",
            "{}");
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, generation);
        check(!opened
                  && opened.error == app::ServiceRuntimeRepositoryOpenError::
                      trusted_state_pending_recovery
                  && opened.trusted_state_error ==
                      app::RuntimeRepositoryTrustedPlanStateError::pending_recovery,
              "service startup must not recover or complete a pending policy journal");
    }
    {
        TemporaryRoot root{"pending-publication"};
        const auto generation = install_generation(root.path, descriptors('4', '5'));
        write_file(
            root.path / ".baas-updater" / "runtime-repositories"
                / ".publish-journal.json",
            "{}");
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, generation);
        check(!opened
                  && opened.error == app::ServiceRuntimeRepositoryOpenError::
                      trusted_state_pending_recovery,
              "service startup must not recover a pending repository publication");
    }
    {
        TemporaryRoot root{"writer-active"};
        const auto generation = install_generation(root.path, descriptors('5', '6'));
        const auto lock_path =
            root.path / ".baas-updater" / "runtime-repositories"
                / ".trusted-plan-writer.lock";
        HeldWriterLock writer{lock_path};
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, generation);
        check(!opened
                  && opened.error == app::ServiceRuntimeRepositoryOpenError::
                      trusted_state_pending_recovery
                  && opened.trusted_state_error ==
                      app::RuntimeRepositoryTrustedPlanStateError::not_ready,
              "service startup must fail immediately while native publication owns the policy lock");
    }
}

void test_malformed_and_tampered_activation_fail_closed()
{
    {
        TemporaryRoot root{"malformed"};
        write_file(
            root.path / ".baas-updater" / "runtime-repositories" / "current.json",
            "{not-json");
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, std::string(64, 'a'));
        check(!opened
                  && opened.error
                      == app::ServiceRuntimeRepositoryOpenError::invalid_activation,
              "malformed current must fail instead of becoming unavailable");
    }
    {
        TemporaryRoot root{"missing-snapshot"};
        const auto values = descriptors('2', '3');
        const auto generation = repository::runtime_repository_generation(values);
        write_file(
            root.path / ".baas-updater" / "runtime-repositories" / "current.json",
            current_json(generation));
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, generation);
        check(!opened
                  && opened.error
                      == app::ServiceRuntimeRepositoryOpenError::invalid_activation,
              "an existing pointer with a missing snapshot must fail closed");
    }
    {
        TemporaryRoot root{"tampered"};
        const auto generation = install_generation(root.path, descriptors('3', '4'));
        const auto state_root =
            root.path / ".baas-updater" / "runtime-repositories";
        auto tampered = descriptors('3', '4');
        tampered[0].manifest_sha256 = std::string(64, 'c');
        write_file(
            state_root / "snapshots" / (generation + ".json"),
            snapshot_json(tampered, generation));
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, generation);
        check(!opened
                  && opened.error
                      == app::ServiceRuntimeRepositoryOpenError::invalid_activation,
              "descriptor tampering must fail generation validation");
    }
}

void test_expected_generation_is_exact_and_canonical()
{
    TemporaryRoot root{"expected"};
    const auto generation = install_generation(root.path, descriptors('4', '5'));
    const auto mismatch = app::open_service_runtime_repository_owner(
        root.path, std::string(64, 'f'));
    check(!mismatch
              && mismatch.error
                  == app::ServiceRuntimeRepositoryOpenError::generation_mismatch,
          "a complete activation for another generation must fail closed");

    for (const std::string invalid : {
             std::string(63, 'a'), std::string(64, 'A'), std::string(64, 'g')}) {
        const auto opened = app::open_service_runtime_repository_owner(
            root.path, invalid);
        check(!opened
                  && opened.error
                      == app::ServiceRuntimeRepositoryOpenError::invalid_expected_generation,
              "direct owner callers must supply a canonical generation");
    }
    check(generation != std::string(64, 'f'),
          "mismatch fixture must differ from the installed generation");
}

void test_concurrent_readers_observe_one_generation()
{
    TemporaryRoot root{"concurrent"};
    const auto first = descriptors('5', '6');
    const auto second = descriptors('7', '8');
    const auto first_generation = install_generation(root.path, first);
    const auto second_generation = install_generation(root.path, second, false);
    auto opened = app::open_service_runtime_repository_owner(
        root.path, first_generation);
    check(static_cast<bool>(opened), "concurrent fixture must open");
    if (!opened) return;
    auto owner = std::move(opened.owner);
    const auto initial_pin = owner->pin();

    const auto state_root =
        root.path / ".baas-updater" / "runtime-repositories";
    write_file(state_root / "current.json", current_json(second_generation));

    std::atomic<bool> mixed{false};
    std::vector<std::thread> readers;
    for (std::size_t index = 0; index < 8; ++index) {
        readers.emplace_back([&] {
            for (std::size_t iteration = 0; iteration < 10'000; ++iteration) {
                const auto pin = owner->pin();
                if (!pin || pin.get() != initial_pin.get()
                    || pin->generation() != first_generation
                    || owner->generation() != first_generation) {
                    mixed.store(true, std::memory_order_release);
                    return;
                }
            }
        });
    }
    for (auto& reader : readers) reader.join();
    check(!mixed.load(std::memory_order_acquire)
              && owner->generation() == first_generation
              && owner->generation() != second_generation,
          "all readers must retain the startup generation after current advances");
}

void test_stable_names_cover_public_states()
{
    using enum app::ServiceRuntimeRepositoryOpenError;
    for (const auto error : {
             none, invalid_expected_generation, generation_mismatch,
             invalid_activation, trusted_state_invalid,
             trusted_state_generation_mismatch,
             trusted_state_pending_recovery, internal_error}) {
        check(app::service_runtime_repository_open_error_name(error) != "unknown",
              "every repository owner error must have a stable name");
    }
    using enum app::ServiceRuntimeRepositoryPhase;
    for (const auto phase : {unavailable, pinned}) {
        check(app::service_runtime_repository_phase_name(phase) != "unknown",
              "every repository owner phase must have a stable name");
    }
}

}  // namespace

int main()
{
    try {
        test_missing_current_fails_expected_generation();
        test_valid_activation_is_pinned_for_owner_lifetime();
        test_trusted_state_is_required_exact_and_recovery_free();
        test_malformed_and_tampered_activation_fail_closed();
        test_expected_generation_is_exact_and_canonical();
        test_concurrent_readers_observe_one_generation();
        test_stable_names_cover_public_states();
    } catch (const std::exception& error) {
        std::cerr << "UNEXPECTED: " << error.what() << '\n';
        return 2;
    }
    if (failures != 0) {
        std::cerr << failures << " service runtime repository owner test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "service runtime repository owner tests passed\n";
    return EXIT_SUCCESS;
}
