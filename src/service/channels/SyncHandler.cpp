#include "service/channels/SyncHandler.h"

#include "service/auth/CanonicalJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace baas::service::channels {
namespace {

using Json = nlohmann::json;
namespace ws = websocket;

struct JsonBounds {
    std::size_t bytes;
    std::size_t depth;
    std::size_t nodes;
};

bool bounded_tree(const Json& value, const std::size_t maximum_depth,
                  const std::size_t maximum_nodes, const std::size_t depth,
                  std::size_t& nodes)
{
    if (++nodes > maximum_nodes || depth > maximum_depth) return false;
    if (value.is_array()) {
        for (const auto& child : value) {
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) return false;
        }
    } else if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            (void)key;
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes)) return false;
        }
    }
    return true;
}

std::optional<Json> parse_json(const std::string_view text, const JsonBounds bounds)
{
    if (text.size() > bounds.bytes) return std::nullopt;
    try {
        bool duplicate{};
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto callback = [&duplicate, &object_keys](int, Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) object_keys.emplace_back();
            else if (event == Json::parse_event_t::key && !object_keys.empty()) {
                if (!object_keys.back().insert(parsed.get<std::string>()).second) duplicate = true;
            } else if (event == Json::parse_event_t::object_end && !object_keys.empty()) {
                object_keys.pop_back();
            }
            return !duplicate;
        };
        Json value = Json::parse(text, callback, false);
        if (duplicate || value.is_discarded()) return std::nullopt;
        std::size_t nodes = 0;
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

std::optional<double> timestamp_value(const std::string& value, const JsonBounds bounds)
{
    const auto parsed = parse_json(value, bounds);
    return parsed ? timestamp_value(*parsed) : std::nullopt;
}

std::string timestamp_json(const double value)
{
    return Json(value).dump();
}

std::string resource_name(const SyncResource resource)
{
    switch (resource) {
        case SyncResource::config: return "config";
        case SyncResource::event: return "event";
        case SyncResource::gui: return "gui";
        case SyncResource::static_data: return "static";
        case SyncResource::setup_toml: return "setup_toml";
    }
    return {};
}

std::optional<SyncResource> parse_resource(const std::string_view value)
{
    if (value == "config") return SyncResource::config;
    if (value == "event") return SyncResource::event;
    if (value == "gui") return SyncResource::gui;
    if (value == "static") return SyncResource::static_data;
    if (value == "setup_toml") return SyncResource::setup_toml;
    return std::nullopt;
}

bool mutable_resource(const SyncResource value) noexcept
{
    return value != SyncResource::static_data;
}

struct ResourceKeyHash {
    std::size_t operator()(const ResourceKey& key) const noexcept
    {
        auto result = static_cast<std::size_t>(key.resource) * 0x9e3779b9U;
        if (key.resource_id) result ^= std::hash<std::string>{}(*key.resource_id);
        return result;
    }
};

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
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](const char c) {
            return c >= '0' && c <= '9';
        })) return std::nullopt;
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
    if (!parts) { error = "Invalid JSON pointer"; return false; }
    const bool needs_value = operation.op == "add" || operation.op == "replace";
    std::optional<Json> value;
    if (needs_value) {
        if (!operation.value_json) { error = "Patch operation is missing value"; return false; }
        value = parse_json(*operation.value_json, bounds);
        if (!value) { error = "Patch value is invalid"; return false; }
    } else if (operation.op != "remove") {
        error = "Unsupported patch operation";
        return false;
    }
    if (parts->empty()) {
        if (operation.op == "remove") { error = "Cannot remove document root"; return false; }
        document = std::move(*value);
        return true;
    }
    Json* parent = &document;
    for (std::size_t index = 0; index + 1 < parts->size(); ++index) {
        const auto& part = (*parts)[index];
        if (parent->is_object()) {
            const auto found = parent->find(part);
            if (found == parent->end()) { error = "Patch path does not exist"; return false; }
            parent = std::addressof(found.value());
        } else if (parent->is_array()) {
            const auto position = array_index(part);
            if (!position || *position >= parent->size()) { error = "Patch array index is invalid"; return false; }
            parent = std::addressof((*parent)[*position]);
        } else { error = "Patch path traverses a scalar"; return false; }
    }
    const auto& leaf = parts->back();
    if (parent->is_object()) {
        if (operation.op == "remove") {
            if (parent->erase(leaf) == 0) { error = "Patch path does not exist"; return false; }
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
        if (!position) { error = "Patch array index is invalid"; return false; }
        if (operation.op == "add") {
            if (*position > parent->size()) { error = "Patch array index is invalid"; return false; }
            parent->insert(parent->begin() + static_cast<Json::difference_type>(*position), std::move(*value));
        } else if (*position >= parent->size()) {
            error = "Patch array index is invalid";
            return false;
        } else if (operation.op == "replace") {
            (*parent)[*position] = std::move(*value);
        } else {
            parent->erase(parent->begin() + static_cast<Json::difference_type>(*position));
        }
        return true;
    }
    error = "Patch parent is not a container";
    return false;
}

Json operations_json(const std::vector<ResourcePatchOperation>& operations)
{
    Json result = Json::array();
    for (const auto& operation : operations) {
        Json item{{"op", operation.op}, {"path", operation.path}};
        if (operation.value_json) item["value"] = Json::parse(*operation.value_json);
        result.push_back(std::move(item));
    }
    return result;
}

ws::BusinessHandlerResult status_result(const ws::BusinessHandlerStatus status)
{
    return {{}, status};
}

ws::BusinessHandlerStatus store_status(const ResourceStoreError error)
{
    return error == ResourceStoreError::capacity ? ws::BusinessHandlerStatus::capacity
                                                  : ws::BusinessHandlerStatus::internal_error;
}

std::optional<std::optional<std::string>> optional_id(const Json& request, const std::size_t limit)
{
    const auto found = request.find("resource_id");
    if (found == request.end() || found->is_null()) return std::optional<std::string>{};
    if (!found->is_string()) return std::nullopt;
    auto value = found->get<std::string>();
    if (value.size() > limit) return std::nullopt;
    return std::optional<std::string>{std::move(value)};
}

Json key_json(const ResourceKey& key)
{
    return key.resource_id ? Json(*key.resource_id) : Json(nullptr);
}

std::optional<Json> update_envelope(const ResourceUpdate& update,
                                    const SyncHandlerLimits& limits)
{
    if ((update.key.resource_id
         && (update.key.resource_id->size() > limits.max_resource_id_bytes
             || !auth::is_valid_utf8(*update.key.resource_id)))
        || update.origin.size() > limits.max_origin_bytes
        || !auth::is_valid_utf8(update.origin)) return std::nullopt;
    const JsonBounds bounds{limits.max_output_json_bytes, limits.max_json_depth, limits.max_json_nodes};
    const auto timestamp = parse_json(update.timestamp_json, bounds);
    const auto operations = parse_json(update.operations_json, bounds);
    if (!timestamp || !timestamp_value(*timestamp) || !operations || !operations->is_array()) return std::nullopt;
    Json result{{"type", "patch"}, {"direction", "push"},
                {"resource", resource_name(update.key.resource)},
                {"resource_id", key_json(update.key)}, {"timestamp", *timestamp},
                {"ops", *operations}, {"origin", update.origin}};
    const auto text = result.dump();
    if (text.size() > limits.max_output_json_bytes) return std::nullopt;
    return result;
}

class SyncHandler final : public ws::BusinessChannelHandler {
public:
    struct Core {
        Core(std::shared_ptr<ws::BusinessPlaintextSink> supplied_output,
             SyncHandlerLimits supplied_limits)
            : output(std::move(supplied_output)), limits(supplied_limits) {}

        std::shared_ptr<ws::BusinessPlaintextSink> output;
        SyncHandlerLimits limits;
        std::mutex mutex;
        std::condition_variable condition;
        bool closed{};
        bool failed{};
        std::size_t in_flight{};
        std::size_t in_flight_bytes{};

        void push(ResourceUpdate update) noexcept
        {
            try {
                const auto envelope = update_envelope(update, limits);
                if (!envelope) { mark_failed(); return; }
                auto payload = envelope->dump();
                const auto payload_size = payload.size();
                {
                    std::lock_guard lock(mutex);
                    if (closed || failed) return;
                    if (in_flight >= limits.max_in_flight_pushes
                        || payload.size() > limits.max_in_flight_push_bytes - std::min(in_flight_bytes, limits.max_in_flight_push_bytes)) {
                        failed = true;
                        return;
                    }
                    ++in_flight;
                    in_flight_bytes += payload_size;
                }
                const auto emitted = output->emit({std::move(payload), false});
                {
                    std::lock_guard lock(mutex);
                    --in_flight;
                    in_flight_bytes -= payload_size;
                    if (emitted != ws::BusinessEmitResult::accepted) failed = true;
                }
                condition.notify_all();
            } catch (...) { mark_failed(); }
        }

        void mark_failed() noexcept
        {
            std::lock_guard lock(mutex);
            failed = true;
        }

        bool is_failed() noexcept
        {
            std::lock_guard lock(mutex);
            return failed;
        }

        void close() noexcept
        {
            std::unique_lock lock(mutex);
            closed = true;
            condition.wait(lock, [this] { return in_flight == 0; });
        }
    };

    SyncHandler(std::shared_ptr<ResourceStore> store,
                std::shared_ptr<ws::BusinessPlaintextSink> output,
                SyncHandlerLimits limits)
        : store_(std::move(store)), core_(std::make_shared<Core>(std::move(output), limits)), limits_(limits)
    {}

    ~SyncHandler() override { closed(ws::BusinessCloseReason::stopped); }

    ws::BusinessHandlerResult ready(std::stop_token stop) override
    {
        if (stop.stop_requested()) return status_result(ws::BusinessHandlerStatus::complete);
        std::weak_ptr<Core> weak = core_;
        auto result = store_->subscribe_updates([weak](ResourceUpdate update) {
            if (const auto core = weak.lock()) core->push(std::move(update));
        });
        if (!result) return status_result(store_status(result.error));
        subscription_ = std::move(result.subscription);
        return {};
    }

    ws::BusinessHandlerResult input(auth::SecretBuffer plaintext, bool peer_final,
                                    std::stop_token stop) override
    {
        if (peer_final || stop.stop_requested()) return status_result(ws::BusinessHandlerStatus::complete);
        if (core_->is_failed()) return status_result(ws::BusinessHandlerStatus::internal_error);
        const std::string_view text(
            reinterpret_cast<const char*>(plaintext.bytes().data()), plaintext.size());
        const JsonBounds bounds{limits_.max_input_json_bytes, limits_.max_json_depth, limits_.max_json_nodes};
        const auto request = parse_json(text, bounds);
        if (!request || !request->is_object()) return status_result(ws::BusinessHandlerStatus::protocol_failed);
        const auto type = request->find("type");
        if (type == request->end() || !type->is_string()) return status_result(ws::BusinessHandlerStatus::protocol_failed);
        if (*type == "list") return list(stop);
        if (*type == "pull") return pull(*request, stop);
        if (*type == "patch") return patch(*request, stop);
        return status_result(ws::BusinessHandlerStatus::protocol_failed);
    }

    ws::BusinessHandlerResult heartbeat(std::stop_token stop) override
    {
        if (stop.stop_requested()) return status_result(ws::BusinessHandlerStatus::complete);
        return core_->is_failed() ? status_result(ws::BusinessHandlerStatus::internal_error)
                                  : ws::BusinessHandlerResult{};
    }

    void closed(ws::BusinessCloseReason) noexcept override
    {
        std::call_once(close_once_, [this] {
            core_->close();
            subscription_.reset();
        });
    }

private:
    ws::BusinessHandlerResult one(Json response)
    {
        auto payload = response.dump();
        if (payload.size() > limits_.max_output_json_bytes) return status_result(ws::BusinessHandlerStatus::capacity);
        return {{{std::move(payload), false}}, ws::BusinessHandlerStatus::ok};
    }

    ws::BusinessHandlerResult list(std::stop_token stop)
    {
        const auto result = store_->config_list(stop);
        if (!result) return status_result(store_status(result.error));
        const JsonBounds bounds{limits_.max_output_json_bytes, limits_.max_json_depth, limits_.max_json_nodes};
        const auto timestamp = parse_json(result->timestamp_json, bounds);
        const auto data = parse_json(result->data_json, bounds);
        if (!timestamp || !timestamp_value(*timestamp) || !data) return status_result(ws::BusinessHandlerStatus::internal_error);
        return one(Json{{"type", "config_list"}, {"timestamp", *timestamp}, {"data", *data}});
    }

    std::optional<ResourceKey> key(const Json& request, const bool patching)
    {
        const auto resource = request.find("resource");
        if (resource == request.end() || !resource->is_string()) return std::nullopt;
        const auto parsed = parse_resource(resource->get<std::string>());
        if (!parsed || (patching && !mutable_resource(*parsed))) return std::nullopt;
        const auto id = optional_id(request, limits_.max_resource_id_bytes);
        if (!id) return std::nullopt;
        return ResourceKey{*parsed, std::move(*id)};
    }

    ws::BusinessHandlerResult pull(const Json& request, std::stop_token stop)
    {
        const auto resource_key = key(request, false);
        if (!resource_key) return status_result(ws::BusinessHandlerStatus::protocol_failed);
        const auto result = store_->pull(*resource_key, stop);
        if (!result) return status_result(store_status(result.error));
        const JsonBounds bounds{limits_.max_output_json_bytes, limits_.max_json_depth, limits_.max_json_nodes};
        const auto timestamp = parse_json(result->timestamp_json, bounds);
        const auto data = parse_json(result->data_json, bounds);
        if (!timestamp || !timestamp_value(*timestamp) || !data) return status_result(ws::BusinessHandlerStatus::internal_error);
        return one(Json{{"type", "snapshot"}, {"resource", resource_name(resource_key->resource)},
                        {"resource_id", key_json(*resource_key)}, {"timestamp", *timestamp}, {"data", *data}});
    }

    ws::BusinessHandlerResult patch(const Json& request, std::stop_token stop)
    {
        auto resource_key = key(request, true);
        if (!resource_key) return status_result(ws::BusinessHandlerStatus::protocol_failed);
        const auto timestamp = request.find("timestamp");
        const auto operations = request.find("ops");
        if (timestamp == request.end() || !timestamp_value(*timestamp)
            || operations == request.end() || !operations->is_array()
            || operations->size() > limits_.max_patch_operations) {
            return status_result(ws::BusinessHandlerStatus::protocol_failed);
        }
        ResourcePatchRequest patch_request{*resource_key, timestamp->dump(), {}};
        patch_request.operations.reserve(operations->size());
        for (const auto& item : *operations) {
            if (!item.is_object()) return status_result(ws::BusinessHandlerStatus::protocol_failed);
            const auto op = item.find("op");
            const auto path = item.find("path");
            if (op == item.end() || !op->is_string() || path == item.end() || !path->is_string())
                return status_result(ws::BusinessHandlerStatus::protocol_failed);
            const auto op_text = op->get<std::string>();
            const auto path_text = path->get<std::string>();
            if ((op_text != "add" && op_text != "remove" && op_text != "replace")
                || (!path_text.empty() && path_text.front() != '/')
                || !split_pointer(path_text))
                return status_result(ws::BusinessHandlerStatus::protocol_failed);
            ResourcePatchOperation parsed{op_text, path_text, std::nullopt};
            if (op_text != "remove") {
                const auto value = item.find("value");
                parsed.value_json = value == item.end() ? "null" : value->dump();
            }
            patch_request.operations.push_back(std::move(parsed));
        }
        const auto result = store_->apply_patch(std::move(patch_request), stop);
        if (!result) return status_result(store_status(result.error));
        if (result->disposition == ResourcePatchDisposition::applied) {
            return one(Json{{"type", "patch_ack"}, {"resource", resource_name(resource_key->resource)},
                            {"resource_id", key_json(*resource_key)}, {"timestamp", *timestamp}});
        }
        const JsonBounds bounds{limits_.max_output_json_bytes, limits_.max_json_depth, limits_.max_json_nodes};
        if (result->error.size() > limits_.max_error_bytes
            || !auth::is_valid_utf8(result->error))
            return status_result(ws::BusinessHandlerStatus::internal_error);
        const auto current_timestamp = parse_json(result->snapshot.timestamp_json, bounds);
        const auto data = parse_json(result->snapshot.data_json, bounds);
        if (!current_timestamp || !timestamp_value(*current_timestamp) || !data)
            return status_result(ws::BusinessHandlerStatus::internal_error);
        return one(Json{{"type", "patch_conflict"}, {"resource", resource_name(resource_key->resource)},
                        {"resource_id", key_json(*resource_key)}, {"request_timestamp", *timestamp},
                        {"timestamp", *current_timestamp}, {"data", *data}, {"error", result->error}});
    }

    std::shared_ptr<ResourceStore> store_;
    std::shared_ptr<Core> core_;
    SyncHandlerLimits limits_;
    std::unique_ptr<ResourceSubscription> subscription_;
    std::once_flag close_once_;
};

} // namespace

class InMemoryResourceStore::Impl {
public:
    struct Subscribers {
        std::mutex mutex;
        bool active{true};
        std::size_t next_id{};
        std::unordered_map<std::size_t, UpdateCallback> callbacks;
        std::size_t maximum{};
    };

    struct Subscription final : ResourceSubscription {
        Subscription(std::weak_ptr<Subscribers> subscribers, const std::size_t id)
            : subscribers(std::move(subscribers)), id(id) {}
        ~Subscription() override
        {
            if (const auto owner = subscribers.lock()) {
                std::lock_guard lock(owner->mutex);
                owner->callbacks.erase(id);
            }
        }
        std::weak_ptr<Subscribers> subscribers;
        std::size_t id;
    };

    Impl(std::vector<InitialResource> initial, ResourceSnapshot list, Clock supplied_clock,
         ResourceStoreLimits supplied_limits)
        : config_list(std::move(list)), clock(std::move(supplied_clock)), limits(supplied_limits),
          subscribers(std::make_shared<Subscribers>())
    {
        if (!clock || initial.size() > limits.max_resources) throw std::invalid_argument("invalid resource store configuration");
        subscribers->maximum = limits.max_subscribers;
        const JsonBounds bounds{limits.max_json_bytes, limits.max_json_depth, limits.max_json_nodes};
        if (!valid_snapshot(config_list, bounds)) throw std::invalid_argument("invalid config list snapshot");
        for (auto& item : initial) {
            if ((item.key.resource_id && item.key.resource_id->size() > limits.max_resource_id_bytes)
                || !valid_snapshot(item.snapshot, bounds)
                || !resources.emplace(std::move(item.key), std::move(item.snapshot)).second)
                throw std::invalid_argument("invalid or duplicate resource snapshot");
        }
    }

    ~Impl()
    {
        std::lock_guard lock(subscribers->mutex);
        subscribers->active = false;
        subscribers->callbacks.clear();
    }

    static bool valid_snapshot(const ResourceSnapshot& snapshot, const JsonBounds bounds)
    {
        const auto stamp = parse_json(snapshot.timestamp_json, bounds);
        return stamp && timestamp_value(*stamp) && parse_json(snapshot.data_json, bounds).has_value();
    }

    void publish(ResourceUpdate update)
    {
        std::vector<UpdateCallback> callbacks;
        {
            std::lock_guard lock(subscribers->mutex);
            if (!subscribers->active) return;
            callbacks.reserve(subscribers->callbacks.size());
            for (const auto& [id, callback] : subscribers->callbacks) {
                (void)id;
                callbacks.push_back(callback);
            }
        }
        for (auto& callback : callbacks) {
            try { callback(update); } catch (...) {}
        }
    }

    std::mutex mutex;
    std::unordered_map<ResourceKey, ResourceSnapshot, ResourceKeyHash> resources;
    ResourceSnapshot config_list;
    Clock clock;
    ResourceStoreLimits limits;
    std::shared_ptr<Subscribers> subscribers;
};

InMemoryResourceStore::InMemoryResourceStore(std::vector<InitialResource> resources,
                                             ResourceSnapshot config_list, Clock clock,
                                             ResourceStoreLimits limits)
    : impl_(std::make_unique<Impl>(std::move(resources), std::move(config_list),
                                   std::move(clock), limits)) {}

InMemoryResourceStore::~InMemoryResourceStore() = default;

ResourceStoreResult<ResourceSnapshot> InMemoryResourceStore::config_list(std::stop_token stop)
{
    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    std::lock_guard lock(impl_->mutex);
    return {impl_->config_list, ResourceStoreError::none};
}

ResourceStoreResult<ResourceSnapshot> InMemoryResourceStore::pull(const ResourceKey& key,
                                                                  std::stop_token stop)
{
    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->resources.find(key);
    if (found == impl_->resources.end()) return {std::nullopt, ResourceStoreError::not_found};
    return {found->second, ResourceStoreError::none};
}

ResourceStoreResult<ResourcePatchResult> InMemoryResourceStore::apply_patch(
    ResourcePatchRequest request, std::stop_token stop)
{
    if (stop.stop_requested()) return {std::nullopt, ResourceStoreError::internal_error};
    if (request.operations.size() > impl_->limits.max_patch_operations)
        return {std::nullopt, ResourceStoreError::capacity};
    const JsonBounds bounds{impl_->limits.max_json_bytes, impl_->limits.max_json_depth, impl_->limits.max_json_nodes};
    const auto expected = timestamp_value(request.expected_timestamp_json, bounds);
    if (!expected || !mutable_resource(request.key.resource)
        || (request.key.resource_id
            && request.key.resource_id->size() > impl_->limits.max_resource_id_bytes))
        return {std::nullopt, ResourceStoreError::invalid_data};
    for (const auto& operation : request.operations) {
        if ((operation.op != "add" && operation.op != "remove" && operation.op != "replace")
            || !split_pointer(operation.path))
            return {std::nullopt, ResourceStoreError::invalid_data};
    }
    std::size_t operation_bytes = 2;
    for (const auto& operation : request.operations) {
        const std::size_t components[]{operation.op.size(), operation.path.size(),
            operation.value_json ? operation.value_json->size() : 0, 48};
        for (const auto component : components) {
            if (component > impl_->limits.max_json_bytes
                || operation_bytes > impl_->limits.max_json_bytes - component)
                return {std::nullopt, ResourceStoreError::capacity};
            operation_bytes += component;
        }
        if (operation.value_json && !parse_json(*operation.value_json, bounds))
            return {std::nullopt, ResourceStoreError::invalid_data};
    }
    std::string serialized_operations;
    try { serialized_operations = operations_json(request.operations).dump(); }
    catch (...) { return {std::nullopt, ResourceStoreError::invalid_data}; }
    if (serialized_operations.size() > impl_->limits.max_json_bytes)
        return {std::nullopt, ResourceStoreError::capacity};
    ResourcePatchResult result;
    ResourceUpdate update;
    {
        std::lock_guard lock(impl_->mutex);
        const auto found = impl_->resources.find(request.key);
        if (found == impl_->resources.end()) return {std::nullopt, ResourceStoreError::not_found};
        const auto current = timestamp_value(found->second.timestamp_json, bounds);
        if (!current) return {std::nullopt, ResourceStoreError::internal_error};
        if (*expected < *current) {
            result = {ResourcePatchDisposition::conflict, found->second,
                      "Incoming patch is older than current snapshot"};
            return {std::move(result), ResourceStoreError::none};
        }
        auto document = parse_json(found->second.data_json, bounds);
        if (!document) return {std::nullopt, ResourceStoreError::internal_error};
        std::string error;
        for (const auto& operation : request.operations) {
            if (!apply_operation(*document, operation, bounds, error)) {
                result = {ResourcePatchDisposition::conflict, found->second, std::move(error)};
                return {std::move(result), ResourceStoreError::none};
            }
        }
        double now{};
        try { now = impl_->clock(); } catch (...) {
            return {std::nullopt, ResourceStoreError::internal_error};
        }
        if (!std::isfinite(now)) return {std::nullopt, ResourceStoreError::internal_error};
        ResourceSnapshot snapshot{timestamp_json(std::max(*expected, now)), document->dump()};
        if (snapshot.data_json.size() > impl_->limits.max_json_bytes)
            return {std::nullopt, ResourceStoreError::capacity};
        found->second = snapshot;
        result = {ResourcePatchDisposition::applied, snapshot, {}};
        update = {request.key, snapshot.timestamp_json, std::move(serialized_operations), "frontend"};
    }
    impl_->publish(std::move(update));
    return {std::move(result), ResourceStoreError::none};
}

ResourceSubscribeResult InMemoryResourceStore::subscribe_updates(UpdateCallback callback)
{
    if (!callback) return {nullptr, ResourceStoreError::invalid_data};
    const auto subscribers = impl_->subscribers;
    std::lock_guard lock(subscribers->mutex);
    if (!subscribers->active) return {nullptr, ResourceStoreError::internal_error};
    if (subscribers->callbacks.size() >= subscribers->maximum)
        return {nullptr, ResourceStoreError::capacity};
    const auto id = subscribers->next_id++;
    subscribers->callbacks.emplace(id, std::move(callback));
    return {std::make_unique<Impl::Subscription>(subscribers, id), ResourceStoreError::none};
}

bool InMemoryResourceStore::replace_and_publish(ResourceKey key, ResourceSnapshot snapshot,
                                                std::string operations, std::string origin)
{
    const JsonBounds bounds{impl_->limits.max_json_bytes, impl_->limits.max_json_depth, impl_->limits.max_json_nodes};
    if ((key.resource_id
         && (key.resource_id->size() > impl_->limits.max_resource_id_bytes
             || !auth::is_valid_utf8(*key.resource_id)))
        || origin.size() > impl_->limits.max_origin_bytes
        || !auth::is_valid_utf8(origin)) return false;
    const auto parsed_operations = parse_json(operations, bounds);
    if (!Impl::valid_snapshot(snapshot, bounds) || !parsed_operations
        || !parsed_operations->is_array()) return false;
    {
        std::lock_guard lock(impl_->mutex);
        const auto found = impl_->resources.find(key);
        if (found == impl_->resources.end()) return false;
        found->second = snapshot;
    }
    impl_->publish({std::move(key), snapshot.timestamp_json, std::move(operations), std::move(origin)});
    return true;
}

SyncHandlerFactory::SyncHandlerFactory(std::shared_ptr<ResourceStore> store, SyncHandlerLimits limits)
    : store_(std::move(store)), limits_(limits)
{
    if (!store_ || limits_.max_input_json_bytes == 0 || limits_.max_output_json_bytes == 0
        || limits_.max_json_depth == 0 || limits_.max_json_nodes == 0
        || limits_.max_in_flight_pushes == 0 || limits_.max_in_flight_push_bytes == 0
        || limits_.max_resource_id_bytes == 0 || limits_.max_origin_bytes == 0
        || limits_.max_error_bytes == 0
        || limits_.max_resource_id_bytes > limits_.max_output_json_bytes
        || limits_.max_origin_bytes > limits_.max_output_json_bytes
        || limits_.max_error_bytes > limits_.max_output_json_bytes)
        throw std::invalid_argument("invalid sync handler configuration");
}

ws::BusinessHandlerCreateResult SyncHandlerFactory::create(ws::BusinessSessionContext context,
                                                           std::shared_ptr<ws::BusinessPlaintextSink> output,
                                                           std::stop_token stop)
{
    if (!output || stop.stop_requested() || context.channel != auth::BusinessChannel::sync)
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    try {
        return {std::make_unique<SyncHandler>(store_, std::move(output), limits_),
                ws::BusinessHandlerCreateError::none};
    } catch (...) {
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    }
}

} // namespace baas::service::channels
