#pragma once

#include "resources/ResourceSnapshot.h"
#include "script/runtime/SynchronousHost.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace baas::script::host {

inline constexpr std::uint64_t resource_release_adapter_id =
    UINT64_C(0x4241415352455301); // "BAASRES" + ABI 1

struct ResourceHostLimits {
    std::size_t max_open_handles{65'536};
    std::size_t max_single_read_bytes{64U * 1024U * 1024U};
    std::size_t max_total_read_bytes{512U * 1024U * 1024U};
    std::size_t cooperative_chunk_bytes{64U * 1024U};
};

struct ResourceHostStats {
    std::size_t open_handles{};
    std::size_t resolved_handles{};
    std::size_t released_handles{};
    std::size_t read_calls{};
    std::size_t read_bytes{};
};

struct ResourceHostRuntime;

class ResourceHost final {
public:
    ~ResourceHost();
    ResourceHost(const ResourceHost&) = delete;
    ResourceHost& operator=(const ResourceHost&) = delete;

    [[nodiscard]] const std::shared_ptr<const resources::ResourceSnapshot>&
        snapshot() const noexcept;
    [[nodiscard]] ResourceHostStats stats() const noexcept;

private:
    friend struct ResourceHostRuntime;
    friend ResourceHostRuntime make_resource_host_runtime(
        std::shared_ptr<const resources::ResourceSnapshot>, ResourceHostLimits);
    struct Impl;
    explicit ResourceHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct ResourceHostRuntime {
    std::shared_ptr<ResourceHost> host;
    std::shared_ptr<runtime::HostReleaseDispatcher> handles;
    std::shared_ptr<const runtime::HostModuleRegistry> metadata;
    std::shared_ptr<const runtime::SynchronousNativeBindingSet> bindings;
};

[[nodiscard]] ResourceHostRuntime make_resource_host_runtime(
    std::shared_ptr<const resources::ResourceSnapshot> snapshot,
    ResourceHostLimits limits = {});

}  // namespace baas::script::host
