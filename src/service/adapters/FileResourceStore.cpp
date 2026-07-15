#include "service/adapters/FileResourceStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace baas::service::adapters {
namespace {

[[nodiscard]] bool is_valid_utf8(const std::string_view input) noexcept
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
    std::size_t index = 0;
    while (index < input.size()) {
        const auto lead = bytes[index++];
        if (lead <= 0x7fU) continue;

        std::size_t trailing{};
        std::uint32_t code_point{};
        std::uint32_t minimum{};
        if (lead >= 0xc2U && lead <= 0xdfU) {
            trailing = 1;
            code_point = lead & 0x1fU;
            minimum = 0x80U;
        } else if (lead >= 0xe0U && lead <= 0xefU) {
            trailing = 2;
            code_point = lead & 0x0fU;
            minimum = 0x800U;
        } else if (lead >= 0xf0U && lead <= 0xf4U) {
            trailing = 3;
            code_point = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (trailing > input.size() - index) return false;
        for (std::size_t offset = 0; offset < trailing; ++offset) {
            const auto byte = bytes[index++];
            if ((byte & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (byte & 0x3fU);
        }
        if (code_point < minimum || code_point > 0x10ffffU
            || (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
    }
    return true;
}

using Json = nlohmann::json;
using channels::ResourceKey;
using channels::ResourcePatchOperation;
using channels::ResourceSnapshot;
using channels::ResourceStoreError;
using channels::SyncResource;

struct JsonBounds {
    std::size_t bytes;
    std::size_t depth;
    std::size_t nodes;
};

struct ResourceKeyHash {
    std::size_t operator()(const ResourceKey& key) const noexcept
    {
        auto result = static_cast<std::size_t>(key.resource) * 0x9e3779b9U;
        if (key.resource_id) result ^= std::hash<std::string>{}(*key.resource_id);
        return result;
    }
};

bool bounded_tree(const Json& value, const std::size_t maximum_depth,
                  const std::size_t maximum_nodes, const std::size_t depth,
                  std::size_t& nodes)
{
    if (++nodes > maximum_nodes || depth > maximum_depth) return false;
    if (value.is_array()) {
        for (const auto& child : value) {
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) {
                return false;
            }
        }
    } else if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            static_cast<void>(key);
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<Json> parse_json(const std::string_view text, const JsonBounds bounds)
{
    if (text.size() > bounds.bytes || !is_valid_utf8(text)) return std::nullopt;
    try {
        bool duplicate{};
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto callback = [&duplicate, &object_keys](
                                  int, const Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) {
                object_keys.emplace_back();
            } else if (event == Json::parse_event_t::key && !object_keys.empty()) {
                if (!object_keys.back().insert(parsed.get<std::string>()).second) {
                    duplicate = true;
                }
            } else if (event == Json::parse_event_t::object_end && !object_keys.empty()) {
                object_keys.pop_back();
            }
            return !duplicate;
        };
        Json value = Json::parse(text, callback, false);
        if (duplicate || value.is_discarded()) return std::nullopt;
        std::size_t nodes{};
        if (!bounded_tree(value, bounds.depth, bounds.nodes, 0, nodes)) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> timestamp_value(const Json& value)
{
    if (!value.is_number()) return std::nullopt;
    const auto result = value.get<double>();
    return std::isfinite(result) ? std::optional{result} : std::nullopt;
}

std::optional<double> timestamp_value(
    const std::string_view value, const JsonBounds bounds)
{
    const auto parsed = parse_json(value, bounds);
    return parsed ? timestamp_value(*parsed) : std::nullopt;
}

std::string timestamp_json(const double value) { return Json(value).dump(); }

std::optional<std::vector<std::string>> split_pointer(const std::string& path)
{
    if (path.empty() || path == "/") return std::vector<std::string>{};
    if (path.front() != '/') return std::nullopt;
    std::vector<std::string> result;
    std::size_t begin = 1;
    while (true) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(begin, end - begin);
        std::string decoded;
        for (std::size_t index = 0; index < component.size(); ++index) {
            if (component[index] != '~') {
                decoded.push_back(component[index]);
                continue;
            }
            if (++index >= component.size()) return std::nullopt;
            if (component[index] == '0') decoded.push_back('~');
            else if (component[index] == '1') decoded.push_back('/');
            else return std::nullopt;
        }
        result.push_back(std::move(decoded));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

std::optional<std::size_t> array_index(const std::string& value)
{
    if (value.empty()
        || !std::all_of(value.begin(), value.end(), [](const char character) {
               return character >= '0' && character <= '9';
           })) {
        return std::nullopt;
    }
    try {
        const auto parsed = std::stoull(value);
        if (parsed > std::numeric_limits<std::size_t>::max()) return std::nullopt;
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

bool apply_operation(Json& document, const ResourcePatchOperation& operation,
                     const JsonBounds bounds, std::string& error)
{
    const auto parts = split_pointer(operation.path);
    if (!parts) {
        error = "Invalid JSON pointer";
        return false;
    }
    const bool needs_value = operation.op == "add" || operation.op == "replace";
    std::optional<Json> value;
    if (needs_value) {
        if (!operation.value_json) {
            error = "Patch operation is missing value";
            return false;
        }
        value = parse_json(*operation.value_json, bounds);
        if (!value) {
            error = "Patch value is invalid";
            return false;
        }
    } else if (operation.op != "remove") {
        error = "Unsupported patch operation";
        return false;
    }
    if (parts->empty()) {
        if (operation.op == "remove") {
            error = "Cannot remove document root";
            return false;
        }
        document = std::move(*value);
        return true;
    }

    Json* parent = std::addressof(document);
    for (std::size_t index = 0; index + 1 < parts->size(); ++index) {
        const auto& part = (*parts)[index];
        if (parent->is_object()) {
            const auto found = parent->find(part);
            if (found == parent->end()) {
                error = "Patch path does not exist";
                return false;
            }
            parent = std::addressof(found.value());
        } else if (parent->is_array()) {
            const auto position = array_index(part);
            if (!position || *position >= parent->size()) {
                error = "Patch array index is invalid";
                return false;
            }
            parent = std::addressof((*parent)[*position]);
        } else {
            error = "Patch path traverses a scalar";
            return false;
        }
    }

    const auto& leaf = parts->back();
    if (parent->is_object()) {
        if (operation.op == "remove") {
            if (parent->erase(leaf) == 0) {
                error = "Patch path does not exist";
                return false;
            }
        } else {
            (*parent)[leaf] = std::move(*value);
        }
        return true;
    }
    if (parent->is_array()) {
        if (operation.op == "add" && leaf == "-") {
            parent->push_back(std::move(*value));
            return true;
        }
        const auto position = array_index(leaf);
        if (!position) {
            error = "Patch array index is invalid";
            return false;
        }
        if (operation.op == "add") {
            if (*position > parent->size()) {
                error = "Patch array index is invalid";
                return false;
            }
            parent->insert(
                parent->begin() + static_cast<Json::difference_type>(*position),
                std::move(*value));
        } else if (*position >= parent->size()) {
            error = "Patch array index is invalid";
            return false;
        } else if (operation.op == "replace") {
            (*parent)[*position] = std::move(*value);
        } else {
            parent->erase(
                parent->begin() + static_cast<Json::difference_type>(*position));
        }
        return true;
    }
    error = "Patch path parent is not a container";
    return false;
}

Json operations_json(const std::vector<ResourcePatchOperation>& operations)
{
    Json result = Json::array();
    for (const auto& operation : operations) {
        Json item{{"op", operation.op}, {"path", operation.path}};
        if (operation.value_json) {
            item["value"] = Json::parse(*operation.value_json);
        }
        result.push_back(std::move(item));
    }
    return result;
}

bool valid_resource_id(const std::string& value, const std::size_t maximum)
{
    if (value.empty() || value.size() > maximum || !is_valid_utf8(value)
        || value == "." || value == "..") {
        return false;
    }
    return std::none_of(value.begin(), value.end(), [](const unsigned char character) {
        return character == '/' || character == '\\' || character == ':'
            || character == 0 || character < 0x20 || character == 0x7f;
    });
}

enum class PathKind { missing, regular_file, directory, reparse, other, error };

PathKind path_kind(const std::filesystem::path& path) noexcept
{
    try {
#if defined(_WIN32)
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return GetLastError() == ERROR_FILE_NOT_FOUND
                || GetLastError() == ERROR_PATH_NOT_FOUND
            ? PathKind::missing
            : PathKind::error;
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return PathKind::reparse;
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) return PathKind::directory;
    return PathKind::regular_file;
#else
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        return error == std::errc::no_such_file_or_directory
            ? PathKind::missing : PathKind::error;
    }
    if (std::filesystem::is_symlink(status)) return PathKind::reparse;
    if (std::filesystem::is_regular_file(status)) return PathKind::regular_file;
    if (std::filesystem::is_directory(status)) return PathKind::directory;
    if (!std::filesystem::exists(status)) return PathKind::missing;
    return PathKind::other;
#endif
    } catch (...) {
        return PathKind::error;
    }
}

bool path_is_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate) noexcept
{
    try {
    std::error_code root_error;
    std::error_code candidate_error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, root_error);
    const auto canonical_candidate =
        std::filesystem::weakly_canonical(candidate, candidate_error);
    if (root_error || candidate_error) return false;
    auto root_part = canonical_root.begin();
    auto candidate_part = canonical_candidate.begin();
    for (; root_part != canonical_root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == canonical_candidate.end() || *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
    } catch (...) {
        return false;
    }
}

std::optional<double> file_mtime_ms(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return std::nullopt;
    try {
        const auto system_time = std::chrono::system_clock::now()
            + (modified - std::filesystem::file_time_type::clock::now());
        const auto millis = std::chrono::duration<double, std::milli>(
                                system_time.time_since_epoch())
                                .count();
        return std::isfinite(millis) ? std::optional{millis} : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::uint64_t process_id() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

std::filesystem::path temporary_path(const std::filesystem::path& target,
                                     const std::uint64_t sequence)
{
    auto name = target.filename().string();
    name += ".baas.tmp." + std::to_string(process_id()) + "."
        + std::to_string(sequence);
    std::u8string encoded;
    encoded.reserve(name.size());
    for (const auto character : name) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return target.parent_path() / std::filesystem::path(encoded);
}

std::string filename_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.filename().u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

AtomicWriteResult checked_post_commit_durability(
    const std::filesystem::path& parent,
    const FileResourceStoreDependencies::PostCommitDurabilityCheck& check) noexcept
{
    if (!check) return AtomicWriteResult::committed;
    try {
        return check(parent) ? AtomicWriteResult::committed
                             : AtomicWriteResult::committed_durability_uncertain;
    } catch (...) {
        return AtomicWriteResult::committed_durability_uncertain;
    }
}

#if defined(_WIN32)
AtomicWriteResult durable_atomic_write(const std::filesystem::path& target,
                                       const std::string_view bytes,
                                       const FileResourceStoreDependencies::
                                           PostCommitDurabilityCheck& check)
{
    static std::atomic<std::uint64_t> next_sequence{};
    const auto parent = target.parent_path();
    for (std::size_t attempt = 0; attempt < 64; ++attempt) {
        const auto temporary = temporary_path(target, next_sequence.fetch_add(1));
        HANDLE file = CreateFileW(
            temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS
                || GetLastError() == ERROR_ALREADY_EXISTS) {
                continue;
            }
            return AtomicWriteResult::not_committed;
        }

        bool ok = true;
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                remaining, std::numeric_limits<DWORD>::max()));
            DWORD written{};
            if (!WriteFile(file, bytes.data() + offset, chunk, &written, nullptr)
                || written == 0) {
                ok = false;
                break;
            }
            offset += written;
        }
        if (ok && !FlushFileBuffers(file)) ok = false;
        if (!CloseHandle(file)) ok = false;
        if (ok) {
            ok = MoveFileExW(
                     temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                != 0;
        }
        if (!ok) static_cast<void>(DeleteFileW(temporary.c_str()));
        if (!ok) return AtomicWriteResult::not_committed;
        return checked_post_commit_durability(parent, check);
    }
    return AtomicWriteResult::not_committed;
}
#else
AtomicWriteResult durable_atomic_write(const std::filesystem::path& target,
                                       const std::string_view bytes,
                                       const FileResourceStoreDependencies::
                                           PostCommitDurabilityCheck& check)
{
    static std::atomic<std::uint64_t> next_sequence{};
    const auto parent = target.parent_path();
    mode_t mode = 0600;
    struct stat target_status {};
    if (::stat(target.c_str(), &target_status) == 0) {
        mode = target_status.st_mode & 0777;
    }
    for (std::size_t attempt = 0; attempt < 64; ++attempt) {
        const auto temporary = temporary_path(target, next_sequence.fetch_add(1));
        const int file = ::open(
            temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
        if (file < 0) {
            if (errno == EEXIST) continue;
            return AtomicWriteResult::not_committed;
        }
        bool ok = true;
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                ok = false;
                break;
            }
            offset += static_cast<std::size_t>(written);
        }
        if (ok && ::fsync(file) != 0) ok = false;
        if (::close(file) != 0) ok = false;
        if (ok && ::rename(temporary.c_str(), target.c_str()) != 0) ok = false;
        if (!ok) {
            static_cast<void>(::unlink(temporary.c_str()));
            return AtomicWriteResult::not_committed;
        }
        const int directory = ::open(
            parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0) {
            return AtomicWriteResult::committed_durability_uncertain;
        }
        const bool directory_ok = ::fsync(directory) == 0;
        const bool close_ok = ::close(directory) == 0;
        if (!directory_ok || !close_ok) {
            return AtomicWriteResult::committed_durability_uncertain;
        }
        return checked_post_commit_durability(parent, check);
    }
    return AtomicWriteResult::not_committed;
}
#endif

double system_clock_ms()
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool valid_limits(const channels::ResourceStoreLimits& limits) noexcept
{
    return limits.max_json_bytes != 0 && limits.max_json_depth != 0
        && limits.max_json_nodes != 0 && limits.max_resources != 0
        && limits.max_subscribers != 0 && limits.max_patch_operations != 0
        && limits.max_resource_id_bytes != 0 && limits.max_origin_bytes != 0;
}

}  // namespace

class FileResourceStore::Impl {
public:
    struct SubscriberSlot {
        UpdateCallback callback;
        bool accepting{true};
        std::size_t entered{};
    };

    inline static thread_local SubscriberSlot* callback_slot{};
    inline static thread_local Impl* publication_drainer{};

    struct Subscribers {
        std::mutex mutex;
        std::condition_variable condition;
        bool active{true};
        std::size_t next_id{};
        std::size_t maximum{};
        std::unordered_map<std::size_t, std::shared_ptr<SubscriberSlot>> slots;
    };

    struct PublicationJob {
        channels::ResourceUpdate update;
        std::vector<std::shared_ptr<SubscriberSlot>> slots;
        std::shared_ptr<PublicationJob> next;
        std::condition_variable condition;
        bool done{};
    };

    struct Subscription final : channels::ResourceSubscription {
        Subscription(std::weak_ptr<Subscribers> owner, const std::size_t id,
                     std::shared_ptr<SubscriberSlot> slot)
            : owner(std::move(owner)), id(id), slot(std::move(slot))
        {}

        ~Subscription() override
        {
            const auto owner_state = owner.lock();
            if (!owner_state) return;
            std::unique_lock lock(owner_state->mutex);
            slot->accepting = false;
            owner_state->slots.erase(id);
            if (Impl::callback_slot == slot.get()) return;
            owner_state->condition.wait(lock, [this] { return slot->entered == 0; });
        }

        std::weak_ptr<Subscribers> owner;
        std::size_t id;
        std::shared_ptr<SubscriberSlot> slot;
    };

    struct LoadedResource {
        ResourceSnapshot snapshot;
        Json document;
        std::filesystem::path path;
    };

    struct LoadResult {
        std::optional<LoadedResource> value;
        ResourceStoreError error{ResourceStoreError::none};
    };

    Impl(std::filesystem::path supplied_root,
         FileResourceStoreDependencies dependencies,
         const channels::ResourceStoreLimits supplied_limits)
        : limits(supplied_limits), subscribers(std::make_shared<Subscribers>())
    {
        if (!valid_limits(limits) || supplied_root.empty()) {
            throw std::invalid_argument("invalid file resource store configuration");
        }
        std::error_code absolute_error;
        root = std::filesystem::absolute(supplied_root, absolute_error).lexically_normal();
        if (absolute_error || path_kind(root) != PathKind::directory) {
            throw std::invalid_argument("project root must be a safe existing directory");
        }
        config_root = root / "config";
        const auto config_kind = path_kind(config_root);
        if (config_kind != PathKind::missing && config_kind != PathKind::directory) {
            throw std::invalid_argument("config root must be a safe directory");
        }
        clock = dependencies.clock ? std::move(dependencies.clock) : system_clock_ms;
        if (dependencies.atomic_writer) {
            atomic_writer = std::move(dependencies.atomic_writer);
        } else {
            auto post_commit_check =
                std::move(dependencies.post_commit_durability_check);
            atomic_writer = [post_commit_check = std::move(post_commit_check)](
                                const std::filesystem::path& target,
                                const std::string_view bytes) {
                return durable_atomic_write(target, bytes, post_commit_check);
            };
        }
        if (!clock || !atomic_writer) {
            throw std::invalid_argument("file resource store dependencies are invalid");
        }
        subscribers->maximum = limits.max_subscribers;
    }

    ~Impl()
    {
        std::unique_lock lock(subscribers->mutex);
        subscribers->active = false;
        for (auto& [id, slot] : subscribers->slots) {
            static_cast<void>(id);
            slot->accepting = false;
        }
        subscribers->condition.wait(lock, [this] {
            return std::all_of(subscribers->slots.begin(), subscribers->slots.end(),
                               [](const auto& item) {
                const auto& slot = item.second;
                return slot->entered == 0;
            });
        });
        subscribers->slots.clear();
    }

    JsonBounds bounds() const noexcept
    {
        return {limits.max_json_bytes, limits.max_json_depth, limits.max_json_nodes};
    }

    std::optional<ResourceStoreError> validate_key(const ResourceKey& key) const
    {
        if (key.resource == SyncResource::config || key.resource == SyncResource::event) {
            if (!key.resource_id
                || !valid_resource_id(*key.resource_id, limits.max_resource_id_bytes)) {
                return ResourceStoreError::invalid_data;
            }
        } else if (key.resource_id) {
            return ResourceStoreError::invalid_data;
        }
        if (key.resource == SyncResource::setup_toml) {
            return ResourceStoreError::not_found;
        }
        return std::nullopt;
    }

    std::filesystem::path unchecked_path(const ResourceKey& key) const
    {
        const auto id_path = [&key] {
            std::u8string encoded;
            if (!key.resource_id) return std::filesystem::path{};
            encoded.reserve(key.resource_id->size());
            for (const auto character : *key.resource_id) {
                encoded.push_back(
                    static_cast<char8_t>(static_cast<unsigned char>(character)));
            }
            return std::filesystem::path(encoded);
        }();
        switch (key.resource) {
            case SyncResource::config:
                return config_root / id_path / "config.json";
            case SyncResource::event:
                return config_root / id_path / "event.json";
            case SyncResource::gui: return config_root / "gui.json";
            case SyncResource::static_data: return config_root / "static.json";
            case SyncResource::setup_toml: break;
        }
        return {};
    }

    ResourceStoreError validate_existing_path(
        const ResourceKey& key, const std::filesystem::path& path) const
    {
        const auto config_kind = path_kind(config_root);
        if (config_kind == PathKind::missing) return ResourceStoreError::not_found;
        if (config_kind != PathKind::directory) return ResourceStoreError::invalid_data;
        if (!path_is_within(config_root, path)) return ResourceStoreError::invalid_data;
        if (key.resource == SyncResource::config || key.resource == SyncResource::event) {
            const auto directory = path.parent_path();
            const auto directory_kind = path_kind(directory);
            if (directory_kind == PathKind::missing) return ResourceStoreError::not_found;
            if (directory_kind != PathKind::directory) return ResourceStoreError::invalid_data;
        }
        const auto target_kind = path_kind(path);
        if (target_kind == PathKind::missing) return ResourceStoreError::not_found;
        if (target_kind != PathKind::regular_file) return ResourceStoreError::invalid_data;
        return ResourceStoreError::none;
    }

    LoadResult load(const ResourceKey& key) const
    {
        if (const auto invalid = validate_key(key)) return {std::nullopt, *invalid};
        const auto path = unchecked_path(key);
        const auto path_error = validate_existing_path(key, path);
        if (path_error != ResourceStoreError::none) return {std::nullopt, path_error};

        std::error_code size_error;
        const auto size = std::filesystem::file_size(path, size_error);
        if (size_error) return {std::nullopt, ResourceStoreError::internal_error};
        if (size > limits.max_json_bytes) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        std::string bytes(static_cast<std::size_t>(size), '\0');
        std::ifstream input(path, std::ios::binary);
        if (!input) return {std::nullopt, ResourceStoreError::internal_error};
        if (!bytes.empty()) {
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
                return {std::nullopt, ResourceStoreError::internal_error};
            }
        }
        if (input.peek() != std::char_traits<char>::eof()) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        const auto document = parse_json(bytes, bounds());
        if (!document) return {std::nullopt, ResourceStoreError::invalid_data};
        const auto modified = file_mtime_ms(path);
        if (!modified) return {std::nullopt, ResourceStoreError::internal_error};
        try {
            ResourceSnapshot snapshot{timestamp_json(*modified), document->dump()};
            if (snapshot.data_json.size() > limits.max_json_bytes) {
                return {std::nullopt, ResourceStoreError::capacity};
            }
            return {LoadedResource{std::move(snapshot), *document, path},
                    ResourceStoreError::none};
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
    }

    std::shared_ptr<PublicationJob> prepare_publication(
        const channels::ResourceUpdate& update)
    {
        try {
            auto job = std::make_shared<PublicationJob>();
            job->update = update;
            job->slots.reserve(subscribers->maximum);
            return job;
        } catch (...) {
            return {};
        }
    }

    void finalize_publication(const std::shared_ptr<PublicationJob>& job) noexcept
    {
        std::lock_guard lock(subscribers->mutex);
        if (!subscribers->active) return;
        for (const auto& [id, slot] : subscribers->slots) {
            static_cast<void>(id);
            if (slot->accepting) job->slots.push_back(slot);
        }
    }

    void invoke_callbacks(PublicationJob& job) noexcept
    {
        for (const auto& slot : job.slots) {
            {
                std::lock_guard lock(subscribers->mutex);
                if (!subscribers->active || !slot->accepting) continue;
                ++slot->entered;
            }
            auto* const previous_slot = callback_slot;
            callback_slot = slot.get();
            try {
                slot->callback(job.update);
            } catch (...) {
            }
            callback_slot = previous_slot;
            std::lock_guard lock(subscribers->mutex);
            --slot->entered;
            subscribers->condition.notify_all();
        }
    }

    bool enqueue_publication(const std::shared_ptr<PublicationJob>& job) noexcept
    {
        std::lock_guard lock(publication_mutex);
        if (publication_tail) publication_tail->next = job;
        else publication_head = job;
        publication_tail = job;
        if (publication_active) return false;
        publication_active = true;
        return true;
    }

    void drain_or_wait(const std::shared_ptr<PublicationJob>& own_job,
                       const bool leader) noexcept
    {
        if (!leader) {
            if (publication_drainer == this) return;
            std::unique_lock lock(publication_mutex);
            own_job->condition.wait(lock, [&own_job] { return own_job->done; });
            return;
        }

        auto* const previous_drainer = publication_drainer;
        publication_drainer = this;
        for (;;) {
            std::shared_ptr<PublicationJob> job;
            {
                std::lock_guard lock(publication_mutex);
                job = publication_head;
                if (!job) {
                    publication_tail.reset();
                    publication_active = false;
                    break;
                }
                publication_head = job->next;
                if (!publication_head) publication_tail.reset();
                job->next.reset();
            }
            invoke_callbacks(*job);
            {
                std::lock_guard lock(publication_mutex);
                job->done = true;
                job->condition.notify_all();
            }
        }
        publication_drainer = previous_drainer;
    }

    std::filesystem::path root;
    std::filesystem::path config_root;
    FileResourceStoreDependencies::Clock clock;
    FileResourceStoreDependencies::AtomicWriter atomic_writer;
    channels::ResourceStoreLimits limits;
    std::mutex state_mutex;
    std::mutex mutation_mutex;
    std::mutex publication_mutex;
    std::shared_ptr<PublicationJob> publication_head;
    std::shared_ptr<PublicationJob> publication_tail;
    bool publication_active{};
    std::unordered_map<ResourceKey, ResourceSnapshot, ResourceKeyHash> resources;
    std::shared_ptr<Subscribers> subscribers;
};

FileResourceStore::FileResourceStore(
    std::filesystem::path project_root, FileResourceStoreDependencies dependencies,
    const channels::ResourceStoreLimits limits)
    : impl_(std::make_shared<Impl>(
          std::move(project_root), std::move(dependencies), limits))
{}

FileResourceStore::~FileResourceStore() = default;

channels::ResourceStoreResult<ResourceSnapshot> FileResourceStore::config_list(
    const std::stop_token stop) try
{
    const auto impl = impl_;
    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    std::vector<std::string> identifiers;
    const auto config_kind = path_kind(impl->config_root);
    if (config_kind == PathKind::missing) {
        double now{};
        try {
            now = impl->clock();
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (!std::isfinite(now)) return {std::nullopt, ResourceStoreError::internal_error};
        return {ResourceSnapshot{timestamp_json(now), "[]"}, ResourceStoreError::none};
    }
    if (config_kind != PathKind::directory) {
        return {std::nullopt, ResourceStoreError::invalid_data};
    }

    std::error_code iteration_error;
    std::filesystem::directory_iterator iterator(
        impl->config_root, std::filesystem::directory_options::skip_permission_denied,
        iteration_error);
    const std::filesystem::directory_iterator end;
    for (; !iteration_error && iterator != end; iterator.increment(iteration_error)) {
        if (stop.stop_requested()) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        const auto& child = iterator->path();
        std::string name;
        try {
            name = filename_utf8(child);
        } catch (...) {
            continue;
        }
        if (!valid_resource_id(name, impl->limits.max_resource_id_bytes)
            || path_kind(child) != PathKind::directory
            || !path_is_within(impl->config_root, child)
            || path_kind(child / "config.json") != PathKind::regular_file
            || path_kind(child / "event.json") != PathKind::regular_file) {
            continue;
        }
        if (identifiers.size() >= impl->limits.max_resources) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        identifiers.push_back(name);
    }
    if (iteration_error) return {std::nullopt, ResourceStoreError::internal_error};
    std::sort(identifiers.begin(), identifiers.end());

    try {
        Json data = identifiers;
        const auto serialized = data.dump();
        if (serialized.size() > impl->limits.max_json_bytes) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        double now = impl->clock();
        if (!std::isfinite(now)) return {std::nullopt, ResourceStoreError::internal_error};
        return {ResourceSnapshot{timestamp_json(now), serialized}, ResourceStoreError::none};
    } catch (...) {
        return {std::nullopt, ResourceStoreError::internal_error};
    }
} catch (...) {
    return {std::nullopt, ResourceStoreError::internal_error};
}

channels::ResourceStoreResult<ResourceSnapshot> FileResourceStore::pull(
    const ResourceKey& key, const std::stop_token stop) try
{
    const auto impl = impl_;
    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    std::lock_guard lock(impl->state_mutex);
    const auto found = impl->resources.find(key);
    if (found != impl->resources.end()) return {found->second, ResourceStoreError::none};
    auto loaded = impl->load(key);
    if (!loaded.value) return {std::nullopt, loaded.error};
    if (impl->resources.size() >= impl->limits.max_resources) {
        return {std::nullopt, ResourceStoreError::capacity};
    }
    ResourceSnapshot result = loaded.value->snapshot;
    try {
        impl->resources.emplace(key, std::move(loaded.value->snapshot));
    } catch (...) {
        return {std::nullopt, ResourceStoreError::internal_error};
    }
    return {std::move(result), ResourceStoreError::none};
} catch (...) {
    return {std::nullopt, ResourceStoreError::internal_error};
}

channels::ResourceStoreResult<channels::ResourcePatchResult>
FileResourceStore::apply_patch(channels::ResourcePatchRequest request,
                               const std::stop_token stop) try
{
    using channels::ResourcePatchDisposition;
    using channels::ResourcePatchResult;
    using channels::ResourceUpdate;
    const auto impl = impl_;

    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    if (request.operations.size() > impl->limits.max_patch_operations) {
        return {std::nullopt, ResourceStoreError::capacity};
    }
    if (const auto invalid = impl->validate_key(request.key)) {
        return {std::nullopt, *invalid};
    }
    if (request.key.resource == SyncResource::static_data) {
        return {std::nullopt, ResourceStoreError::invalid_data};
    }
    const auto bounds = impl->bounds();
    const auto expected = timestamp_value(request.expected_timestamp_json, bounds);
    if (!expected) return {std::nullopt, ResourceStoreError::invalid_data};

    std::size_t operation_bytes = 2;
    for (const auto& operation : request.operations) {
        const std::size_t components[]{
            operation.op.size(), operation.path.size(),
            operation.value_json ? operation.value_json->size() : 0, 48};
        for (const auto component : components) {
            if (component > impl->limits.max_json_bytes
                || operation_bytes > impl->limits.max_json_bytes - component) {
                return {std::nullopt, ResourceStoreError::capacity};
            }
            operation_bytes += component;
        }
        if ((operation.op != "add" && operation.op != "remove"
             && operation.op != "replace")
            || !split_pointer(operation.path)) {
            return {std::nullopt, ResourceStoreError::invalid_data};
        }
        if (operation.value_json && !parse_json(*operation.value_json, bounds)) {
            return {std::nullopt, ResourceStoreError::invalid_data};
        }
    }

    std::string serialized_operations;
    try {
        serialized_operations = operations_json(request.operations).dump();
    } catch (...) {
        return {std::nullopt, ResourceStoreError::invalid_data};
    }
    if (serialized_operations.size() > impl->limits.max_json_bytes) {
        return {std::nullopt, ResourceStoreError::capacity};
    }

    ResourcePatchResult result;
    ResourceUpdate update;
    std::shared_ptr<Impl::PublicationJob> publication;
    std::unique_lock mutation_lock(impl->mutation_mutex);
    {
        std::lock_guard lock(impl->state_mutex);
        if (stop.stop_requested()) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        auto found = impl->resources.find(request.key);
        if (found == impl->resources.end()) {
            auto loaded = impl->load(request.key);
            if (!loaded.value) return {std::nullopt, loaded.error};
            if (impl->resources.size() >= impl->limits.max_resources) {
                return {std::nullopt, ResourceStoreError::capacity};
            }
            try {
                found = impl->resources
                            .emplace(request.key, std::move(loaded.value->snapshot))
                            .first;
            } catch (...) {
                return {std::nullopt, ResourceStoreError::internal_error};
            }
        }
        auto disk = impl->load(request.key);
        if (!disk.value) return {std::nullopt, disk.error};
        if (disk.value->snapshot.data_json != found->second.data_json) {
            found->second = std::move(disk.value->snapshot);
            result = {ResourcePatchDisposition::conflict, found->second,
                      "Resource changed outside the store"};
            return {std::move(result), ResourceStoreError::none};
        }
        const auto current = timestamp_value(found->second.timestamp_json, bounds);
        if (!current) return {std::nullopt, ResourceStoreError::internal_error};
        if (*expected < *current) {
            result = {ResourcePatchDisposition::conflict, found->second,
                      "Incoming patch is older than current snapshot"};
            return {std::move(result), ResourceStoreError::none};
        }
        auto document = parse_json(found->second.data_json, bounds);
        if (!document) return {std::nullopt, ResourceStoreError::internal_error};
        std::string patch_error;
        for (const auto& operation : request.operations) {
            if (!apply_operation(*document, operation, bounds, patch_error)) {
                result = {ResourcePatchDisposition::conflict, found->second,
                          std::move(patch_error)};
                return {std::move(result), ResourceStoreError::none};
            }
        }
        std::size_t nodes{};
        if (!bounded_tree(
                *document, bounds.depth, bounds.nodes, 0, nodes)) {
            return {std::nullopt, ResourceStoreError::capacity};
        }

        double now{};
        try {
            now = impl->clock();
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (!std::isfinite(now)) return {std::nullopt, ResourceStoreError::internal_error};

        std::string bytes;
        try {
            bytes = document->dump(2);
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (bytes.size() > impl->limits.max_json_bytes) {
            return {std::nullopt, ResourceStoreError::capacity};
        }
        const auto path = impl->unchecked_path(request.key);
        const auto path_error = impl->validate_existing_path(request.key, path);
        if (path_error != ResourceStoreError::none) return {std::nullopt, path_error};

        ResourceSnapshot committed;
        try {
            committed = {timestamp_json(std::max(*expected, now)), document->dump()};
            result = {ResourcePatchDisposition::applied, committed, {}};
            update = {request.key, committed.timestamp_json,
                      serialized_operations, "frontend"};
            publication = impl->prepare_publication(update);
        } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (!publication) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }

        AtomicWriteResult write_result{AtomicWriteResult::not_committed};
        try {
            write_result = impl->atomic_writer(path, bytes);
        } catch (...) {
            write_result = AtomicWriteResult::not_committed;
        }
        if (write_result == AtomicWriteResult::not_committed) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }

        found->second = std::move(committed);
        impl->finalize_publication(publication);
    }
    const bool publication_leader = impl->enqueue_publication(publication);
    mutation_lock.unlock();
    impl->drain_or_wait(publication, publication_leader);
    return {std::move(result), ResourceStoreError::none};
} catch (...) {
    return {std::nullopt, ResourceStoreError::internal_error};
}

channels::ResourceSubscribeResult FileResourceStore::subscribe_updates(
    UpdateCallback callback) try
{
    const auto impl = impl_;
    if (!callback) return {nullptr, ResourceStoreError::invalid_data};
    const auto subscribers = impl->subscribers;
    std::lock_guard lock(subscribers->mutex);
    if (!subscribers->active) return {nullptr, ResourceStoreError::internal_error};
    if (subscribers->slots.size() >= subscribers->maximum) {
        return {nullptr, ResourceStoreError::capacity};
    }
    try {
        auto slot = std::make_shared<Impl::SubscriberSlot>();
        slot->callback = std::move(callback);
        for (std::size_t attempt = 0; attempt <= subscribers->slots.size(); ++attempt) {
            const auto id = subscribers->next_id;
            subscribers->next_id = id == std::numeric_limits<std::size_t>::max()
                ? 0 : id + 1;
            if (subscribers->slots.contains(id)) continue;
            const auto [position, inserted] = subscribers->slots.emplace(id, slot);
            static_cast<void>(position);
            if (!inserted) continue;
            try {
                return {std::make_unique<Impl::Subscription>(subscribers, id, slot),
                        ResourceStoreError::none};
            } catch (...) {
                subscribers->slots.erase(id);
                throw;
            }
        }
        return {nullptr, ResourceStoreError::internal_error};
    } catch (...) {
        return {nullptr, ResourceStoreError::internal_error};
    }
} catch (...) {
    return {nullptr, ResourceStoreError::internal_error};
}

bool FileResourceStore::refresh_and_publish(ResourceKey key, std::string origin) try
{
    const auto impl = impl_;
    if (origin.size() > impl->limits.max_origin_bytes || !is_valid_utf8(origin)) {
        return false;
    }
    if (const auto invalid = impl->validate_key(key)) return false;

    channels::ResourceUpdate update;
    std::shared_ptr<Impl::PublicationJob> publication;
    std::unique_lock mutation_lock(impl->mutation_mutex);
    {
        std::lock_guard lock(impl->state_mutex);
        auto loaded = impl->load(key);
        if (!loaded.value) return false;
        auto found = impl->resources.find(key);
        if (found != impl->resources.end()
            && found->second.data_json == loaded.value->snapshot.data_json) {
            found->second.timestamp_json = std::move(loaded.value->snapshot.timestamp_json);
            return false;
        }
        try {
            Json operations = Json::array({
                Json{{"op", "replace"}, {"path", ""},
                     {"value", loaded.value->document}}});
            update = {key, loaded.value->snapshot.timestamp_json,
                      operations.dump(), std::move(origin)};
            publication = impl->prepare_publication(update);
            if (!publication) return false;
            if (found == impl->resources.end()) {
                if (impl->resources.size() >= impl->limits.max_resources) return false;
                impl->resources.emplace(key, std::move(loaded.value->snapshot));
            } else {
                found->second = std::move(loaded.value->snapshot);
            }
            impl->finalize_publication(publication);
        } catch (...) {
            return false;
        }
    }
    const bool publication_leader = impl->enqueue_publication(publication);
    mutation_lock.unlock();
    impl->drain_or_wait(publication, publication_leader);
    return true;
} catch (...) {
    return false;
}

const std::filesystem::path& FileResourceStore::project_root() const noexcept
{
    return impl_->root;
}

}  // namespace baas::service::adapters
