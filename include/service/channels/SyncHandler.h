#pragma once

#include "service/websocket/BusinessSessionFactory.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace baas::service::channels {

enum class SyncResource : std::uint8_t { config, event, gui, static_data, setup_toml };

struct ResourceKey {
    SyncResource resource{SyncResource::config};
    std::optional<std::string> resource_id;
    [[nodiscard]] bool operator==(const ResourceKey&) const = default;
};

struct ResourceSnapshot {
    std::string timestamp_json;
    std::string data_json;
};

struct ResourcePatchOperation {
    std::string op;
    std::string path;
    std::optional<std::string> value_json;
};

struct ResourcePatchRequest {
    ResourceKey key;
    std::string expected_timestamp_json;
    std::vector<ResourcePatchOperation> operations;
};

enum class ResourceStoreError : std::uint8_t {
    none,
    not_found,
    capacity,
    invalid_data,
    internal_error,
};

template <typename T>
struct ResourceStoreResult {
    std::optional<T> value;
    ResourceStoreError error{ResourceStoreError::none};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ResourceStoreError::none && value.has_value();
    }
    [[nodiscard]] T* operator->() noexcept { return std::addressof(*value); }
    [[nodiscard]] const T* operator->() const noexcept
    {
        return std::addressof(*value);
    }
};

enum class ResourcePatchDisposition : std::uint8_t { applied, conflict };

struct ResourcePatchResult {
    ResourcePatchDisposition disposition{ResourcePatchDisposition::applied};
    ResourceSnapshot snapshot;
    std::string error;
};

struct ResourceUpdate {
    ResourceKey key;
    std::string timestamp_json;
    std::string operations_json;
    std::string origin{"backend"};
};

class ResourceSubscription {
public:
    virtual ~ResourceSubscription() = default;
    ResourceSubscription(const ResourceSubscription&) = delete;
    ResourceSubscription& operator=(const ResourceSubscription&) = delete;
protected:
    ResourceSubscription() = default;
};

struct ResourceSubscribeResult {
    std::unique_ptr<ResourceSubscription> subscription;
    ResourceStoreError error{ResourceStoreError::none};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ResourceStoreError::none && subscription != nullptr;
    }
};

class ResourceStore {
public:
    using UpdateCallback = std::function<void(ResourceUpdate)>;
    virtual ~ResourceStore() = default;
    [[nodiscard]] virtual ResourceStoreResult<ResourceSnapshot> config_list(
        std::stop_token stop) = 0;
    [[nodiscard]] virtual ResourceStoreResult<ResourceSnapshot> pull(
        const ResourceKey& key, std::stop_token stop) = 0;
    [[nodiscard]] virtual ResourceStoreResult<ResourcePatchResult> apply_patch(
        ResourcePatchRequest request, std::stop_token stop) = 0;
    [[nodiscard]] virtual ResourceSubscribeResult subscribe_updates(
        UpdateCallback callback) = 0;
};

struct ResourceStoreLimits {
    std::size_t max_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_json_depth{64};
    std::size_t max_json_nodes{65'536};
    std::size_t max_resources{4'096};
    std::size_t max_subscribers{64};
    std::size_t max_patch_operations{1'024};
    std::size_t max_resource_id_bytes{256};
    std::size_t max_origin_bytes{64};
};

struct InitialResource {
    ResourceKey key;
    ResourceSnapshot snapshot;
};

class InMemoryResourceStore final : public ResourceStore {
public:
    using Clock = std::function<double()>;
    InMemoryResourceStore(
        std::vector<InitialResource> resources,
        ResourceSnapshot config_list,
        Clock clock,
        ResourceStoreLimits limits = {});
    ~InMemoryResourceStore() override;

    [[nodiscard]] ResourceStoreResult<ResourceSnapshot> config_list(
        std::stop_token stop) override;
    [[nodiscard]] ResourceStoreResult<ResourceSnapshot> pull(
        const ResourceKey& key, std::stop_token stop) override;
    [[nodiscard]] ResourceStoreResult<ResourcePatchResult> apply_patch(
        ResourcePatchRequest request, std::stop_token stop) override;
    [[nodiscard]] ResourceSubscribeResult subscribe_updates(
        UpdateCallback callback) override;

    // Injects a filesystem/backend update and publishes it after atomic commit.
    [[nodiscard]] bool replace_and_publish(
        ResourceKey key,
        ResourceSnapshot snapshot,
        std::string operations_json,
        std::string origin);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct SyncHandlerLimits {
    std::size_t max_input_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_output_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_json_depth{64};
    std::size_t max_json_nodes{65'536};
    std::size_t max_patch_operations{1'024};
    std::size_t max_resource_id_bytes{256};
    std::size_t max_origin_bytes{64};
    std::size_t max_error_bytes{4U * 1'024U};
    std::size_t max_in_flight_pushes{64};
    std::size_t max_in_flight_push_bytes{4U * 1'024U * 1'024U};
};

class SyncHandlerFactory final : public websocket::BusinessChannelHandlerFactory {
public:
    explicit SyncHandlerFactory(
        std::shared_ptr<ResourceStore> store,
        SyncHandlerLimits limits = {});
    [[nodiscard]] websocket::BusinessHandlerCreateResult create(
        websocket::BusinessSessionContext context,
        std::shared_ptr<websocket::BusinessPlaintextSink> output,
        std::stop_token stop) override;
private:
    std::shared_ptr<ResourceStore> store_;
    SyncHandlerLimits limits_;
};

}  // namespace baas::service::channels
