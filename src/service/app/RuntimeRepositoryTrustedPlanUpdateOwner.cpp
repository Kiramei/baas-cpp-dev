#include "service/app/RuntimeRepositoryTrustedPlanUpdateOwner.h"

#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace baas::service::app {
namespace {

using runtime::repository::ExpectedCurrent;
using runtime::repository::PublishDisposition;
using runtime::repository::RuntimeRepositoryCommitClaim;
using runtime::repository::RuntimeRepositoryRecoveryPolicy;
using runtime::repository::RuntimeRepositoryUpdateResult;

class PolicyWriterLock final {
public:
  explicit PolicyWriterLock(const std::filesystem::path &root) {
    const auto path = root / ".trusted-plan-writer.lock";
#ifdef _WIN32
    handle_ = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
      throw std::runtime_error("trusted plan writer lock open failed");

    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!GetFileInformationByHandleEx(handle_, FileAttributeTagInfo,
                                      &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
      throw std::runtime_error("trusted plan writer lock is not a plain file");
    }

    if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped_)) {
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
      throw std::runtime_error("trusted plan writer lock failed");
    }

    BY_HANDLE_FILE_INFORMATION locked_information{};
    if (!GetFileInformationByHandle(handle_, &locked_information)) {
      release();
      throw std::runtime_error("trusted plan writer lock identity failed");
    }
    const auto reopened = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION path_information{};
    const auto same_identity =
        reopened != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(reopened, &path_information) &&
        locked_information.dwVolumeSerialNumber ==
            path_information.dwVolumeSerialNumber &&
        locked_information.nFileIndexHigh == path_information.nFileIndexHigh &&
        locked_information.nFileIndexLow == path_information.nFileIndexLow;
    if (reopened != INVALID_HANDLE_VALUE)
      CloseHandle(reopened);
    if (!same_identity) {
      release();
      throw std::runtime_error(
          "trusted plan writer lock path changed while locked");
    }
#else
    descriptor_ =
        open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor_ < 0)
      throw std::runtime_error("trusted plan writer lock open failed");
    if (flock(descriptor_, LOCK_EX) != 0) {
      close(descriptor_);
      descriptor_ = -1;
      throw std::runtime_error("trusted plan writer lock failed");
    }
    struct stat locked_information{};
    struct stat path_information{};
    if (fstat(descriptor_, &locked_information) != 0 ||
        lstat(path.c_str(), &path_information) != 0 ||
        !S_ISREG(locked_information.st_mode) ||
        !S_ISREG(path_information.st_mode) ||
        locked_information.st_dev != path_information.st_dev ||
        locked_information.st_ino != path_information.st_ino) {
      release();
      throw std::runtime_error(
          "trusted plan writer lock path changed while locked");
    }
#endif
  }

  ~PolicyWriterLock() { release(); }

  PolicyWriterLock(const PolicyWriterLock &) = delete;
  PolicyWriterLock &operator=(const PolicyWriterLock &) = delete;

private:
  void release() noexcept {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
      UnlockFileEx(handle_, 0, 1, 0, &overlapped_);
      CloseHandle(handle_);
      handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (descriptor_ >= 0) {
      flock(descriptor_, LOCK_UN);
      close(descriptor_);
      descriptor_ = -1;
    }
#endif
  }
#ifdef _WIN32
  HANDLE handle_{INVALID_HANDLE_VALUE};
  OVERLAPPED overlapped_{};
#else
  int descriptor_{-1};
#endif
};

[[nodiscard]] std::optional<std::string>
current_generation(runtime::repository::RuntimeRepositoryUpdater &updater) {
  const auto current = updater.pin_current();
  if (!current)
    return std::nullopt;
  return current->generation();
}

[[nodiscard]] RuntimeRepositoryTrustedPlanUpdateOwnerResult
fail(const RuntimeRepositoryTrustedPlanUpdateOwnerError error,
     RuntimeRepositoryUpdateResult update = {},
     const RuntimeRepositoryTrustedPlanError plan_error =
         RuntimeRepositoryTrustedPlanError::none,
     const RuntimeRepositoryTrustedPlanStateError state_error =
         RuntimeRepositoryTrustedPlanStateError::none) {
  return {std::move(update), error, plan_error, state_error};
}

class PolicyCommitClaim final : public RuntimeRepositoryCommitClaim {
public:
  PolicyCommitClaim(
      std::shared_ptr<RuntimeRepositoryTrustedPlanStateStore> store,
      std::optional<std::string> previous_generation,
      RuntimeRepositoryTrustedState next_state,
      RuntimeRepositoryCommitClaim *terminal_claim)
      : store_(std::move(store)),
        previous_generation_(std::move(previous_generation)),
        next_state_(std::move(next_state)), terminal_claim_(terminal_claim) {}

  [[nodiscard]] bool
  claim(const std::string_view target_generation) noexcept override {
    attempted_ = true;
    if (target_generation != next_state_.generation) {
      error_ = RuntimeRepositoryTrustedPlanStateError::invalid_state;
      return false;
    }
    const auto previous =
        previous_generation_
            ? std::optional<std::string_view>{*previous_generation_}
            : std::nullopt;
    const auto result = store_->prepare(previous, next_state_);
    error_ = result.error;
    prepared_ = static_cast<bool>(result);
    if (!prepared_)
      return false;
    if (terminal_claim_ && !terminal_claim_->claim(target_generation)) {
      terminal_rejected_ = true;
      return false;
    }
    return true;
  }

  [[nodiscard]] bool attempted() const noexcept { return attempted_; }
  [[nodiscard]] bool prepared() const noexcept { return prepared_; }
  [[nodiscard]] RuntimeRepositoryTrustedPlanStateError error() const noexcept {
    return error_;
  }
  [[nodiscard]] bool terminal_rejected() const noexcept {
    return terminal_rejected_;
  }

private:
  std::shared_ptr<RuntimeRepositoryTrustedPlanStateStore> store_;
  std::optional<std::string> previous_generation_;
  RuntimeRepositoryTrustedState next_state_;
  RuntimeRepositoryCommitClaim *terminal_claim_{};
  RuntimeRepositoryTrustedPlanStateError error_{
      RuntimeRepositoryTrustedPlanStateError::none};
  bool attempted_{};
  bool prepared_{};
  bool terminal_rejected_{};
};

} // namespace

struct RuntimeRepositoryTrustedPlanUpdateOwner::Impl final {
  Impl(
      std::filesystem::path state_root,
      const std::span<const std::byte> public_key,
      std::shared_ptr<runtime::repository::RuntimeRepositoryUpdaterHooks> hooks)
      : updater(std::move(state_root), std::move(hooks)),
        state_store(std::make_shared<RuntimeRepositoryTrustedPlanStateStore>(
            updater.state_root())),
        verifier(public_key, state_store) {}

  runtime::repository::RuntimeRepositoryUpdater updater;
  std::shared_ptr<RuntimeRepositoryTrustedPlanStateStore> state_store;
  RuntimeRepositoryTrustedPlanVerifier verifier;
  mutable std::mutex mutex;
  bool is_recovered{};
};

RuntimeRepositoryTrustedPlanUpdateOwner::
    RuntimeRepositoryTrustedPlanUpdateOwner(
        std::filesystem::path runtime_repository_state_root,
        const std::span<const std::byte> trusted_public_key,
        std::shared_ptr<runtime::repository::RuntimeRepositoryUpdaterHooks>
            hooks)
    : impl_(std::make_unique<Impl>(std::move(runtime_repository_state_root),
                                   trusted_public_key, std::move(hooks))) {}

RuntimeRepositoryTrustedPlanUpdateOwner::
    ~RuntimeRepositoryTrustedPlanUpdateOwner() = default;

RuntimeRepositoryTrustedPlanUpdateOwnerResult
RuntimeRepositoryTrustedPlanUpdateOwner::recover(
    const runtime::repository::RuntimeRepositoryTreeValidator
        &validator) noexcept {
  try {
    std::scoped_lock lock(impl_->mutex);
    impl_->is_recovered = false;
    PolicyWriterLock policy_lock{impl_->updater.state_root()};
    const auto ownership = impl_->state_store->claim_ownership();
    if (!ownership)
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                  {}, RuntimeRepositoryTrustedPlanError::none, ownership.error);
    auto update = impl_->updater.recover(validator);
    if (!update)
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::updater_failure,
                  std::move(update));
    const auto actual = current_generation(impl_->updater);
    const auto actual_view =
        actual ? std::optional<std::string_view>{*actual} : std::nullopt;
    const auto reconciled = impl_->state_store->reconcile(actual_view);
    if (!reconciled)
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                  std::move(update), RuntimeRepositoryTrustedPlanError::none,
                  reconciled.error);
    impl_->is_recovered = true;
    return {std::move(update),
            RuntimeRepositoryTrustedPlanUpdateOwnerError::none,
            RuntimeRepositoryTrustedPlanError::none,
            RuntimeRepositoryTrustedPlanStateError::none};
  } catch (const std::bad_alloc &) {
    return fail(
        RuntimeRepositoryTrustedPlanUpdateOwnerError::resource_exhausted);
  } catch (...) {
    return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::internal_error);
  }
}

RuntimeRepositoryTrustedPlanUpdateOwnerResult
RuntimeRepositoryTrustedPlanUpdateOwner::apply(
    const std::string_view signed_envelope,
    runtime::repository::RuntimeRepositoryFetchBackend &fetch_backend,
    const runtime::repository::RuntimeRepositoryTreeValidator &validator,
    const std::stop_token stop_token,
    RuntimeRepositoryCommitClaim *const terminal_commit_claim) noexcept {
  try {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->is_recovered)
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::not_recovered);
    PolicyWriterLock policy_lock{impl_->updater.state_root()};
    const auto ownership = impl_->state_store->claim_ownership();
    if (!ownership) {
      impl_->is_recovered = false;
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                  {}, RuntimeRepositoryTrustedPlanError::none, ownership.error);
    }
    auto refreshed = impl_->updater.recover(validator);
    if (!refreshed) {
      impl_->is_recovered = false;
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::updater_failure,
                  std::move(refreshed));
    }

    const auto previous_generation = current_generation(impl_->updater);
    const auto previous_view =
        previous_generation
            ? std::optional<std::string_view>{*previous_generation}
            : std::nullopt;
    const auto consistent = impl_->state_store->reconcile(previous_view);
    if (!consistent) {
      impl_->is_recovered = false;
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                  {}, RuntimeRepositoryTrustedPlanError::none,
                  consistent.error);
    }
    if (refreshed.disposition == PublishDisposition::Committed) {
      impl_->is_recovered = false;
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::not_recovered,
                  std::move(refreshed));
    }

    const auto verified = impl_->verifier.verify(signed_envelope);
    if (!verified)
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::invalid_plan,
                  {}, verified.error);

    const RuntimeRepositoryTrustedState next_state{
        verified.plan->target_generation(), verified.plan->sequence(),
        verified.plan->payload_sha256()};
    const auto accepted_state = impl_->state_store->trusted_state();
    if (!accepted_state && previous_generation &&
        next_state.generation != *previous_generation) {
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::invalid_plan,
                  {}, RuntimeRepositoryTrustedPlanError::bootstrap_rejected);
    }

    PolicyCommitClaim claim{impl_->state_store, previous_generation, next_state,
                            terminal_commit_claim};
    const auto expected = previous_generation
                              ? ExpectedCurrent::exact(*previous_generation)
                              : ExpectedCurrent::absent();
    auto update = impl_->updater.update(
        *verified.plan, fetch_backend, validator, expected, stop_token, &claim,
        RuntimeRepositoryRecoveryPolicy::RequireClean);

    if (!update) {
      if (claim.attempted() && !claim.prepared()) {
        return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                    std::move(update), RuntimeRepositoryTrustedPlanError::none,
                    claim.error());
      }
      if (claim.prepared()) {
        const auto recovered = impl_->updater.recover(validator);
        if (!recovered) {
          impl_->is_recovered = false;
          return fail(
              RuntimeRepositoryTrustedPlanUpdateOwnerError::updater_failure,
              std::move(update));
        }
        const auto actual = current_generation(impl_->updater);
        const auto actual_view =
            actual ? std::optional<std::string_view>{*actual} : std::nullopt;
        const auto reconciled = impl_->state_store->reconcile(actual_view);
        if (!reconciled) {
          impl_->is_recovered = false;
          return fail(
              RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
              std::move(update), RuntimeRepositoryTrustedPlanError::none,
              reconciled.error);
        }
      }
      if (claim.terminal_rejected())
        return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::cancelled,
                    std::move(update));
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::updater_failure,
                  std::move(update));
    }

    const bool same_generation =
        previous_generation && *previous_generation == next_state.generation;
    if (same_generation && claim.attempted()) {
      impl_->is_recovered = false;
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                  std::move(update), RuntimeRepositoryTrustedPlanError::none,
                  RuntimeRepositoryTrustedPlanStateError::invalid_state);
    }
    if (same_generation && terminal_commit_claim &&
        !terminal_commit_claim->claim(next_state.generation)) {
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::cancelled,
                  std::move(update));
    }
    if (!same_generation && !claim.prepared()) {
      impl_->is_recovered = false;
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                  std::move(update), RuntimeRepositoryTrustedPlanError::none,
                  claim.error());
    }
    const auto policy_commit = impl_->state_store->commit(next_state);
    if (!policy_commit) {
      impl_->is_recovered = false;
      return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::state_failure,
                  std::move(update), RuntimeRepositoryTrustedPlanError::none,
                  policy_commit.error);
    }
    return {std::move(update),
            RuntimeRepositoryTrustedPlanUpdateOwnerError::none,
            RuntimeRepositoryTrustedPlanError::none,
            RuntimeRepositoryTrustedPlanStateError::none};
  } catch (const std::bad_alloc &) {
    impl_->is_recovered = false;
    return fail(
        RuntimeRepositoryTrustedPlanUpdateOwnerError::resource_exhausted);
  } catch (...) {
    impl_->is_recovered = false;
    return fail(RuntimeRepositoryTrustedPlanUpdateOwnerError::internal_error);
  }
}

bool RuntimeRepositoryTrustedPlanUpdateOwner::recovered() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->is_recovered;
}

std::string_view runtime_repository_trusted_plan_update_owner_error_name(
    const RuntimeRepositoryTrustedPlanUpdateOwnerError error) noexcept {
  using enum RuntimeRepositoryTrustedPlanUpdateOwnerError;
  switch (error) {
  case none:
    return "none";
  case not_recovered:
    return "not_recovered";
  case invalid_plan:
    return "invalid_plan";
  case cancelled:
    return "cancelled";
  case state_failure:
    return "state_failure";
  case updater_failure:
    return "updater_failure";
  case resource_exhausted:
    return "resource_exhausted";
  case internal_error:
    return "internal_error";
  }
  return "internal_error";
}

} // namespace baas::service::app
