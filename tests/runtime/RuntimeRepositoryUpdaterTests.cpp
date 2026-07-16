#include "runtime/repository/RuntimeRepositoryUpdater.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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

void check(const bool condition, const std::string_view message) {
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

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
            const auto entropy = (static_cast<unsigned long long>(random()) << 32U) ^ random();
            path_ = std::filesystem::temp_directory_path() /
                    ("baas-runtime-repository-updater-" + std::to_string(process) + "-" +
                     std::to_string(++next) + "-" + std::to_string(entropy));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
                return;
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error(
                    "test temporary directory creation failed", path_, error);
        }
        throw std::runtime_error("test temporary directory allocation exhausted");
    }

    ~TempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string_view contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output)
        throw std::runtime_error("test file write failed");
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string persisted_pointer(const std::string_view generation) {
    return "{\n  \"schema\": \"baas.runtime-repositories.current/v1\","
           "\n  \"generation\": \"" +
           std::string(generation) + "\",\n  \"snapshot\": \"snapshots/" + std::string(generation) +
           ".json\"\n}\n";
}

[[nodiscard]] std::string rollback_journal(const std::string_view phase,
                                           const std::string_view old_previous,
                                           const std::string_view old_current) {
    auto previous = persisted_pointer(old_previous);
    auto current = persisted_pointer(old_current);
    previous.pop_back();
    current.pop_back();
    return "{\n  \"schema\": \"baas.runtime-repositories.publish-journal/v1\","
           "\n  \"operation\": \"rollback\","
           "\n  \"phase\": \"" +
           std::string(phase) + "\",\n  \"old_previous\": " + previous +
           ",\n  \"old_current\": " + current + ",\n  \"new_previous\": " + current +
           ",\n  \"new_current\": " + previous + "\n}\n";
}

[[nodiscard]] std::string id_name(const repository::RuntimeRepositoryId id) {
    return id == repository::RuntimeRepositoryId::Resources ? "resources" : "scripts";
}

[[nodiscard]] repository::RuntimeRepositoryUpdatePlan plan(const char resource_commit,
                                                           const char script_commit,
                                                           const char resource_manifest_hash,
                                                           const char script_manifest_hash) {
    return {{{
        {repository::RuntimeRepositoryId::Resources, "https://invalid.example/resources.git",
         "refs/heads/main", std::string(40, resource_commit), "resources.json",
         std::string(64, resource_manifest_hash)},
        {repository::RuntimeRepositoryId::Scripts, "https://invalid.example/scripts.git",
         "refs/heads/main", std::string(40, script_commit), "scripts.json",
         std::string(64, script_manifest_hash)},
    }}};
}

[[nodiscard]] std::array<repository::RuntimeRepository, 2>
descriptors(const repository::RuntimeRepositoryUpdatePlan& update_plan) {
    std::array<repository::RuntimeRepository, 2> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto& spec = update_plan.repositories[index];
        const auto id = id_name(spec.id);
        result[index] = {id, spec.exact_commit, "objects/" + id + "/" + spec.exact_commit,
                         spec.manifest, spec.expected_manifest_sha256};
    }
    return result;
}

class EventLog final {
  public:
    void add(std::string event) {
        std::lock_guard lock(mutex_);
        events_.push_back(std::move(event));
    }

    [[nodiscard]] std::vector<std::string> snapshot() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<std::string> events_;
};

class FakePlanProvider final : public repository::RuntimeRepositoryUpdatePlanProvider {
  public:
    FakePlanProvider(repository::RuntimeRepositoryUpdatePlan value, EventLog& events)
        : value_(std::move(value)), events_(&events) {}

    [[nodiscard]] repository::RuntimeRepositoryUpdatePlan trusted_plan() const override {
        events_->add("plan");
        return value_;
    }

  private:
    repository::RuntimeRepositoryUpdatePlan value_;
    EventLog* events_;
};

class ThrowingPlanProvider final : public repository::RuntimeRepositoryUpdatePlanProvider {
  public:
    explicit ThrowingPlanProvider(std::string detail) : detail_(std::move(detail)) {}

    [[nodiscard]] repository::RuntimeRepositoryUpdatePlan trusted_plan() const override {
        throw std::runtime_error(detail_);
    }

  private:
    std::string detail_;
};

class FakeFetchBackend final : public repository::RuntimeRepositoryFetchBackend {
  public:
    explicit FakeFetchBackend(EventLog& events) : events_(&events) {}

    std::optional<repository::RuntimeRepositoryId> mismatch_id;
    std::optional<repository::RuntimeRepositoryId> fail_id;
    std::stop_source* cancel_after_stage{};
    std::vector<std::filesystem::path> staging_directories;

    [[nodiscard]] repository::RepositoryStageResult
    stage_exact(const repository::RepositoryFetchSpec& spec,
                const std::filesystem::path& staging_directory,
                const std::stop_token stop_token) override {
        events_->add("stage:" + id_name(spec.id));
        staging_directories.push_back(staging_directory);
        if (stop_token.stop_requested())
            throw std::runtime_error("fake backend received cancelled work");

        write_file(staging_directory / spec.manifest, "manifest:" + id_name(spec.id));
        write_file(staging_directory / "payload.bin", "payload:" + spec.exact_commit);
        if (fail_id && *fail_id == spec.id)
            throw std::runtime_error("injected fake backend failure");
        if (cancel_after_stage)
            cancel_after_stage->request_stop();

        if (mismatch_id && *mismatch_id == spec.id)
            return {std::string(spec.exact_commit.size(), 'f')};
        return {spec.exact_commit};
    }

  private:
    EventLog* events_;
};

class ThrowingFetchBackend final : public repository::RuntimeRepositoryFetchBackend {
  public:
    explicit ThrowingFetchBackend(std::string detail) : detail_(std::move(detail)) {}

    [[nodiscard]] repository::RepositoryStageResult
    stage_exact(const repository::RepositoryFetchSpec&, const std::filesystem::path&,
                std::stop_token) override {
        throw std::runtime_error(detail_);
    }

  private:
    std::string detail_;
};

class FakeTreeValidator final : public repository::RuntimeRepositoryTreeValidator {
  public:
    explicit FakeTreeValidator(EventLog& events) : events_(&events) {}

    [[nodiscard]] repository::RepositoryTreeSeal
    validate_and_seal(const repository::RepositoryFetchSpec& spec,
                      const std::filesystem::path& repository_root,
                      const std::stop_token stop_token) const override {
        events_->add("validate:" + id_name(spec.id));
        if (stop_token.stop_requested())
            throw std::runtime_error("fake validator received cancelled work");
        if (!std::filesystem::is_regular_file(repository_root / spec.manifest) ||
            !std::filesystem::is_regular_file(repository_root / "payload.bin"))
            throw std::runtime_error("fake validator received an incomplete tree");
        return {
            spec.expected_manifest_sha256,
            std::string(64, spec.id == repository::RuntimeRepositoryId::Resources ? 'e' : 'd'),
            2,
            static_cast<std::uintmax_t>(
                std::filesystem::file_size(repository_root / spec.manifest) +
                std::filesystem::file_size(repository_root / "payload.bin")),
        };
    }

  private:
    EventLog* events_;
};

class ThrowingTreeValidator final : public repository::RuntimeRepositoryTreeValidator {
  public:
    explicit ThrowingTreeValidator(std::string detail) : detail_(std::move(detail)) {}

    [[nodiscard]] repository::RepositoryTreeSeal
    validate_and_seal(const repository::RepositoryFetchSpec&, const std::filesystem::path&,
                      std::stop_token) const override {
        throw std::runtime_error(detail_);
    }

  private:
    std::string detail_;
};

class RejectInstalledRevalidationValidator final
    : public repository::RuntimeRepositoryTreeValidator {
  public:
    [[nodiscard]] repository::RepositoryTreeSeal
    validate_and_seal(const repository::RepositoryFetchSpec& spec,
                      const std::filesystem::path& repository_root,
                      const std::stop_token) const override {
        if (!std::filesystem::is_regular_file(repository_root / spec.manifest) ||
            !std::filesystem::is_regular_file(repository_root / "payload.bin"))
            throw std::runtime_error("incomplete injected tree");
        if (spec.id == repository::RuntimeRepositoryId::Resources && ++resource_calls_ == 2)
            throw std::runtime_error("injected installed-object revalidation failure");
        return {spec.expected_manifest_sha256, std::string(64, 'e'), 2, 16};
    }

  private:
    mutable std::size_t resource_calls_{};
};

constexpr std::string_view empty_sha256 =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
constexpr std::string_view manifest_sha256 =
    "11888349804e7ade124720093248848fa5fa6077a5c9daaec656024e40225f29";
constexpr std::string_view strict_manifest =
    "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[{"
    "\"path\":\"payload.bin\",\"size\":\"7\",\"sha256\":"
    "\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\","
    "\"mode\":\"file\"}]}\n";

[[nodiscard]] std::string tree_manifest_for(const std::vector<std::string_view>& paths) {
    std::string result = "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[";
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (index != 0)
            result.push_back(',');
        result += "{\"path\":\"" + std::string(paths[index]) +
                  "\",\"size\":\"7\",\"sha256\":"
                  "\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\","
                  "\"mode\":\"file\"}";
    }
    return result + "]}\n";
}

[[nodiscard]] std::string tree_manifest_for_encoded_json_path(const std::string_view path) {
    return "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\",\"entries\":[{"
           "\"path\":\"" +
           std::string(path) +
           "\",\"size\":\"7\",\"sha256\":"
           "\"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5\","
           "\"mode\":\"file\"}]}\n";
}

[[nodiscard]] repository::RepositoryFetchSpec
strict_spec_with_manifest_hash(const std::string_view hash) {
    return {repository::RuntimeRepositoryId::Resources,
            "https://invalid.example/resources.git",
            "refs/heads/main",
            std::string(40, '1'),
            "manifest.json",
            std::string(hash)};
}

[[nodiscard]] repository::RuntimeRepositoryUpdatePlan strict_plan(const char resource_commit,
                                                                  const char script_commit) {
    return {{{
        {repository::RuntimeRepositoryId::Resources, "https://invalid.example/resources.git",
         "refs/heads/main", std::string(40, resource_commit), "manifest.json",
         std::string(manifest_sha256)},
        {repository::RuntimeRepositoryId::Scripts, "https://invalid.example/scripts.git",
         "refs/heads/main", std::string(40, script_commit), "manifest.json",
         std::string(manifest_sha256)},
    }}};
}

class StrictFixtureBackend final : public repository::RuntimeRepositoryFetchBackend {
  public:
    [[nodiscard]] repository::RepositoryStageResult
    stage_exact(const repository::RepositoryFetchSpec& spec,
                const std::filesystem::path& staging_directory,
                const std::stop_token stop_token) override {
        if (stop_token.stop_requested())
            throw std::runtime_error("strict fixture received cancelled work");
        write_file(staging_directory / spec.manifest, strict_manifest);
        write_file(staging_directory / "payload.bin", "payload");
        return {spec.exact_commit};
    }
};

class BlockingFetchBackend final : public repository::RuntimeRepositoryFetchBackend {
  public:
    [[nodiscard]] repository::RepositoryStageResult
    stage_exact(const repository::RepositoryFetchSpec& spec,
                const std::filesystem::path& staging_directory, const std::stop_token) override {
        write_file(staging_directory / spec.manifest, "manifest:" + id_name(spec.id));
        write_file(staging_directory / "payload.bin", "payload:" + spec.exact_commit);
        {
            std::lock_guard lock(mutex_);
            if (!blocked_once_) {
                blocked_once_ = true;
                entered_ = true;
            }
        }
        condition_.notify_all();
        std::unique_lock lock(mutex_);
        if (entered_ && !released_)
            condition_.wait(lock, [&] { return released_; });
        return {spec.exact_commit};
    }

    [[nodiscard]] bool wait_until_entered() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(5), [&] { return entered_; });
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool blocked_once_{};
    bool entered_{};
    bool released_{};
};

[[nodiscard]] std::string
checkpoint_name(const repository::RuntimeRepositoryUpdaterCheckpoint checkpoint) {
    using Checkpoint = repository::RuntimeRepositoryUpdaterCheckpoint;
    switch (checkpoint) {
    case Checkpoint::PlanValidated:
        return "plan-validated";
    case Checkpoint::ResourcesStaged:
        return "resources-staged";
    case Checkpoint::ScriptsStaged:
        return "scripts-staged";
    case Checkpoint::CandidatesSealed:
        return "candidates-sealed";
    case Checkpoint::ObjectsCommitted:
        return "objects-committed";
    case Checkpoint::SnapshotInstalled:
        return "snapshot-installed";
    case Checkpoint::JournalPrepared:
        return "journal-prepared";
    case Checkpoint::PreviousReplaced:
        return "previous-replaced";
    case Checkpoint::BeforeCurrentReplace:
        return "before-current-replace";
    case Checkpoint::CurrentReplaced:
        return "current-replaced";
    case Checkpoint::JournalRemoved:
        return "journal-removed";
    }
    return "unknown";
}

class FaultHooks final : public repository::RuntimeRepositoryUpdaterHooks {
  public:
    explicit FaultHooks(EventLog& events) : events_(&events) {}

    std::optional<repository::RuntimeRepositoryUpdaterCheckpoint> fail_at;
    std::vector<repository::RuntimeRepositoryUpdaterCheckpoint> committed_observations;

    void checkpoint(const repository::RuntimeRepositoryUpdaterCheckpoint checkpoint) override {
        events_->add("checkpoint:" + checkpoint_name(checkpoint));
        if (fail_at && *fail_at == checkpoint)
            throw std::runtime_error("injected updater checkpoint failure");
    }

    void
    committed(const repository::RuntimeRepositoryUpdaterCheckpoint checkpoint) noexcept override {
        committed_observations.push_back(checkpoint);
        try {
            events_->add("committed:" + checkpoint_name(checkpoint));
        } catch (...) {
        }
    }

  private:
    EventLog* events_;
};

class RecordingDiagnosticHooks : public repository::RuntimeRepositoryUpdaterHooks {
  public:
    void checkpoint(repository::RuntimeRepositoryUpdaterCheckpoint) override {}

    void diagnostic(const repository::RuntimeRepositoryUpdateErrorCode code,
                    const std::string_view detail) noexcept override {
        try {
            diagnostic_code = code;
            diagnostic_detail.assign(detail);
        } catch (...) {
        }
    }

    std::optional<repository::RuntimeRepositoryUpdateErrorCode> diagnostic_code;
    std::string diagnostic_detail;
};

class ThrowingFilesystemHooks final : public RecordingDiagnosticHooks {
  public:
    ThrowingFilesystemHooks(std::string detail, std::filesystem::path path)
        : detail_(std::move(detail)), path_(std::move(path)) {}

    void checkpoint(const repository::RuntimeRepositoryUpdaterCheckpoint checkpoint) override {
        if (checkpoint == repository::RuntimeRepositoryUpdaterCheckpoint::PlanValidated)
            throw std::filesystem::filesystem_error(
                detail_, path_, std::make_error_code(std::errc::permission_denied));
    }

  private:
    std::string detail_;
    std::filesystem::path path_;
};

class BlockingCheckpointHooks final : public repository::RuntimeRepositoryUpdaterHooks {
  public:
    void arm() noexcept { armed_.store(true, std::memory_order_release); }

    void checkpoint(const repository::RuntimeRepositoryUpdaterCheckpoint checkpoint) override {
        if (checkpoint != repository::RuntimeRepositoryUpdaterCheckpoint::BeforeCurrentReplace ||
            !armed_.load(std::memory_order_acquire))
            return;
        std::unique_lock lock(mutex_);
        entered_ = true;
        condition_.notify_all();
        condition_.wait(lock, [&] { return released_; });
    }

    [[nodiscard]] bool wait_until_entered() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(5), [&] { return entered_; });
    }

    void release() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

  private:
    std::atomic<bool> armed_{};
    std::mutex mutex_;
    std::condition_variable condition_;
    bool entered_{};
    bool released_{};
};

class CancelOnCommittedHooks final : public repository::RuntimeRepositoryUpdaterHooks {
  public:
    void arm(std::stop_source& source) noexcept { source_ = &source; }

    void checkpoint(repository::RuntimeRepositoryUpdaterCheckpoint) override {}

    void
    committed(const repository::RuntimeRepositoryUpdaterCheckpoint checkpoint) noexcept override {
        if (checkpoint != repository::RuntimeRepositoryUpdaterCheckpoint::CurrentReplaced ||
            source_ == nullptr)
            return;
        observed_.store(true, std::memory_order_release);
        source_->request_stop();
    }

    [[nodiscard]] bool observed() const noexcept {
        return observed_.load(std::memory_order_acquire);
    }

  private:
    std::stop_source* source_{};
    std::atomic<bool> observed_{};
};

class FileOperationFaultHooks final : public repository::RuntimeRepositoryUpdaterHooks {
  public:
    std::optional<repository::RuntimeRepositoryFileOperation> fail_at;

    void checkpoint(repository::RuntimeRepositoryUpdaterCheckpoint) override {}

    void
    before_file_operation(const repository::RuntimeRepositoryFileOperation operation) override {
        if (fail_at && *fail_at == operation)
            throw std::runtime_error("injected updater file operation failure");
    }
};

[[nodiscard]] std::size_t first_event(const std::vector<std::string>& events,
                                      const std::string_view event) {
    const auto found = std::find(events.begin(), events.end(), event);
    return found == events.end() ? events.size()
                                 : static_cast<std::size_t>(std::distance(events.begin(), found));
}

void check_event_order(const std::vector<std::string>& events,
                       const std::initializer_list<std::string_view> expected) {
    std::size_t previous{};
    bool first = true;
    for (const auto event : expected) {
        const auto position = first_event(events, event);
        check(position != events.size(), "expected updater event is absent");
        if (!first)
            check(position > previous, "updater events occurred out of order");
        previous = position;
        first = false;
    }
}

[[nodiscard]] bool is_within(const std::filesystem::path& root,
                             const std::filesystem::path& candidate) {
    const auto relative = candidate.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || relative.is_absolute())
        return false;
    const auto first = relative.begin();
    return first == relative.end() || *first != "..";
}

[[nodiscard]] bool directory_empty(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return !error;
    return std::filesystem::directory_iterator(path, error) ==
               std::filesystem::directory_iterator{} &&
           !error;
}

void check_error(const repository::RuntimeRepositoryUpdateResult& result,
                 const repository::RuntimeRepositoryUpdateErrorCode expected,
                 const std::string_view message) {
    check(!result && result.error.has_value() && result.error->code == expected, message);
    check(result.disposition == repository::PublishDisposition::NotCommitted,
          "ordinary updater failure must report not committed");
}

void check_success(const repository::RuntimeRepositoryUpdateResult& result,
                   const std::string_view message) {
    if (!result && result.error)
        std::cerr << "UPDATE ERROR [" << static_cast<int>(result.error->code)
                  << "]: " << result.error->message << '\n';
    check(static_cast<bool>(result), message);
}

void check_redacted_error(const repository::RuntimeRepositoryUpdateResult& result,
                          const repository::RuntimeRepositoryUpdateErrorCode expected,
                          const RecordingDiagnosticHooks& hooks,
                          const std::string_view sensitive_detail,
                          const std::string_view message) {
    check_error(result, expected, message);
    check(result.error &&
              result.error->message == repository::runtime_repository_update_error_message(expected),
          "public updater errors must use the stable message for their category");
    check(result.error && result.error->message.find("secret-token") == std::string::npos &&
              result.error->message.find("alice:password") == std::string::npos &&
              result.error->message.find("C:\\Users\\private") == std::string::npos,
          "public updater errors must not expose credentials or local paths");
    check(hooks.diagnostic_code == expected &&
              hooks.diagnostic_detail.find(sensitive_detail) != std::string::npos,
          "trusted local diagnostics must retain the original failure detail");
}

void test_public_errors_are_stable_and_sensitive_diagnostics_stay_local() {
    constexpr std::string_view sensitive_url =
        "https://alice:password@example.invalid/repository.git?token=secret-token";
    constexpr std::string_view sensitive_path = "C:\\Users\\private\\runtime-repositories";
    const auto detail = std::string("backend failed for ") + std::string(sensitive_url) +
                        " through proxy at " + std::string(sensitive_path);

    {
        TempDirectory temporary;
        EventLog events;
        auto hooks = std::make_shared<RecordingDiagnosticHooks>();
        repository::RuntimeRepositoryUpdater updater(temporary.path() / "state", hooks);
        ThrowingPlanProvider provider(detail);
        FakeFetchBackend backend(events);
        FakeTreeValidator validator(events);
        const auto result = updater.update(provider, backend, validator);
        check_redacted_error(result, repository::RuntimeRepositoryUpdateErrorCode::InvalidPlan,
                             *hooks, sensitive_url,
                             "plan-provider exceptions must be categorized and redacted");
    }
    {
        TempDirectory temporary;
        EventLog events;
        auto hooks = std::make_shared<RecordingDiagnosticHooks>();
        repository::RuntimeRepositoryUpdater updater(temporary.path() / "state", hooks);
        FakePlanProvider provider(plan('1', '2', 'a', 'b'), events);
        ThrowingFetchBackend backend(detail);
        FakeTreeValidator validator(events);
        const auto result = updater.update(provider, backend, validator);
        check_redacted_error(result, repository::RuntimeRepositoryUpdateErrorCode::FetchFailed,
                             *hooks, sensitive_url,
                             "fetch-backend exceptions must be categorized and redacted");
    }
    {
        TempDirectory temporary;
        EventLog events;
        auto hooks = std::make_shared<RecordingDiagnosticHooks>();
        repository::RuntimeRepositoryUpdater updater(temporary.path() / "state", hooks);
        FakePlanProvider provider(plan('1', '2', 'a', 'b'), events);
        FakeFetchBackend backend(events);
        ThrowingTreeValidator validator(detail);
        const auto result = updater.update(provider, backend, validator);
        check_redacted_error(result,
                             repository::RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                             *hooks, sensitive_path,
                             "validator exceptions must be categorized and redacted");
    }
    {
        TempDirectory temporary;
        EventLog events;
        auto hooks = std::make_shared<ThrowingFilesystemHooks>(detail, sensitive_path);
        repository::RuntimeRepositoryUpdater updater(temporary.path() / "state", hooks);
        FakePlanProvider provider(plan('1', '2', 'a', 'b'), events);
        FakeFetchBackend backend(events);
        FakeTreeValidator validator(events);
        const auto result = updater.update(provider, backend, validator);
        check_redacted_error(result, repository::RuntimeRepositoryUpdateErrorCode::Io, *hooks,
                             sensitive_path,
                             "filesystem exceptions must be categorized and redacted");
    }
}

void test_update_order_protocol_vector_and_pin() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    auto hooks = std::make_shared<FaultHooks>(events);
    repository::RuntimeRepositoryUpdater updater(state_root, hooks);

    const auto first_plan = plan('1', '2', 'a', 'b');
    FakePlanProvider first_provider(first_plan, events);
    FakeFetchBackend backend(events);
    FakeTreeValidator validator(events);
    const auto first =
        updater.update(first_provider, backend, validator, repository::ExpectedCurrent::absent());

    constexpr std::string_view fixture_generation =
        "24f74e7ebac7bb8920e775edb8fa377384c6bf2317afa4dd9a85b42b7695e1ae";
    check_success(first, "a valid two-repository update must succeed");
    check(first.disposition == repository::PublishDisposition::Committed,
          "a durable successful update must report committed");
    check(first.pinned_generation == fixture_generation && first.pinned_bundle &&
              first.pinned_bundle->generation() == fixture_generation,
          "update result must pin the canonical cross-language generation vector");
    check(repository::runtime_repository_generation(descriptors(first_plan)) == fixture_generation,
          "test fixture must retain the C++/Tauri generation contract vector");
    check(std::filesystem::is_regular_file(state_root / "current.json") &&
              std::filesystem::is_regular_file(state_root / "snapshots" /
                                               (std::string(fixture_generation) + ".json")),
          "updater must publish the canonical current/snapshot layout");
    check(read_file(state_root / "current.json")
                  .find("snapshots/" + std::string(fixture_generation) + ".json") !=
              std::string::npos,
          "current pointer must name its generation snapshot");

    const auto recorded = events.snapshot();
    check_event_order(recorded, {
                                    "plan",
                                    "checkpoint:plan-validated",
                                    "stage:resources",
                                    "checkpoint:resources-staged",
                                    "validate:resources",
                                    "stage:scripts",
                                    "checkpoint:scripts-staged",
                                    "validate:scripts",
                                    "checkpoint:candidates-sealed",
                                    "checkpoint:objects-committed",
                                    "checkpoint:snapshot-installed",
                                    "checkpoint:journal-prepared",
                                    "checkpoint:previous-replaced",
                                    "checkpoint:before-current-replace",
                                    "committed:current-replaced",
                                    "committed:journal-removed",
                                });
    check(backend.staging_directories.size() == 2 &&
              std::all_of(backend.staging_directories.begin(), backend.staging_directories.end(),
                          [&](const auto& path) { return is_within(state_root, path); }),
          "backend must receive only updater-owned staging directories under the "
          "state root");

    const auto pinned_old = updater.pin_current();
    check(pinned_old && pinned_old->generation() == fixture_generation,
          "pin_current must return the committed immutable bundle");

    const auto second_plan = plan('3', '4', 'c', 'd');
    const auto second_generation =
        repository::runtime_repository_generation(descriptors(second_plan));
    FakePlanProvider second_provider(second_plan, events);
    const auto second =
        updater.update(second_provider, backend, validator,
                       repository::ExpectedCurrent::exact(std::string(fixture_generation)));
    check_success(second, "exact-current CAS update must succeed");
    check(second.pinned_generation == second_generation,
          "exact-current CAS must publish the next generation");
    check(pinned_old->generation() == fixture_generation &&
              pinned_old->resources().commit == std::string(40, '1'),
          "an old bundle pin must remain immutable after current advances");
    const auto pinned_new = updater.pin_current();
    check(pinned_new && pinned_new->generation() == second_generation,
          "pin_current must observe the latest complete generation");
}

void test_commit_oid_mismatch_fails_closed() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    FakePlanProvider provider(plan('1', '2', 'a', 'b'), events);
    FakeFetchBackend backend(events);
    backend.mismatch_id = repository::RuntimeRepositoryId::Resources;
    FakeTreeValidator validator(events);

    const auto result =
        updater.update(provider, backend, validator, repository::ExpectedCurrent::absent());
    check_error(result, repository::RuntimeRepositoryUpdateErrorCode::CommitMismatch,
                "a backend-resolved OID mismatch must fail closed");
    const auto recorded = events.snapshot();
    check(first_event(recorded, "stage:resources") != recorded.size() &&
              first_event(recorded, "stage:scripts") == recorded.size() &&
              first_event(recorded, "validate:resources") == recorded.size(),
          "OID mismatch must stop before another repository is staged or "
          "validated");
    check(!std::filesystem::exists(state_root / "current.json"),
          "OID mismatch must not publish current");
}

void test_cancelled_update_does_not_publish() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    FakePlanProvider provider(plan('1', '2', 'a', 'b'), events);
    FakeFetchBackend backend(events);
    FakeTreeValidator validator(events);
    std::stop_source stop;
    stop.request_stop();

    const auto result = updater.update(provider, backend, validator,
                                       repository::ExpectedCurrent::absent(), stop.get_token());
    check_error(result, repository::RuntimeRepositoryUpdateErrorCode::Cancelled,
                "a pre-cancelled update must report cancellation");
    check(!std::filesystem::exists(state_root / "current.json"),
          "cancelled update must not publish current");
}

void test_second_repository_failure_cleans_all_staging() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    FakePlanProvider provider(plan('1', '2', 'a', 'b'), events);
    FakeFetchBackend backend(events);
    backend.fail_id = repository::RuntimeRepositoryId::Scripts;
    FakeTreeValidator validator(events);

    const auto result =
        updater.update(provider, backend, validator, repository::ExpectedCurrent::absent());
    check_error(result, repository::RuntimeRepositoryUpdateErrorCode::FetchFailed,
                "second repository fetch failure must be reported");
    const auto recorded = events.snapshot();
    check_event_order(recorded, {
                                    "stage:resources",
                                    "validate:resources",
                                    "stage:scripts",
                                });
    check(directory_empty(state_root / "staging"),
          "second repository failure must remove both staging directories");
    check(!std::filesystem::exists(state_root / "current.json"),
          "partial two-repository fetch must not publish current");
}

void test_cancellation_after_first_stage_cleans_staging() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    FakePlanProvider provider(plan('1', '2', 'a', 'b'), events);
    FakeFetchBackend backend(events);
    FakeTreeValidator validator(events);
    std::stop_source stop;
    backend.cancel_after_stage = &stop;

    const auto result = updater.update(provider, backend, validator,
                                       repository::ExpectedCurrent::absent(), stop.get_token());
    check_error(result, repository::RuntimeRepositoryUpdateErrorCode::Cancelled,
                "cancellation requested by the first staged repository must stop "
                "the update");
    const auto recorded = events.snapshot();
    check(first_event(recorded, "stage:resources") != recorded.size() &&
              first_event(recorded, "stage:scripts") == recorded.size() &&
              first_event(recorded, "validate:resources") == recorded.size(),
          "post-stage cancellation must stop before sealing or staging the "
          "second repository");
    check(directory_empty(state_root / "staging"),
          "post-stage cancellation must remove allocated staging directories");
}

void test_path_escape_plan_is_rejected_before_fetch() {
    TempDirectory temporary;
    EventLog events;
    auto escaped = plan('1', '2', 'a', 'b');
    escaped.repositories[0].manifest = "../resources.json";
    FakePlanProvider provider(std::move(escaped), events);
    FakeFetchBackend backend(events);
    FakeTreeValidator validator(events);
    repository::RuntimeRepositoryUpdater updater(temporary.path() / "runtime-repositories");

    const auto result =
        updater.update(provider, backend, validator, repository::ExpectedCurrent::absent());
    check_error(result, repository::RuntimeRepositoryUpdateErrorCode::InvalidPlan,
                "manifest path escape must be rejected as an invalid trusted plan");
    const auto recorded = events.snapshot();
    check(first_event(recorded, "stage:resources") == recorded.size(),
          "path escape must fail before untrusted backend execution");

    EventLog reference_events;
    auto missing_reference = plan('1', '2', 'a', 'b');
    missing_reference.repositories[0].advertised_reference.clear();
    FakePlanProvider reference_provider(std::move(missing_reference), reference_events);
    FakeFetchBackend reference_backend(reference_events);
    FakeTreeValidator reference_validator(reference_events);
    const auto reference_result =
        updater.update(reference_provider, reference_backend, reference_validator,
                       repository::ExpectedCurrent::absent());
    check_error(reference_result, repository::RuntimeRepositoryUpdateErrorCode::InvalidPlan,
                "an advertised reference is required before fetching an exact "
                "commit");
    check(first_event(reference_events.snapshot(), "stage:resources") ==
              reference_events.snapshot().size(),
          "a missing advertised reference must fail before backend execution");
}

#ifdef _WIN32
[[nodiscard]] bool create_junction(const std::filesystem::path& target,
                                   const std::filesystem::path& junction) {
    std::error_code error;
    std::filesystem::create_directory(junction, error);
    if (error)
        return false;

    const auto absolute_target = std::filesystem::absolute(target).wstring();
    const auto substitute = L"\\??\\" + absolute_target;
    const auto substitute_bytes = static_cast<unsigned short>(substitute.size() * sizeof(wchar_t));
    const auto print_bytes = static_cast<unsigned short>(absolute_target.size() * sizeof(wchar_t));
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
    std::memcpy(reinterpret_cast<std::byte*>(buffer->paths) + buffer->print_offset,
                absolute_target.c_str(), print_bytes);
    *reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(buffer->paths) + buffer->print_offset +
                                print_bytes) = L'\0';

    const auto handle =
        CreateFileW(junction.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned{};
    const auto success =
        DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer, static_cast<DWORD>(total), nullptr,
                        0, &returned, nullptr) != FALSE;
    CloseHandle(handle);
    return success;
}
#endif

[[nodiscard]] bool create_directory_link(const std::filesystem::path& target,
                                         const std::filesystem::path& link) {
#ifdef _WIN32
    return create_junction(target, link);
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(target, link, error);
    return !error;
#endif
}

#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
std::filesystem::path swap_test_outside;
std::atomic<bool> swap_test_executed{};
std::atomic<std::size_t> validation_read_count{};

void swap_parent_before_anchored_read(const std::filesystem::path& root,
                                      const std::filesystem::path& relative) {
    if (relative.generic_string() != "inside/payload.bin" || swap_test_executed.exchange(true))
        return;
    std::filesystem::rename(root / "inside", root / "inside-original");
    if (!create_directory_link(swap_test_outside, root / "inside"))
        throw std::runtime_error("test parent swap link creation failed");
}

void count_validation_read(const std::filesystem::path&, const std::filesystem::path&) {
    validation_read_count.fetch_add(1, std::memory_order_relaxed);
}
#endif

void test_parent_swap_between_enumeration_and_read_fails_closed() {
#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
    TempDirectory temporary;
    const auto root = temporary.path() / "candidate";
    const auto outside = temporary.path() / "outside";
    write_file(root / "manifest.json", tree_manifest_for({"inside/payload.bin"}));
    write_file(root / "inside" / "payload.bin", "payload");
    write_file(outside / "payload.bin", "outside");
    const auto spec = strict_spec_with_manifest_hash(
        "8c7d7e43165e8d8b19260fc30a053b848a707c7d1aae3a67dc163351bd9d0fe9");
    swap_test_outside = outside;
    swap_test_executed.store(false);
    repository::set_runtime_repository_validation_read_hook_for_testing(
        &swap_parent_before_anchored_read);
    bool rejected{};
    try {
        repository::StrictRuntimeRepositoryTreeValidator validator;
        static_cast<void>(validator.validate_and_seal(spec, root, {}));
    } catch (...) {
        rejected = true;
    }
    repository::set_runtime_repository_validation_read_hook_for_testing(nullptr);
    check(swap_test_executed.load() && rejected,
          "a parent swapped to an external link between enumeration and read "
          "must fail closed");
#endif
}

void test_strict_validator_rejects_link_escape() {
    TempDirectory temporary;
    const auto repository_root = temporary.path() / "candidate";
    const auto outside = temporary.path() / "outside";
    write_file(repository_root / "manifest.json", "");
    write_file(outside / "payload.bin", "outside");
    const auto linked = create_directory_link(outside, repository_root / "escaped");
    check(linked, "platform test must create a directory link/reparse point");
    if (!linked)
        return;

    repository::RepositoryFetchSpec spec{
        repository::RuntimeRepositoryId::Resources,
        "https://invalid.example/resources.git",
        "refs/heads/main",
        std::string(40, '1'),
        "manifest.json",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    };
    repository::StrictRuntimeRepositoryTreeValidator validator;
    bool rejected{};
    try {
        static_cast<void>(validator.validate_and_seal(spec, repository_root, {}));
    } catch (...) {
        rejected = true;
    }
    check(rejected, "strict tree validation must reject linked payload escape");
}

void test_strict_validator_rejects_staging_hard_links() {
    TempDirectory temporary;
    const auto root = temporary.path() / "candidate";
    const auto outside = temporary.path() / "outside-payload.bin";
    write_file(root / "manifest.json", strict_manifest);
    write_file(outside, "payload");
    std::error_code link_error;
    std::filesystem::create_hard_link(outside, root / "payload.bin", link_error);
    check(!link_error, "hard-link staging fixture must be created on the test filesystem");
    if (link_error)
        return;

    bool rejected{};
    try {
        repository::StrictRuntimeRepositoryTreeValidator validator;
        static_cast<void>(validator.validate_and_seal(
            strict_spec_with_manifest_hash(manifest_sha256), root, {}));
    } catch (...) {
        rejected = true;
    }
    check(rejected, "strict validation must reject a staged file with link count not equal to one");
}

[[nodiscard]] bool strict_validation_rejected(const std::filesystem::path& root,
                                              const repository::RepositoryValidationLimits limits) {
    const repository::RepositoryFetchSpec spec{
        repository::RuntimeRepositoryId::Resources,
        "https://invalid.example/resources.git",
        "refs/heads/main",
        std::string(40, '1'),
        "manifest.json",
        std::string(empty_sha256),
    };
    try {
        const repository::StrictRuntimeRepositoryTreeValidator validator(limits);
        static_cast<void>(validator.validate_and_seal(spec, root, {}));
        return false;
    } catch (...) {
        return true;
    }
}

void test_strict_validator_enforces_file_and_byte_limits() {
    TempDirectory temporary;
    const auto root = temporary.path() / "candidate";
    write_file(root / "manifest.json", "");
    write_file(root / "payload.bin", "payload");

    auto files = repository::RepositoryValidationLimits{};
    files.max_files = 1;
    check(strict_validation_rejected(root, files),
          "strict validator must enforce the total file-count limit");

    auto total_bytes = repository::RepositoryValidationLimits{};
    total_bytes.max_total_bytes = 1;
    check(strict_validation_rejected(root, total_bytes),
          "strict validator must enforce the aggregate byte limit");

    auto file_bytes = repository::RepositoryValidationLimits{};
    file_bytes.max_file_bytes = 1;
    check(strict_validation_rejected(root, file_bytes),
          "strict validator must enforce the per-file byte limit");

    auto entries = repository::RepositoryValidationLimits{};
    entries.max_files = 2;
    entries.max_entries = 2;
    std::filesystem::create_directory(root / "extra-directory");
    check(strict_validation_rejected(root, entries),
          "strict validator must bound file and directory entries together");

    auto depth = repository::RepositoryValidationLimits{};
    depth.max_relative_path_depth = 1;
    write_file(root / "nested" / "payload.bin", "payload");
    check(strict_validation_rejected(root, depth),
          "strict validator must enforce relative path depth");
}

void test_manifest_limit_applies_before_hashing() {
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "candidate";
        write_file(root / "manifest.json", strict_manifest);
        write_file(root / "payload.bin", "payload");
        auto limits = repository::RepositoryValidationLimits{};
        limits.max_manifest_bytes = strict_manifest.size();
        bool accepted{};
        try {
            const repository::StrictRuntimeRepositoryTreeValidator validator(limits);
            accepted = validator
                           .validate_and_seal(
                               strict_spec_with_manifest_hash(manifest_sha256), root, {})
                           .file_count == 2;
        } catch (...) {
        }
        check(accepted, "a manifest exactly at max_manifest_bytes must be accepted");
    }
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "candidate";
        write_file(root / "manifest.json", strict_manifest);
        write_file(root / "payload.bin", "payload");
        auto limits = repository::RepositoryValidationLimits{};
        limits.max_manifest_bytes = strict_manifest.size() - 1;
#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
        validation_read_count.store(0, std::memory_order_release);
        repository::set_runtime_repository_validation_read_hook_for_testing(
            &count_validation_read);
#endif
        bool rejected{};
        try {
            const repository::StrictRuntimeRepositoryTreeValidator validator(limits);
            static_cast<void>(validator.validate_and_seal(
                strict_spec_with_manifest_hash(manifest_sha256), root, {}));
        } catch (...) {
            rejected = true;
        }
#ifdef BAAS_RUNTIME_REPOSITORY_UPDATER_TESTING
        repository::set_runtime_repository_validation_read_hook_for_testing(nullptr);
        check(validation_read_count.load(std::memory_order_acquire) == 0,
              "an oversized manifest must be rejected before any complete file hash read");
#endif
        check(rejected, "a manifest above max_manifest_bytes must be rejected");
    }
}

void test_many_empty_directories_fail_without_quadratic_scan() {
    TempDirectory temporary;
    const auto root = temporary.path() / "candidate";
    write_file(root / "manifest.json", "{\"schema\":\"baas.runtime-repository.tree-manifest/v1\","
                                       "\"entries\":[]}\n");
    for (std::size_t index = 0; index < 2'000; ++index)
        std::filesystem::create_directories(root / ("empty-" + std::to_string(index)));
    const auto spec = strict_spec_with_manifest_hash(
        "2ffd016a8367418f7c58e5972b391e31a105d4c86ccf48eba481814c3f1df097");
    repository::StrictRuntimeRepositoryTreeValidator validator;
    bool rejected{};
    try {
        static_cast<void>(validator.validate_and_seal(spec, root, {}));
    } catch (...) {
        rejected = true;
    }
    check(rejected, "large empty-directory trees must fail closed with an indexed ancestor "
                    "check");
}

void test_strict_manifest_portable_utf8_contract() {
    repository::StrictRuntimeRepositoryTreeValidator validator;
    const std::string cjk_path = "\xe6\x8f\x8f\xe8\xbf\xb0/"
                                 "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e/"
                                 "\xed\x95\x9c\xea\xb8\x80 file.txt";
    {
        TempDirectory temporary;
        const auto root = temporary.path() / "candidate";
        const auto manifest = tree_manifest_for({cjk_path});
        write_file(root / "manifest.json", manifest);
        write_file(root / std::filesystem::path(std::u8string(
                              reinterpret_cast<const char8_t*>(cjk_path.data()), cjk_path.size())),
                   "payload");
        const auto spec = strict_spec_with_manifest_hash(
            "36a94a01c0054fa1672faf941d81b60046343d09b7bcef580c4a197dfc9bd1b2");
        bool accepted{};
        try {
            const auto seal = validator.validate_and_seal(spec, root, {});
            accepted = seal.file_count == 2;
        } catch (...) {
        }
        check(accepted, "strict manifest must accept canonical CJK, Kana, and Hangul UTF-8 "
                        "paths end to end");
    }

    const auto rejected_manifest = [&](const std::string_view path, const std::string_view hash,
                                       const std::string_view message) {
        TempDirectory temporary;
        const auto root = temporary.path() / "candidate";
        write_file(root / "manifest.json", tree_manifest_for({path}));
        const auto spec = strict_spec_with_manifest_hash(hash);
        bool rejected{};
        try {
            static_cast<void>(validator.validate_and_seal(spec, root, {}));
        } catch (...) {
            rejected = true;
        }
        check(rejected, message);
    };
    rejected_manifest("e\xcc\x81.txt",
                      "629ce6b7e40abfb0b4f738847b99e8898372d45b1d2172af750f562ff9db436c",
                      "strict manifest must reject decomposed combining path aliases");
    rejected_manifest("CON.txt", "8c53b20804498785bc52a130ce362c5c5bcc73fb289a02811cae92eee4233a82",
                      "strict manifest must reject Windows reserved path aliases");
    rejected_manifest("COM\xc2\xb9.txt",
                      "224c05181b1804d9b3b525356f1ebf35ccd071bb00909e8aa72393fed9a9a4c5",
                      "strict manifest must reject superscript Win32 device aliases");

    {
        TempDirectory temporary;
        const auto root = temporary.path() / "candidate";
        write_file(root / "manifest.json", tree_manifest_for({"Foo.txt", "foo.txt"}));
        const auto spec = strict_spec_with_manifest_hash(
            "4c914210656fd384efcf23a10a415b4ca33e2904e03edbf15b71e7e9e9c0641b");
        bool rejected{};
        try {
            static_cast<void>(validator.validate_and_seal(spec, root, {}));
        } catch (...) {
            rejected = true;
        }
        check(rejected, "strict manifest must reject ASCII case-folding path collisions");
    }
}

void test_json_unicode_escapes_match_standard_json_behavior() {
    repository::StrictRuntimeRepositoryTreeValidator validator;
    const auto accepted_manifest = [&](const std::string_view encoded_path,
                                       const std::string_view decoded_path,
                                       const std::string_view manifest_hash,
                                       const std::string_view message) {
        TempDirectory temporary;
        const auto root = temporary.path() / "candidate";
        write_file(root / "manifest.json", tree_manifest_for_encoded_json_path(encoded_path));
        write_file(root / std::filesystem::path(std::u8string(
                              reinterpret_cast<const char8_t*>(decoded_path.data()),
                              decoded_path.size())),
                   "payload");
        bool accepted{};
        try {
            const auto seal = validator.validate_and_seal(
                strict_spec_with_manifest_hash(manifest_hash), root, {});
            accepted = seal.file_count == 2;
        } catch (...) {
        }
        check(accepted, message);
    };

    const std::string cjk_path = "\xe6\x8f\x8f\xe8\xbf\xb0/"
                                 "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e/"
                                 "\xed\x95\x9c\xea\xb8\x80 file.txt";
    accepted_manifest("\\u63cf\\u8ff0/\\u65e5\\u672c\\u8a9e/\\ud55c\\uae00 file.txt",
                      cjk_path,
                      "e9c5c589bc0d8b11ea8c8387ae899fc93c94b27032312b45e6463f43543bc8bb",
                      "standard CJK Unicode escapes must decode to canonical UTF-8 paths");
    accepted_manifest("rocket-\\ud83d\\ude80.txt", "rocket-\xf0\x9f\x9a\x80.txt",
                      "40f7a63b67ad88cfbdb6f5a1cf85d88f0c2a0860f6f8a855c2e0fd6cbc89712d",
                      "a valid JSON surrogate pair must decode to one supplementary code point");

    const auto rejected_manifest = [&](const std::string_view encoded_path,
                                       const std::string_view manifest_hash,
                                       const std::string_view expected_diagnostic,
                                       const std::string_view message) {
        TempDirectory temporary;
        const auto root = temporary.path() / "candidate";
        write_file(root / "manifest.json", tree_manifest_for_encoded_json_path(encoded_path));
        std::string diagnostic;
        try {
            static_cast<void>(validator.validate_and_seal(
                strict_spec_with_manifest_hash(manifest_hash), root, {}));
        } catch (const std::exception& error) {
            diagnostic = error.what();
        }
        check(diagnostic.find(expected_diagnostic) != std::string::npos, message);
    };
    rejected_manifest("bad-\\ud83d.txt",
                      "aac28b6ad82a2d812386f8cf8556dc30ecf8e7c5bb0f3d06c4d0018561586e8c",
                      "high surrogate", "an isolated high surrogate must be rejected");
    rejected_manifest("bad-\\ude80.txt",
                      "b38b8acca629e932310a799e79d100afeff5f4f140e3c622a0822ec5e0451bf4",
                      "isolated low surrogate", "an isolated low surrogate must be rejected");
    rejected_manifest("bad-\\ud83d\\u0041.txt",
                      "5e38bbf3ed4851e8494acb3a769436e1d0d7cc3cce1e69d26d7ee2e4057fc08f",
                      "high surrogate", "a high surrogate followed by a BMP value must fail");
    rejected_manifest("bad-\\u12x4.txt",
                      "fcc97d063b145ddfd84b63626e98572409c96e466ec9f6782646777392e3c16f",
                      "invalid JSON Unicode escape", "non-hex Unicode escape digits must fail");
}

void test_deduplicated_payload_tamper_is_rejected() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    const auto update_plan = strict_plan('1', '2');
    FakePlanProvider provider(update_plan, events);
    StrictFixtureBackend backend;
    repository::StrictRuntimeRepositoryTreeValidator validator;
    const auto first =
        updater.update(provider, backend, validator, repository::ExpectedCurrent::absent());
    check_success(first, "dedup tamper baseline update must succeed");
    if (!first)
        return;

    const auto resource_object =
        state_root / "objects" / "resources" / update_plan.repositories[0].exact_commit;
    write_file(resource_object / "payload.bin", "tampered-payload");
    FakePlanProvider retry_provider(update_plan, events);
    const auto retry = updater.update(retry_provider, backend, validator,
                                      repository::ExpectedCurrent::exact(first.pinned_generation));
    check_error(retry, repository::RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                "deduplicated object payload tamper must fail seal comparison");
    const auto current = updater.pin_current();
    check(current && current->generation() == first.pinned_generation,
          "dedup tamper failure must preserve the current generation pin");
    check(directory_empty(state_root / "staging"),
          "dedup tamper failure must clean all staged candidates");
}

void test_installed_object_hard_link_is_rejected_before_reuse() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    const auto update_plan = strict_plan('1', '2');
    FakePlanProvider provider(update_plan, events);
    StrictFixtureBackend backend;
    repository::StrictRuntimeRepositoryTreeValidator validator;
    const auto first =
        updater.update(provider, backend, validator, repository::ExpectedCurrent::absent());
    check_success(first, "installed hard-link baseline update must succeed");
    if (!first)
        return;

    const auto installed_payload = state_root / "objects" / "resources" /
                                   update_plan.repositories[0].exact_commit / "payload.bin";
    const auto outside_alias = temporary.path() / "outside-installed-alias.bin";
    std::error_code link_error;
    std::filesystem::create_hard_link(installed_payload, outside_alias, link_error);
    check(!link_error, "installed-object hard-link fixture must be created");
    if (link_error)
        return;

    FakePlanProvider retry_provider(update_plan, events);
    const auto retry = updater.update(retry_provider, backend, validator,
                                      repository::ExpectedCurrent::exact(first.pinned_generation));
    check_error(retry, repository::RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                "an installed object with an external hard-link alias must not be reused");
    check(updater.pin_current() &&
              updater.pin_current()->generation() == first.pinned_generation,
          "hard-link rejection must preserve the previously pinned generation");
    check(directory_empty(state_root / "staging"),
          "hard-link rejection before reuse must not leave staging state");
}

void test_failed_installed_revalidation_does_not_poison_object_slot() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    const auto update_plan = plan('1', '2', 'a', 'b');
    FakePlanProvider provider(update_plan, events);
    FakeFetchBackend backend(events);
    RejectInstalledRevalidationValidator validator;
    const auto result =
        updater.update(provider, backend, validator, repository::ExpectedCurrent::absent());
    check_error(result, repository::RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                "an installed object that fails final revalidation must reject "
                "publication");
    check(!std::filesystem::exists(state_root / "objects" / "resources" /
                                   update_plan.repositories[0].exact_commit),
          "an updater-owned failed object must be quarantined from its immutable "
          "slot");
    check(directory_empty(state_root / "staging"),
          "failed installed-object revalidation must clean staging ownership");
}

void test_constructor_reports_corrupt_persisted_current() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    write_file(state_root / "current.json", "not-json");
    bool rejected{};
    try {
        repository::RuntimeRepositoryUpdater updater(state_root);
    } catch (...) {
        rejected = true;
    }
    check(rejected, "constructor must distinguish corrupt persisted current from an absent "
                    "generation");
}

void test_expected_current_conflicts_before_fetch() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog baseline_events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    const auto first_plan = plan('1', '2', 'a', 'b');
    FakePlanProvider baseline_provider(first_plan, baseline_events);
    FakeFetchBackend baseline_backend(baseline_events);
    FakeTreeValidator baseline_validator(baseline_events);
    const auto baseline = updater.update(baseline_provider, baseline_backend, baseline_validator,
                                         repository::ExpectedCurrent::absent());
    check_success(baseline, "ExpectedCurrent conflict baseline must succeed");
    if (!baseline)
        return;

    EventLog conflict_events;
    FakePlanProvider conflict_provider(plan('3', '4', 'c', 'd'), conflict_events);
    FakeFetchBackend conflict_backend(conflict_events);
    FakeTreeValidator conflict_validator(conflict_events);
    const auto wrong_exact =
        updater.update(conflict_provider, conflict_backend, conflict_validator,
                       repository::ExpectedCurrent::exact(std::string(64, 'f')));
    check_error(wrong_exact, repository::RuntimeRepositoryUpdateErrorCode::CurrentConflict,
                "wrong exact generation must report a current conflict");
    const auto absent = updater.update(conflict_provider, conflict_backend, conflict_validator,
                                       repository::ExpectedCurrent::absent());
    check_error(absent, repository::RuntimeRepositoryUpdateErrorCode::CurrentConflict,
                "absent expectation must conflict with a published current");
    const auto recorded = conflict_events.snapshot();
    check(first_event(recorded, "stage:resources") == recorded.size(),
          "ExpectedCurrent conflict must fail before backend execution");
    check(updater.pin_current() &&
              updater.pin_current()->generation() == baseline.pinned_generation,
          "ExpectedCurrent conflict must preserve the current pin");
}

void test_rollback_no_previous_and_redo() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    StrictFixtureBackend backend;
    repository::StrictRuntimeRepositoryTreeValidator validator;

    const auto first_plan = strict_plan('1', '2');
    const auto first_generation =
        repository::runtime_repository_generation(descriptors(first_plan));
    FakePlanProvider first_provider(first_plan, events);
    const auto first =
        updater.update(first_provider, backend, validator, repository::ExpectedCurrent::absent());
    check_success(first, "rollback baseline update must succeed");
    if (!first)
        return;
    const auto old_pin = first.pinned_bundle;

    const auto no_previous = updater.rollback(repository::ExpectedCurrent::exact(first_generation));
    check_error(no_previous, repository::RuntimeRepositoryUpdateErrorCode::NoPrevious,
                "one published generation must not invent a rollback target");

    const auto second_plan = strict_plan('3', '4');
    const auto second_generation =
        repository::runtime_repository_generation(descriptors(second_plan));
    FakePlanProvider second_provider(second_plan, events);
    const auto second = updater.update(second_provider, backend, validator,
                                       repository::ExpectedCurrent::exact(first_generation));
    check_success(second, "second rollback fixture update must succeed");
    if (!second)
        return;

    const auto rollback = updater.rollback(repository::ExpectedCurrent::exact(second_generation));
    check_success(rollback, "rollback to previous generation must succeed");
    check(rollback.pinned_generation == first_generation,
          "rollback must make previous the new current generation");
    check(old_pin && old_pin->generation() == first_generation,
          "rollback must not mutate a previously returned pin");

    const auto redo = updater.rollback(repository::ExpectedCurrent::exact(first_generation));
    check_success(redo, "a second rollback must redo the displaced generation");
    check(redo.pinned_generation == second_generation,
          "rollback must swap current and previous to provide redo");
}

void test_manifest_bound_payload_tamper_blocks_rollback() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    repository::RuntimeRepositoryUpdater updater(state_root);
    StrictFixtureBackend backend;
    repository::StrictRuntimeRepositoryTreeValidator validator;

    const auto first_plan = strict_plan('1', '2');
    const auto second_plan = strict_plan('3', '4');
    FakePlanProvider first_provider(first_plan, events);
    const auto first =
        updater.update(first_provider, backend, validator, repository::ExpectedCurrent::absent());
    check_success(first, "manifest-bound rollback baseline must succeed");
    if (!first)
        return;
    FakePlanProvider second_provider(second_plan, events);
    const auto second = updater.update(second_provider, backend, validator,
                                       repository::ExpectedCurrent::exact(first.pinned_generation));
    check_success(second, "manifest-bound rollback target must succeed");
    if (!second)
        return;

    write_file(state_root / "objects" / "resources" / first_plan.repositories[0].exact_commit /
                   "payload.bin",
               "tampered");
    const auto rollback =
        updater.rollback(repository::ExpectedCurrent::exact(second.pinned_generation));
    check_error(rollback, repository::RuntimeRepositoryUpdateErrorCode::ValidationFailed,
                "rollback must reject payload bytes not bound by the trusted "
                "manifest hash");
    check(updater.pin_current() && updater.pin_current()->generation() == second.pinned_generation,
          "rejected rollback must preserve the current generation");
}

void test_persisted_journal_phases_roll_forward_deterministically() {
    constexpr std::array phases{
        std::string_view{"prepared"},
        std::string_view{"previous_replaced"},
        std::string_view{"current_replaced"},
    };
    for (const auto phase : phases) {
        TempDirectory temporary;
        const auto state_root = temporary.path() / "runtime-repositories";
        EventLog events;
        const auto first_plan = strict_plan('1', '2');
        const auto second_plan = strict_plan('3', '4');
        const auto first_generation =
            repository::runtime_repository_generation(descriptors(first_plan));
        const auto second_generation =
            repository::runtime_repository_generation(descriptors(second_plan));
        StrictFixtureBackend backend;
        repository::StrictRuntimeRepositoryTreeValidator validator;
        {
            repository::RuntimeRepositoryUpdater updater(state_root);
            FakePlanProvider first_provider(first_plan, events);
            const auto first = updater.update(first_provider, backend, validator,
                                              repository::ExpectedCurrent::absent());
            check_success(first, "journal recovery first baseline update must succeed");
            if (!first)
                continue;
            FakePlanProvider second_provider(second_plan, events);
            const auto second =
                updater.update(second_provider, backend, validator,
                               repository::ExpectedCurrent::exact(first_generation));
            check_success(second, "journal recovery second baseline update must succeed");
            if (!second)
                continue;
        }

        if (phase == "prepared") {
            write_file(state_root / "previous.json", persisted_pointer(first_generation));
            write_file(state_root / "current.json", persisted_pointer(second_generation));
        } else if (phase == "previous_replaced") {
            write_file(state_root / "previous.json", persisted_pointer(second_generation));
            write_file(state_root / "current.json", persisted_pointer(second_generation));
        } else {
            write_file(state_root / "previous.json", persisted_pointer(second_generation));
            write_file(state_root / "current.json", persisted_pointer(first_generation));
        }
        write_file(state_root / ".publish-journal.json",
                   rollback_journal(phase, first_generation, second_generation));

        repository::RuntimeRepositoryUpdater recovering(state_root);
        FakePlanProvider retry_first(first_plan, events);
        const auto recovered = recovering.update(
            retry_first, backend, validator, repository::ExpectedCurrent::exact(first_generation));
        check_success(recovered, "persisted journal recovery trigger must succeed");
        check(recovered.pinned_generation == first_generation && recovered.pinned_bundle &&
                  recovered.pinned_bundle->generation() == first_generation,
              "all persisted journal phases must roll forward to new_current");
        check(read_file(state_root / "current.json") == persisted_pointer(first_generation),
              "journal recovery must durably publish new_current");
        check(read_file(state_root / "previous.json") == persisted_pointer(second_generation),
              "journal recovery must durably publish new_previous");
        check(!std::filesystem::exists(state_root / ".publish-journal.json"),
              "successful journal recovery must remove the persisted journal");
        const auto pin = recovering.pin_current();
        check(pin && pin->generation() == first_generation,
              "journal recovery must refresh the updater current pin");
    }
}

void test_same_generation_retry_preserves_real_previous() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    const auto first_plan = strict_plan('1', '2');
    const auto second_plan = strict_plan('3', '4');
    const auto first_generation =
        repository::runtime_repository_generation(descriptors(first_plan));
    const auto second_generation =
        repository::runtime_repository_generation(descriptors(second_plan));
    repository::RuntimeRepositoryUpdater updater(state_root);
    StrictFixtureBackend backend;
    repository::StrictRuntimeRepositoryTreeValidator validator;
    FakePlanProvider first_provider(first_plan, events);
    const auto first =
        updater.update(first_provider, backend, validator, repository::ExpectedCurrent::absent());
    check_success(first, "idempotent retry first baseline must succeed");
    if (!first)
        return;
    FakePlanProvider second_provider(second_plan, events);
    const auto second = updater.update(second_provider, backend, validator,
                                       repository::ExpectedCurrent::exact(first_generation));
    check_success(second, "idempotent retry second baseline must succeed");
    if (!second)
        return;

    const auto previous_before = read_file(state_root / "previous.json");
    FakePlanProvider retry_provider(second_plan, events);
    const auto retry = updater.update(retry_provider, backend, validator,
                                      repository::ExpectedCurrent::exact(second_generation));
    check_success(retry, "same-generation retry must be idempotently successful");
    check(retry.pinned_generation == second_generation,
          "same-generation retry must return the existing current pin");
    check(read_file(state_root / "current.json") == persisted_pointer(second_generation),
          "same-generation retry must retain current");
    check(read_file(state_root / "previous.json") == previous_before &&
              previous_before == persisted_pointer(first_generation),
          "same-generation retry must retain the real previous generation");
    check(!std::filesystem::exists(state_root / ".publish-journal.json"),
          "idempotent retry must not leave a publication journal");
}

void test_cancellation_from_current_replaced_observer_cannot_uncommit() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    auto hooks = std::make_shared<CancelOnCommittedHooks>();
    repository::RuntimeRepositoryUpdater updater(state_root, hooks);
    FakeFetchBackend backend(events);
    FakeTreeValidator validator(events);
    const auto first_plan = plan('1', '2', 'a', 'b');
    FakePlanProvider first_provider(first_plan, events);
    const auto first =
        updater.update(first_provider, backend, validator, repository::ExpectedCurrent::absent());
    check_success(first, "commit-observer cancellation baseline must succeed");
    if (!first)
        return;

    const auto second_plan = plan('3', '4', 'c', 'd');
    const auto second_generation =
        repository::runtime_repository_generation(descriptors(second_plan));
    FakePlanProvider second_provider(second_plan, events);
    std::stop_source stop;
    hooks->arm(stop);
    const auto second = updater.update(second_provider, backend, validator,
                                       repository::ExpectedCurrent::exact(first.pinned_generation),
                                       stop.get_token());
    check(hooks->observed() && stop.stop_requested(),
          "CurrentReplaced observer must request cancellation at the committed "
          "boundary");
    check(second.disposition != repository::PublishDisposition::NotCommitted,
          "cancellation requested after current replacement cannot report not "
          "committed");
    check_success(second, "post-current cancellation must retain committed success");
    check(second.disposition == repository::PublishDisposition::Committed &&
              second.pinned_generation == second_generation,
          "post-current cancellation must return the durable new generation");
    const auto current = updater.pin_current();
    check(current && current->generation() == second_generation,
          "post-current cancellation must leave the updater pinned to new current");
}

void test_file_operation_failure_matrix_respects_commit_boundary() {
    using Operation = repository::RuntimeRepositoryFileOperation;
    constexpr std::array pre_current{
        Operation::PreparedJournalReplace,
        Operation::PreviousPointerReplace,
        Operation::PreviousPhaseJournalReplace,
        Operation::CurrentPointerReplace,
    };
    constexpr std::array post_current{
        Operation::CurrentPhaseJournalReplace,
        Operation::JournalRemove,
    };

    for (const auto operation : pre_current) {
        TempDirectory temporary;
        const auto state_root = temporary.path() / "runtime-repositories";
        EventLog events;
        auto hooks = std::make_shared<FileOperationFaultHooks>();
        repository::RuntimeRepositoryUpdater updater(state_root, hooks);
        FakeFetchBackend backend(events);
        FakeTreeValidator validator(events);
        const auto first_plan = plan('1', '2', 'a', 'b');
        FakePlanProvider first_provider(first_plan, events);
        const auto first = updater.update(first_provider, backend, validator,
                                          repository::ExpectedCurrent::absent());
        check_success(first, "pre-current file-fault baseline must succeed");
        if (!first)
            continue;

        const auto second_plan = plan('3', '4', 'c', 'd');
        FakePlanProvider second_provider(second_plan, events);
        hooks->fail_at = operation;
        const auto failed =
            updater.update(second_provider, backend, validator,
                           repository::ExpectedCurrent::exact(first.pinned_generation));
        check(!failed && failed.disposition == repository::PublishDisposition::NotCommitted,
              "file-operation failure before current replacement must be not "
              "committed");
        check(read_file(state_root / "current.json") == persisted_pointer(first.pinned_generation),
              "pre-current file-operation failure must restore old current");
        check(!std::filesystem::exists(state_root / "previous.json") &&
                  !std::filesystem::exists(state_root / ".publish-journal.json"),
              "pre-current file-operation failure must restore previous and remove "
              "journal");
        const auto pin = updater.pin_current();
        check(pin && pin->generation() == first.pinned_generation,
              "pre-current file-operation failure must retain old current pin");
    }

    for (const auto operation : post_current) {
        TempDirectory temporary;
        const auto state_root = temporary.path() / "runtime-repositories";
        EventLog events;
        auto hooks = std::make_shared<FileOperationFaultHooks>();
        repository::RuntimeRepositoryUpdater updater(state_root, hooks);
        FakeFetchBackend backend(events);
        FakeTreeValidator validator(events);
        const auto first_plan = plan('1', '2', 'a', 'b');
        FakePlanProvider first_provider(first_plan, events);
        const auto first = updater.update(first_provider, backend, validator,
                                          repository::ExpectedCurrent::absent());
        check_success(first, "post-current file-fault baseline must succeed");
        if (!first)
            continue;

        const auto second_plan = plan('3', '4', 'c', 'd');
        const auto second_generation =
            repository::runtime_repository_generation(descriptors(second_plan));
        FakePlanProvider second_provider(second_plan, events);
        hooks->fail_at = operation;
        const auto failed =
            updater.update(second_provider, backend, validator,
                           repository::ExpectedCurrent::exact(first.pinned_generation));
        check(!failed && failed.error.has_value() &&
                  failed.disposition ==
                      repository::PublishDisposition::CommittedDurabilityUncertain,
              "post-current file-operation failure must report committed "
              "durability uncertain");
        check(read_file(state_root / "current.json") == persisted_pointer(second_generation),
              "post-current file-operation failure must retain new current");
        check(read_file(state_root / "previous.json") == persisted_pointer(first.pinned_generation),
              "post-current file-operation failure must retain the real previous "
              "pointer");
        check(std::filesystem::exists(state_root / ".publish-journal.json"),
              "post-current file-operation failure must retain journal for "
              "deterministic recovery");
        const auto pin = updater.pin_current();
        check(pin && pin->generation() == second_generation &&
                  failed.pinned_generation == second_generation,
              "post-current file-operation failure must expose a pin to new current");
    }
}

void test_cross_instance_writer_busy() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog first_events;
    EventLog second_events;
    repository::RuntimeRepositoryUpdater first_updater(state_root);
    repository::RuntimeRepositoryUpdater second_updater(state_root);
    FakePlanProvider first_provider(plan('1', '2', 'a', 'b'), first_events);
    FakePlanProvider second_provider(plan('3', '4', 'c', 'd'), second_events);
    BlockingFetchBackend blocking_backend;
    FakeFetchBackend second_backend(second_events);
    FakeTreeValidator first_validator(first_events);
    FakeTreeValidator second_validator(second_events);

    auto first_update = std::async(std::launch::async, [&] {
        return first_updater.update(first_provider, blocking_backend, first_validator,
                                    repository::ExpectedCurrent::absent());
    });
    const auto entered = blocking_backend.wait_until_entered();
    check(entered, "first updater must enter the blocking backend while holding "
                   "writer lock");
    if (!entered)
        blocking_backend.release();

    const auto busy = second_updater.update(second_provider, second_backend, second_validator,
                                            repository::ExpectedCurrent::absent());
    check_error(busy, repository::RuntimeRepositoryUpdateErrorCode::Busy,
                "a second updater instance must fail fast on the shared writer lock");
    check(first_event(second_events.snapshot(), "stage:resources") ==
              second_events.snapshot().size(),
          "busy writer result must occur before the second backend is invoked");

    blocking_backend.release();
    const auto first = first_update.get();
    check_success(first, "lock-owning updater must finish after the backend is released");
}

void test_concurrent_readers_observe_old_or_new_and_old_pin_survives() {
    TempDirectory temporary;
    const auto state_root = temporary.path() / "runtime-repositories";
    EventLog events;
    auto hooks = std::make_shared<BlockingCheckpointHooks>();
    repository::RuntimeRepositoryUpdater updater(state_root, hooks);
    FakeFetchBackend backend(events);
    FakeTreeValidator validator(events);
    const auto first_plan = plan('1', '2', 'a', 'b');
    FakePlanProvider first_provider(first_plan, events);
    const auto first =
        updater.update(first_provider, backend, validator, repository::ExpectedCurrent::absent());
    check_success(first, "concurrent reader baseline must succeed");
    if (!first)
        return;
    const auto old_pin = first.pinned_bundle;

    const auto second_plan = plan('3', '4', 'c', 'd');
    const auto second_generation =
        repository::runtime_repository_generation(descriptors(second_plan));
    FakePlanProvider second_provider(second_plan, events);
    hooks->arm();
    auto writer = std::async(std::launch::async, [&] {
        return updater.update(second_provider, backend, validator,
                              repository::ExpectedCurrent::exact(first.pinned_generation));
    });
    const auto entered = hooks->wait_until_entered();
    check(entered, "writer must reach the before-current replacement barrier");

    std::atomic<bool> stop{};
    std::atomic<unsigned int> old_observed{};
    std::atomic<unsigned int> new_observed{};
    std::atomic<unsigned int> unexpected{};
    auto reader = std::async(std::launch::async, [&] {
        while (!stop.load(std::memory_order_acquire)) {
            try {
                const auto snapshot = repository::RuntimeRepositorySnapshot::activate(state_root);
                if (snapshot->generation() == first.pinned_generation)
                    ++old_observed;
                else if (snapshot->generation() == second_generation)
                    ++new_observed;
                else
                    ++unexpected;
            } catch (...) {
                ++unexpected;
            }
        }
    });
    const auto old_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (old_observed.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < old_deadline)
        std::this_thread::yield();
    check(old_observed.load(std::memory_order_acquire) != 0,
          "reader must observe the old generation before current replacement");

    hooks->release();
    const auto second = writer.get();
    check_success(second, "barrier-released writer must publish the new generation");
    const auto new_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (new_observed.load(std::memory_order_acquire) == 0 &&
           std::chrono::steady_clock::now() < new_deadline)
        std::this_thread::yield();
    stop.store(true, std::memory_order_release);
    reader.get();
    check(new_observed.load(std::memory_order_acquire) != 0 && unexpected.load() == 0,
          "racing readers must observe only complete old or new generations");
    check(old_pin && old_pin->generation() == first.pinned_generation &&
              old_pin->resources().commit == std::string(40, '1'),
          "old reader pin must remain immutable after concurrent publication");
}

// This helper is intentionally checkpoint-oriented so journal and atomic-rename
// failure cases can be added as rows without duplicating setup and pin
// assertions.
void test_all_pre_current_faults_preserve_pinned_generation() {
    using Checkpoint = repository::RuntimeRepositoryUpdaterCheckpoint;
    constexpr std::array checkpoints{
        Checkpoint::PlanValidated,        Checkpoint::ResourcesStaged,
        Checkpoint::ScriptsStaged,        Checkpoint::CandidatesSealed,
        Checkpoint::ObjectsCommitted,     Checkpoint::SnapshotInstalled,
        Checkpoint::JournalPrepared,      Checkpoint::PreviousReplaced,
        Checkpoint::BeforeCurrentReplace,
    };
    for (const auto checkpoint : checkpoints) {
        TempDirectory temporary;
        const auto state_root = temporary.path() / "runtime-repositories";
        EventLog events;
        auto hooks = std::make_shared<FaultHooks>(events);
        repository::RuntimeRepositoryUpdater updater(state_root, hooks);
        FakeFetchBackend backend(events);
        FakeTreeValidator validator(events);

        const auto first_plan = plan('1', '2', 'a', 'b');
        FakePlanProvider first_provider(first_plan, events);
        const auto first = updater.update(first_provider, backend, validator,
                                          repository::ExpectedCurrent::absent());
        check_success(first, "fault-matrix baseline update must succeed");
        if (!first)
            continue;

        hooks->fail_at = checkpoint;
        const auto second_plan = plan('3', '4', 'c', 'd');
        FakePlanProvider second_provider(second_plan, events);
        const auto failed =
            updater.update(second_provider, backend, validator,
                           repository::ExpectedCurrent::exact(first.pinned_generation));
        check(!failed && failed.disposition == repository::PublishDisposition::NotCommitted,
              "every injected pre-current checkpoint failure must remain not "
              "committed");
        const auto still_current = updater.pin_current();
        check(still_current && still_current->generation() == first.pinned_generation,
              "every pre-current failure must preserve the prior pinned generation");
        check(!std::filesystem::exists(state_root / ".publish-journal.json"),
              "pre-current fault recovery must remove any prepared journal");
        check(directory_empty(state_root / "staging"),
              "pre-current fault recovery must clean staging directories");
    }
}

} // namespace

int main() {
    try {
        test_public_errors_are_stable_and_sensitive_diagnostics_stay_local();
        test_update_order_protocol_vector_and_pin();
        test_commit_oid_mismatch_fails_closed();
        test_cancelled_update_does_not_publish();
        test_second_repository_failure_cleans_all_staging();
        test_cancellation_after_first_stage_cleans_staging();
        test_path_escape_plan_is_rejected_before_fetch();
        test_parent_swap_between_enumeration_and_read_fails_closed();
        test_strict_validator_rejects_link_escape();
        test_strict_validator_rejects_staging_hard_links();
        test_strict_validator_enforces_file_and_byte_limits();
        test_manifest_limit_applies_before_hashing();
        test_many_empty_directories_fail_without_quadratic_scan();
        test_strict_manifest_portable_utf8_contract();
        test_json_unicode_escapes_match_standard_json_behavior();
        test_deduplicated_payload_tamper_is_rejected();
        test_installed_object_hard_link_is_rejected_before_reuse();
        test_failed_installed_revalidation_does_not_poison_object_slot();
        test_constructor_reports_corrupt_persisted_current();
        test_expected_current_conflicts_before_fetch();
        test_rollback_no_previous_and_redo();
        test_manifest_bound_payload_tamper_blocks_rollback();
        test_persisted_journal_phases_roll_forward_deterministically();
        test_same_generation_retry_preserves_real_previous();
        test_cancellation_from_current_replaced_observer_cannot_uncommit();
        test_file_operation_failure_matrix_respects_commit_boundary();
        test_cross_instance_writer_busy();
        test_concurrent_readers_observe_old_or_new_and_old_pin_survives();
        test_all_pre_current_faults_preserve_pinned_generation();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "runtime repository updater tests passed\n";
    return EXIT_SUCCESS;
}
