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
#include <process.h>
#else
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
    if (publish) write_file(state_root / "current.json", current_json(generation));
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
    check(owner->phase() == app::ServiceRuntimeRepositoryPhase::pinned
              && owner->generation() == generation && pin
              && pin->generation() == generation,
          "valid activation must publish the exact immutable generation");

    std::error_code ignored;
    std::filesystem::remove_all(
        root.path / ".baas-updater" / "runtime-repositories", ignored);
    check(pin && pin->generation() == generation
              && pin->resources().commit == std::string(40, '1')
              && owner->generation() == generation,
          "owner and retained pins must outlive mutable activation files");
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
             invalid_activation, internal_error}) {
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
