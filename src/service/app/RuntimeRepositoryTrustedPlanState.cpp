#include "service/app/RuntimeRepositoryTrustedPlanState.h"

#include "service/adapters/BoundedJson.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace baas::service::app {
namespace {

using Json = nlohmann::json;
constexpr std::string_view state_schema =
    "baas.runtime-repositories.trusted-plan-state/v1";
constexpr std::string_view journal_schema =
    "baas.runtime-repositories.trusted-plan-journal/v1";
constexpr std::string_view state_name = ".trusted-plan-state.json";
constexpr std::string_view journal_name = ".trusted-plan-journal.json";
constexpr std::string_view owner_name = ".trusted-plan-owner";
constexpr std::string_view owner_uninitialized =
    "baas.runtime-repositories.trusted-plan-owner/v1\nuninitialized\n";
constexpr std::string_view owner_initialized =
    "baas.runtime-repositories.trusted-plan-owner/v1\ninitialized\n";
constexpr std::size_t maximum_state_bytes = 32U * 1'024U;
std::atomic<std::uint64_t> temporary_sequence{};

class StoreFailure final : public std::runtime_error {
public:
  explicit StoreFailure(const RuntimeRepositoryTrustedPlanStateError error)
      : std::runtime_error("runtime repository trusted plan state failure"),
        error_(error) {}

  [[nodiscard]] RuntimeRepositoryTrustedPlanStateError error() const noexcept {
    return error_;
  }

private:
  RuntimeRepositoryTrustedPlanStateError error_;
};

[[noreturn]] void fail(const RuntimeRepositoryTrustedPlanStateError error) {
  throw StoreFailure{error};
}

[[nodiscard]] bool lower_hex(const std::string_view value,
                             const std::size_t size) noexcept {
  if (value.size() != size)
    return false;
  for (const char byte : value) {
    if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f')))
      return false;
  }
  return true;
}

[[nodiscard]] bool
valid_state(const RuntimeRepositoryTrustedState &value) noexcept {
  return lower_hex(value.generation, 64) && value.sequence != 0 &&
         lower_hex(value.payload_sha256, 64);
}

[[nodiscard]] std::string decimal(const std::uint64_t value) {
  return std::to_string(value);
}

[[nodiscard]] std::optional<std::uint64_t>
parse_decimal(const Json &value) noexcept {
  if (!value.is_string())
    return std::nullopt;
  const auto &text = value.get_ref<const std::string &>();
  if (text.empty() || (text.size() > 1 && text.front() == '0'))
    return std::nullopt;
  std::uint64_t result{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (error != std::errc{} || end != text.data() + text.size())
    return std::nullopt;
  return result;
}

[[nodiscard]] bool
exact_fields(const Json &value,
             const std::initializer_list<std::string_view> names) {
  if (!value.is_object() || value.size() != names.size())
    return false;
  for (const auto name : names) {
    if (!value.contains(std::string{name}))
      return false;
  }
  return true;
}

[[nodiscard]] Json state_json(const RuntimeRepositoryTrustedState &value) {
  return Json{{"schema", state_schema},
              {"generation", value.generation},
              {"sequence", decimal(value.sequence)},
              {"payload_sha256", value.payload_sha256}};
}

[[nodiscard]] std::optional<RuntimeRepositoryTrustedState>
parse_state(const Json &value) {
  if (!exact_fields(value,
                    {"schema", "generation", "sequence", "payload_sha256"}) ||
      !value.at("schema").is_string() ||
      value.at("schema").get_ref<const std::string &>() != state_schema ||
      !value.at("generation").is_string() ||
      !value.at("payload_sha256").is_string()) {
    return std::nullopt;
  }
  const auto sequence = parse_decimal(value.at("sequence"));
  RuntimeRepositoryTrustedState result{
      value.at("generation").get<std::string>(), sequence.value_or(0),
      value.at("payload_sha256").get<std::string>()};
  if (!sequence || !valid_state(result))
    return std::nullopt;
  return result;
}

struct Journal {
  std::optional<std::string> previous_generation;
  std::optional<RuntimeRepositoryTrustedState> previous_state;
  RuntimeRepositoryTrustedState next_state;
};

[[nodiscard]] std::string journal_json(const Journal &value) {
  Json previous_generation = nullptr;
  if (value.previous_generation)
    previous_generation = *value.previous_generation;
  Json previous_state = nullptr;
  if (value.previous_state)
    previous_state = state_json(*value.previous_state);
  return Json{{"schema", journal_schema},
              {"previous_generation", std::move(previous_generation)},
              {"previous_state", std::move(previous_state)},
              {"next_state", state_json(value.next_state)}}
      .dump();
}

[[nodiscard]] std::optional<Journal> parse_journal(const Json &value) {
  if (!exact_fields(value, {"schema", "previous_generation", "previous_state",
                            "next_state"}) ||
      !value.at("schema").is_string() ||
      value.at("schema").get_ref<const std::string &>() != journal_schema)
    return std::nullopt;

  std::optional<std::string> previous_generation;
  if (!value.at("previous_generation").is_null()) {
    if (!value.at("previous_generation").is_string())
      return std::nullopt;
    previous_generation = value.at("previous_generation").get<std::string>();
    if (!lower_hex(*previous_generation, 64))
      return std::nullopt;
  }

  std::optional<RuntimeRepositoryTrustedState> previous_state;
  if (!value.at("previous_state").is_null()) {
    previous_state = parse_state(value.at("previous_state"));
    if (!previous_state)
      return std::nullopt;
  }
  const auto next_state = parse_state(value.at("next_state"));
  if (!next_state ||
      (previous_state && (!previous_generation ||
                          previous_state->generation != *previous_generation)))
    return std::nullopt;
  return Journal{std::move(previous_generation), std::move(previous_state),
                 *next_state};
}

[[nodiscard]] std::filesystem::path child(const std::filesystem::path &root,
                                          const std::string_view name) {
  return root /
         std::filesystem::path{std::u8string{
             reinterpret_cast<const char8_t *>(name.data()), name.size()}};
}

void require_plain_root(const std::filesystem::path &root) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(root, error);
  if (error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status))
    fail(RuntimeRepositoryTrustedPlanStateError::invalid_root);
#ifdef _WIN32
  const auto attributes = GetFileAttributesW(root.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    fail(RuntimeRepositoryTrustedPlanStateError::invalid_root);
#endif
}

[[nodiscard]] bool present(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory ||
      status.type() == std::filesystem::file_type::not_found)
    return false;
  if (error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status))
    fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
  return true;
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
#ifdef _WIN32
  const auto handle = CreateFileW(
      path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  const auto close = [&] { CloseHandle(handle); };
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  FILE_STANDARD_INFO information{};
  if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                    sizeof(attributes)) ||
      !GetFileInformationByHandleEx(handle, FileStandardInfo, &information,
                                    sizeof(information)) ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      information.NumberOfLinks != 1 || information.EndOfFile.QuadPart < 0 ||
      static_cast<std::uint64_t>(information.EndOfFile.QuadPart) >
          maximum_state_bytes) {
    close();
    fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
  }
  std::string result(static_cast<std::size_t>(information.EndOfFile.QuadPart),
                     '\0');
  DWORD read{};
  if ((!result.empty() &&
       (!ReadFile(handle, result.data(), static_cast<DWORD>(result.size()),
                  &read, nullptr) ||
        read != result.size()))) {
    close();
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  }
  close();
  return result;
#else
  const auto descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0)
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  struct stat information{};
  if (fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
      information.st_nlink != 1 || information.st_size < 0 ||
      static_cast<std::uint64_t>(information.st_size) > maximum_state_bytes) {
    close(descriptor);
    fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
  }
  std::string result(static_cast<std::size_t>(information.st_size), '\0');
  std::size_t offset{};
  while (offset < result.size()) {
    const auto count =
        read(descriptor, result.data() + offset, result.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      close(descriptor);
      fail(RuntimeRepositoryTrustedPlanStateError::io);
    }
    offset += static_cast<std::size_t>(count);
  }
  close(descriptor);
  return result;
#endif
}

void sync_directory(const std::filesystem::path &root) {
#ifndef _WIN32
  const auto descriptor =
      open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0 || fsync(descriptor) != 0) {
    if (descriptor >= 0)
      close(descriptor);
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  }
  close(descriptor);
#else
  static_cast<void>(root);
#endif
}

void replace_file(const std::filesystem::path &root,
                  const std::string_view name, const std::string_view bytes) {
  if (bytes.size() > maximum_state_bytes)
    fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
  const auto destination = child(root, name);
  const auto nonce = temporary_sequence.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
  const auto temporary = destination.wstring() + L".tmp-" +
                         std::to_wstring(GetCurrentProcessId()) + L"-" +
                         std::to_wstring(nonce);
  const auto handle =
      CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  DWORD written{};
  const bool okay =
      (bytes.empty() ||
       (WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()),
                  &written, nullptr) &&
        written == bytes.size())) &&
      FlushFileBuffers(handle);
  CloseHandle(handle);
  if (!okay ||
      !MoveFileExW(temporary.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary.c_str());
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  }
#else
  const auto temporary =
      destination.string() + ".tmp-" +
      std::to_string(static_cast<unsigned long long>(getpid())) + "-" +
      std::to_string(nonce);
  const auto descriptor =
      open(temporary.c_str(),
           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (descriptor < 0)
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  std::size_t offset{};
  while (offset < bytes.size()) {
    const auto count =
        write(descriptor, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0) {
      close(descriptor);
      unlink(temporary.c_str());
      fail(RuntimeRepositoryTrustedPlanStateError::io);
    }
    offset += static_cast<std::size_t>(count);
  }
  const bool sync_failed = fsync(descriptor) != 0;
  const bool close_failed = close(descriptor) != 0;
  if (sync_failed || close_failed ||
      rename(temporary.c_str(), destination.c_str()) != 0) {
    unlink(temporary.c_str());
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  }
  sync_directory(root);
#endif
}

void remove_file(const std::filesystem::path &root,
                 const std::string_view name) {
  const auto path = child(root, name);
#ifdef _WIN32
  if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
    fail(RuntimeRepositoryTrustedPlanStateError::io);
#else
  if (unlink(path.c_str()) != 0 && errno != ENOENT)
    fail(RuntimeRepositoryTrustedPlanStateError::io);
  sync_directory(root);
#endif
}

enum class OwnerPhase { uninitialized, initialized };

[[nodiscard]] OwnerPhase ensure_owner(const std::filesystem::path &root) {
  const auto path = child(root, owner_name);
  if (!present(path))
    replace_file(root, owner_name, owner_uninitialized);
  const auto contents = read_file(path);
  if (contents == owner_uninitialized)
    return OwnerPhase::uninitialized;
  if (contents == owner_initialized)
    return OwnerPhase::initialized;
  fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
}

void mark_owner_initialized(const std::filesystem::path &root) {
  replace_file(root, owner_name, owner_initialized);
}

[[nodiscard]] std::optional<Json> read_json(const std::filesystem::path &root,
                                            const std::string_view name) {
  const auto path = child(root, name);
  if (!present(path))
    return std::nullopt;
  const auto text = read_file(path);
  const auto parsed =
      adapters::bounded_json::parse_json(text, {maximum_state_bytes, 8, 64});
  if (!parsed)
    fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
  return parsed;
}

[[nodiscard]] bool
same_generation(const std::optional<std::string_view> left,
                const std::optional<std::string> &right) noexcept {
  return (!left && !right) ||
         (left && right && *left == std::string_view{*right});
}

} // namespace

struct RuntimeRepositoryTrustedPlanStateStore::Impl final {
  explicit Impl(std::filesystem::path requested_root)
      : root(std::move(requested_root)) {}

  [[nodiscard]] std::optional<RuntimeRepositoryTrustedState>
  read_current() const {
    const auto value = read_json(root, state_name);
    if (!value)
      return std::nullopt;
    const auto parsed = parse_state(*value);
    if (!parsed)
      fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
    return parsed;
  }

  [[nodiscard]] std::optional<Journal> read_journal() const {
    const auto value = read_json(root, journal_name);
    if (!value)
      return std::nullopt;
    const auto parsed = parse_journal(*value);
    if (!parsed)
      fail(RuntimeRepositoryTrustedPlanStateError::invalid_state);
    return parsed;
  }

  std::filesystem::path root;
  mutable std::mutex mutex;
  std::optional<RuntimeRepositoryTrustedState> current;
  bool is_ready{};
};

RuntimeRepositoryTrustedPlanStateStore::RuntimeRepositoryTrustedPlanStateStore(
    std::filesystem::path runtime_repository_state_root)
    : impl_(std::make_unique<Impl>(std::move(runtime_repository_state_root))) {}

RuntimeRepositoryTrustedPlanStateStore::
    ~RuntimeRepositoryTrustedPlanStateStore() = default;

std::optional<RuntimeRepositoryTrustedState>
RuntimeRepositoryTrustedPlanStateStore::trusted_state() const {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->is_ready)
    fail(RuntimeRepositoryTrustedPlanStateError::not_ready);
  return impl_->current;
}

RuntimeRepositoryTrustedPlanStateResult
RuntimeRepositoryTrustedPlanStateStore::claim_ownership() noexcept {
  try {
    std::scoped_lock lock(impl_->mutex);
    require_plain_root(impl_->root);
    static_cast<void>(ensure_owner(impl_->root));
    return {RuntimeRepositoryTrustedPlanStateError::none};
  } catch (const StoreFailure &error) {
    return {error.error()};
  } catch (const std::bad_alloc &) {
    return {RuntimeRepositoryTrustedPlanStateError::resource_exhausted};
  } catch (...) {
    return {RuntimeRepositoryTrustedPlanStateError::internal_error};
  }
}

RuntimeRepositoryTrustedPlanStateResult
RuntimeRepositoryTrustedPlanStateStore::reconcile(
    const std::optional<std::string_view> actual_generation) noexcept {
  try {
    std::scoped_lock lock(impl_->mutex);
    impl_->is_ready = false;
    require_plain_root(impl_->root);
    const auto owner_phase = ensure_owner(impl_->root);
    auto current = impl_->read_current();
    const auto journal = impl_->read_journal();
    if (journal) {
      if (actual_generation &&
          *actual_generation == journal->next_state.generation) {
        if (current && *current != journal->next_state &&
            (!journal->previous_state ||
             *current != *journal->previous_state)) {
          return {
              RuntimeRepositoryTrustedPlanStateError::inconsistent_generation};
        }
        replace_file(impl_->root, state_name,
                     state_json(journal->next_state).dump());
        mark_owner_initialized(impl_->root);
        current = journal->next_state;
        remove_file(impl_->root, journal_name);
      } else if (same_generation(actual_generation,
                                 journal->previous_generation) &&
                 current == journal->previous_state) {
        remove_file(impl_->root, journal_name);
      } else {
        return {
            RuntimeRepositoryTrustedPlanStateError::inconsistent_generation};
      }
    }
    if (current &&
        (!actual_generation || current->generation != *actual_generation)) {
      return {RuntimeRepositoryTrustedPlanStateError::inconsistent_generation};
    }
    if (owner_phase == OwnerPhase::initialized && !current)
      return {RuntimeRepositoryTrustedPlanStateError::invalid_state};
    if (current && owner_phase == OwnerPhase::uninitialized)
      mark_owner_initialized(impl_->root);
    impl_->current = std::move(current);
    impl_->is_ready = true;
    return {RuntimeRepositoryTrustedPlanStateError::none};
  } catch (const StoreFailure &error) {
    return {error.error()};
  } catch (const std::bad_alloc &) {
    return {RuntimeRepositoryTrustedPlanStateError::resource_exhausted};
  } catch (...) {
    return {RuntimeRepositoryTrustedPlanStateError::internal_error};
  }
}

RuntimeRepositoryTrustedPlanStateResult
RuntimeRepositoryTrustedPlanStateStore::prepare(
    const std::optional<std::string_view> expected_previous_generation,
    const RuntimeRepositoryTrustedState &next_state) noexcept {
  try {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->is_ready)
      return {RuntimeRepositoryTrustedPlanStateError::not_ready};
    if (!valid_state(next_state) ||
        (expected_previous_generation &&
         !lower_hex(*expected_previous_generation, 64)) ||
        (impl_->current &&
         (!expected_previous_generation ||
          impl_->current->generation != *expected_previous_generation ||
          next_state.sequence <= impl_->current->sequence)) ||
        (expected_previous_generation &&
         next_state.generation == *expected_previous_generation)) {
      return {RuntimeRepositoryTrustedPlanStateError::invalid_state};
    }
    Journal journal;
    if (expected_previous_generation)
      journal.previous_generation = std::string{*expected_previous_generation};
    journal.previous_state = impl_->current;
    journal.next_state = next_state;
    replace_file(impl_->root, journal_name, journal_json(journal));
    return {RuntimeRepositoryTrustedPlanStateError::none};
  } catch (const StoreFailure &error) {
    return {error.error()};
  } catch (const std::bad_alloc &) {
    return {RuntimeRepositoryTrustedPlanStateError::resource_exhausted};
  } catch (...) {
    return {RuntimeRepositoryTrustedPlanStateError::internal_error};
  }
}

RuntimeRepositoryTrustedPlanStateResult
RuntimeRepositoryTrustedPlanStateStore::commit(
    const RuntimeRepositoryTrustedState &next_state) noexcept {
  try {
    std::scoped_lock lock(impl_->mutex);
    if (!impl_->is_ready)
      return {RuntimeRepositoryTrustedPlanStateError::not_ready};
    if (!valid_state(next_state) ||
        (impl_->current && (next_state.sequence < impl_->current->sequence ||
                            (next_state.sequence == impl_->current->sequence &&
                             next_state != *impl_->current)))) {
      return {RuntimeRepositoryTrustedPlanStateError::invalid_state};
    }
    const auto journal = impl_->read_journal();
    if (journal && journal->next_state != next_state)
      return {RuntimeRepositoryTrustedPlanStateError::invalid_state};
    replace_file(impl_->root, state_name, state_json(next_state).dump());
    mark_owner_initialized(impl_->root);
    impl_->current = next_state;
    if (journal)
      remove_file(impl_->root, journal_name);
    return {RuntimeRepositoryTrustedPlanStateError::none};
  } catch (const StoreFailure &error) {
    return {error.error()};
  } catch (const std::bad_alloc &) {
    return {RuntimeRepositoryTrustedPlanStateError::resource_exhausted};
  } catch (...) {
    return {RuntimeRepositoryTrustedPlanStateError::internal_error};
  }
}

bool RuntimeRepositoryTrustedPlanStateStore::ready() const noexcept {
  std::scoped_lock lock(impl_->mutex);
  return impl_->is_ready;
}

const std::filesystem::path &
RuntimeRepositoryTrustedPlanStateStore::state_root() const noexcept {
  return impl_->root;
}

std::string_view runtime_repository_trusted_plan_state_error_name(
    const RuntimeRepositoryTrustedPlanStateError error) noexcept {
  using enum RuntimeRepositoryTrustedPlanStateError;
  switch (error) {
  case none:
    return "none";
  case not_ready:
    return "not_ready";
  case invalid_root:
    return "invalid_root";
  case invalid_state:
    return "invalid_state";
  case inconsistent_generation:
    return "inconsistent_generation";
  case io:
    return "io";
  case resource_exhausted:
    return "resource_exhausted";
  case internal_error:
    return "internal_error";
  }
  return "internal_error";
}

} // namespace baas::service::app
