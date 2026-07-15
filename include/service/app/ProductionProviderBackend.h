#pragma once

#include "service/channels/ProviderHandler.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace baas::service::app {

struct ProductionProviderBackendLimits {
    std::size_t max_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_json_depth{64};
    std::size_t max_json_nodes{65'536};
    std::size_t max_scope_bytes{256};
    std::size_t max_scopes{4'096};
    std::size_t max_history_entries{4'096};
    std::size_t max_history_bytes{768U * 1'024U};
    // Exact scopes_json.size() + entries_json.size() after every publication.
    std::size_t max_snapshot_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_subscriptions_per_stream{64};
};

// Thread-safe production state owner for the provider business channel. Log
// entries are strict JSON objects with a non-empty string `scope`. Accepted
// payload bytes are retained verbatim so floating-point and extension fields
// preserve their wire spelling.
class ProductionProviderBackend final : public channels::ProviderBackend {
public:
    explicit ProductionProviderBackend(
        ProductionProviderBackendLimits limits = {});
    ~ProductionProviderBackend() override;

    ProductionProviderBackend(const ProductionProviderBackend&) = delete;
    ProductionProviderBackend& operator=(const ProductionProviderBackend&) = delete;
    ProductionProviderBackend(ProductionProviderBackend&&) = delete;
    ProductionProviderBackend& operator=(ProductionProviderBackend&&) = delete;

    [[nodiscard]] channels::ProviderBackendResult<channels::ProviderLogsFull>
        logs_full(std::stop_token stop) override;
    [[nodiscard]] channels::ProviderBackendResult<std::string>
        status(std::stop_token stop) override;
    [[nodiscard]] channels::ProviderBackendResult<std::optional<bool>>
        all_data_initialized(std::stop_token stop) override;
    [[nodiscard]] channels::ProviderBackendResult<channels::ProviderStaticSnapshot>
        static_snapshot(std::stop_token stop) override;
    [[nodiscard]] channels::ProviderSubscribeResult subscribe_logs(
        PushCallback callback) override;
    [[nodiscard]] channels::ProviderSubscribeResult subscribe_status(
        PushCallback callback) override;

    // Invalid JSON returns internal_error. A valid payload that exceeds a
    // configured byte/scope/snapshot limit returns capacity. No state or
    // callback is committed on either failure.
    [[nodiscard]] channels::ProviderBackendError publish_log(
        std::string entry_json) noexcept;
    [[nodiscard]] channels::ProviderBackendError publish_status(
        std::string status_json) noexcept;
    [[nodiscard]] channels::ProviderBackendError set_initialized(
        std::optional<bool> initialized) noexcept;
    [[nodiscard]] channels::ProviderBackendError replace_static(
        std::string timestamp_json, std::string data_json) noexcept;

    [[nodiscard]] const ProductionProviderBackendLimits& limits() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace baas::service::app
