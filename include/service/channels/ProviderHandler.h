#pragma once

#include "service/websocket/BusinessSessionFactory.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace baas::service::channels {

struct ProviderHandlerLimits {
    std::size_t max_input_json_bytes{64U * 1'024U};
    std::size_t max_output_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_json_depth{64};
    std::size_t max_json_nodes{65'536};
    std::size_t max_pending_pushes{256};
    std::size_t max_pending_push_bytes{4U * 1'024U * 1'024U};
};

enum class ProviderBackendError : std::uint8_t {
    none,
    capacity,
    internal_error,
};

template <typename T>
struct ProviderBackendResult {
    std::optional<T> value;
    ProviderBackendError error{ProviderBackendError::none};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ProviderBackendError::none && value.has_value();
    }
    [[nodiscard]] T* operator->() noexcept { return std::addressof(*value); }
    [[nodiscard]] const T* operator->() const noexcept
    {
        return std::addressof(*value);
    }
};

struct ProviderLogsFull {
    // Validated as JSON arrays by the handler and embedded without rewriting.
    std::string scopes_json;
    std::string entries_json;
};

struct ProviderStaticSnapshot {
    // timestamp_json is a JSON number; data_json may be any JSON value.
    std::string timestamp_json;
    std::string data_json;
};

class ProviderSubscription {
public:
    virtual ~ProviderSubscription() = default;
    ProviderSubscription(const ProviderSubscription&) = delete;
    ProviderSubscription& operator=(const ProviderSubscription&) = delete;
protected:
    ProviderSubscription() = default;
};

struct ProviderSubscribeResult {
    std::unique_ptr<ProviderSubscription> subscription;
    ProviderBackendError error{ProviderBackendError::none};
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ProviderBackendError::none && subscription != nullptr;
    }
};

// Implementations are thread-safe. Snapshot methods may run concurrently with
// callbacks. A subscription destructor must stop future callback entry; a
// callback already in progress may finish before destruction returns.
class ProviderBackend {
public:
    using PushCallback = std::function<void(std::string payload_json)>;
    virtual ~ProviderBackend() = default;

    [[nodiscard]] virtual ProviderBackendResult<ProviderLogsFull> logs_full(
        std::stop_token stop) = 0;
    [[nodiscard]] virtual ProviderBackendResult<std::string> status(
        std::stop_token stop) = 0;
    [[nodiscard]] virtual ProviderBackendResult<std::optional<bool>>
        all_data_initialized(std::stop_token stop) = 0;
    [[nodiscard]] virtual ProviderBackendResult<ProviderStaticSnapshot>
        static_snapshot(std::stop_token stop) = 0;
    [[nodiscard]] virtual ProviderSubscribeResult subscribe_logs(
        PushCallback callback) = 0;
    [[nodiscard]] virtual ProviderSubscribeResult subscribe_status(
        PushCallback callback) = 0;
};

class ProviderHandlerFactory final
    : public websocket::BusinessChannelHandlerFactory {
public:
    explicit ProviderHandlerFactory(
        std::shared_ptr<ProviderBackend> backend,
        ProviderHandlerLimits limits = {});

    [[nodiscard]] websocket::BusinessHandlerCreateResult create(
        websocket::BusinessSessionContext context,
        std::shared_ptr<websocket::BusinessPlaintextSink> output,
        std::stop_token stop) override;

private:
    std::shared_ptr<ProviderBackend> backend_;
    ProviderHandlerLimits limits_;
};

}  // namespace baas::service::channels
