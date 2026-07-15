#pragma once

#include "service/protocol/TriggerIngress.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::trigger {

namespace trigger_protocol = baas::service::protocol::trigger;

class TriggerExecutor;

struct TriggerDispatchLimits {
    trigger_protocol::TriggerEnvelopeLimits response_envelope{};
    std::size_t max_exception_error_bytes{1'024};
};

enum class TriggerRegistryError : std::uint8_t {
    none,
    invalid_limits,
    unknown_descriptor,
    duplicate_registration,
    empty_handler,
};

[[nodiscard]] std::string_view trigger_registry_error_name(
    TriggerRegistryError error) noexcept;

enum class TriggerResponseError : std::uint8_t {
    none,
    progress_for_single,
    terminal_already_published,
    response_pending,
    no_pending_response,
    internal_failure,
    encode_rejected,
    publish_rejected,
};

enum class TriggerResponseDisposition : std::uint8_t {
    staged,
    published,
    retryable_backpressure,
    close_session,
    handler_may_retry,
};

[[nodiscard]] std::string_view trigger_response_error_name(
    TriggerResponseError error) noexcept;
[[nodiscard]] std::string_view trigger_response_disposition_name(
    TriggerResponseDisposition disposition) noexcept;

struct TriggerResponseResult {
    TriggerResponseError error{TriggerResponseError::none};
    TriggerResponseDisposition disposition{TriggerResponseDisposition::published};
    trigger_protocol::EnvelopeError envelope_error{
        trigger_protocol::EnvelopeError::none};
    trigger_protocol::PublishError publish_error{
        trigger_protocol::PublishError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TriggerResponseError::none;
    }
};

// Immutable admitted identity. It owns the exact ingress item and is bound to
// the session that minted its AdmissionReceipt. Handlers receive only const
// access and cannot select command, timestamp, response mode, or session.
class AdmittedTriggerRequest final {
public:
    AdmittedTriggerRequest(const AdmittedTriggerRequest&) = delete;
    AdmittedTriggerRequest& operator=(const AdmittedTriggerRequest&) = delete;
    AdmittedTriggerRequest(AdmittedTriggerRequest&&) noexcept = default;
    AdmittedTriggerRequest& operator=(AdmittedTriggerRequest&&) noexcept = default;

    [[nodiscard]] std::string_view command() const noexcept;
    [[nodiscard]] trigger_protocol::Timestamp timestamp() const noexcept;
    [[nodiscard]] trigger_protocol::ResponseMode response_mode() const noexcept;
    [[nodiscard]] const std::optional<std::string>& config_id() const noexcept;
    [[nodiscard]] std::string_view payload_json() const noexcept;
    [[nodiscard]] bool has_binary() const noexcept;
    [[nodiscard]] const std::optional<std::vector<std::byte>>& binary() const noexcept;
    [[nodiscard]] const TriggerCommandDescriptor& descriptor() const noexcept;

private:
    AdmittedTriggerRequest(
        trigger_protocol::TriggerIngressItem item,
        trigger_protocol::TriggerSession& session,
        trigger_protocol::AdmissionReceipt receipt) noexcept;

    friend class TriggerDispatcher;
    friend class TriggerResponseSink;
    friend class TriggerExecutor;

    trigger_protocol::TriggerIngressItem item_;
    trigger_protocol::TriggerSession* session_{};
    trigger_protocol::AdmissionReceipt receipt_;
};

class PendingTriggerResponse final {
public:
    PendingTriggerResponse(const PendingTriggerResponse&) = delete;
    PendingTriggerResponse& operator=(const PendingTriggerResponse&) = delete;
    PendingTriggerResponse(PendingTriggerResponse&&) noexcept = default;
    PendingTriggerResponse& operator=(PendingTriggerResponse&&) noexcept = default;

    [[nodiscard]] TriggerResponseResult retry();
    [[nodiscard]] bool pending() const noexcept { return batch_.has_value(); }
    [[nodiscard]] std::size_t bytes() const noexcept;
    [[nodiscard]] bool replace_with_cancelled() noexcept;
    [[nodiscard]] bool cancellation_replaceable() const noexcept
    {
        return !irrevocable_;
    }

private:
    PendingTriggerResponse(
        trigger_protocol::TriggerSession& session,
        trigger_protocol::AdmissionReceipt receipt,
        trigger_protocol::OutboundBatch batch,
        std::optional<trigger_protocol::OutboundBatch> cancelled_fallback,
        bool irrevocable) noexcept;

    friend class TriggerResponseSink;
    friend class TriggerDispatcher;
    friend class TriggerExecutor;

    trigger_protocol::TriggerSession* session_{};
    trigger_protocol::AdmissionReceipt receipt_;
    std::optional<trigger_protocol::OutboundBatch> batch_;
    std::optional<trigger_protocol::OutboundBatch> cancelled_fallback_;
    bool irrevocable_{};
};

// Synchronous handler-scoped publisher. Identity fields are always copied from
// AdmittedTriggerRequest; handlers supply only result data/error/binary.
class TriggerResponseSink final {
public:
    TriggerResponseSink(const TriggerResponseSink&) = delete;
    TriggerResponseSink& operator=(const TriggerResponseSink&) = delete;
    TriggerResponseSink(TriggerResponseSink&&) = delete;
    TriggerResponseSink& operator=(TriggerResponseSink&&) = delete;

    [[nodiscard]] TriggerResponseResult progress(
        std::optional<std::string> data_json = std::nullopt,
        std::optional<std::vector<std::byte>> binary = std::nullopt);
    [[nodiscard]] TriggerResponseResult success(
        std::optional<std::string> data_json = std::nullopt,
        std::optional<std::vector<std::byte>> binary = std::nullopt);
    // Encodes the terminal first, then atomically closes the Session's
    // cancellation window without publishing it. Use only immediately before
    // an irreversible side-effect commit.
    [[nodiscard]] TriggerResponseResult irrevocable_success(
        std::optional<std::string> data_json = std::nullopt,
        std::optional<std::vector<std::byte>> binary = std::nullopt);
    // Replaces a prepared irrevocable success when the side-effect commit did
    // not happen. Cancellation remains closed, but no success is reported.
    [[nodiscard]] TriggerResponseResult irrevocable_error(std::string message);
    [[nodiscard]] TriggerResponseResult error(std::string message);
    [[nodiscard]] TriggerResponseResult cancelled(
        std::string message = "cancelled");
    [[nodiscard]] TriggerResponseResult retry_pending();

    [[nodiscard]] bool terminal_published() const noexcept
    {
        return terminal_committed_;
    }
    [[nodiscard]] bool has_pending_terminal() const noexcept
    {
        return pending_.has_value() && pending_->pending();
    }
    [[nodiscard]] bool irrevocable_terminal_claimed() const noexcept
    {
        return irrevocable_terminal_claimed_;
    }

private:
    TriggerResponseSink(
        const AdmittedTriggerRequest& request,
        TriggerDispatchLimits limits) noexcept;

    [[nodiscard]] TriggerResponseResult publish(
        trigger_protocol::ResponseStatus status,
        bool terminal,
        std::optional<std::string> data_json,
        std::string error,
        std::optional<std::vector<std::byte>> binary);
    [[nodiscard]] std::optional<PendingTriggerResponse> take_pending() noexcept;
    void discard_staged_terminal() noexcept;
    [[nodiscard]] TriggerResponseResult commit_staged_terminal();

    friend class TriggerDispatcher;

    const AdmittedTriggerRequest& request_;
    TriggerDispatchLimits limits_;
    bool terminal_committed_{};
    bool irrevocable_terminal_claimed_{};
    std::optional<trigger_protocol::OutboundBatch> staged_terminal_;
    std::optional<PendingTriggerResponse> pending_;
    TriggerResponseResult last_result_{};
};

using TriggerHandler = std::function<void(
    const AdmittedTriggerRequest&, TriggerResponseSink&, std::stop_token)>;

struct TriggerHandlerRegistration {
    // Must exactly equal a catalog descriptor canonical_name. Prefix
    // descriptors retain their trailing '*'; actual commands remain untouched.
    std::string descriptor_name;
    TriggerHandler handler;
};

enum class TriggerDispatchError : std::uint8_t {
    none,
    unregistered_command,
    admission_rejected,
    handler_exception,
    handler_returned_without_terminal,
    internal_failure,
};

enum class TriggerDispatchDisposition : std::uint8_t {
    rejected_before_admission,
    completed,
    retry_response,
    close_session,
};

[[nodiscard]] std::string_view trigger_dispatch_error_name(
    TriggerDispatchError error) noexcept;
[[nodiscard]] std::string_view trigger_dispatch_disposition_name(
    TriggerDispatchDisposition disposition) noexcept;

struct TriggerDispatchResult {
    TriggerDispatchError error{TriggerDispatchError::none};
    TriggerDispatchDisposition disposition{
        TriggerDispatchDisposition::rejected_before_admission};
    trigger_protocol::AdmissionError admission_error{
        trigger_protocol::AdmissionError::none};
    TriggerResponseResult response{};
    std::optional<PendingTriggerResponse> pending_response;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TriggerDispatchError::none
            && disposition == TriggerDispatchDisposition::completed;
    }
};

struct TriggerDispatcherBuildResult;

// Immutable after create(). TriggerExecutor is the only execution owner allowed
// to turn a resolved ingress item into an admitted handler invocation. This
// keeps capacity reservation and task registration ahead of worker execution.
class TriggerDispatcher final {
public:
    TriggerDispatcher(const TriggerDispatcher&) = delete;
    TriggerDispatcher& operator=(const TriggerDispatcher&) = delete;
    TriggerDispatcher(TriggerDispatcher&&) noexcept = default;
    TriggerDispatcher& operator=(TriggerDispatcher&&) noexcept = default;

    [[nodiscard]] static TriggerDispatcherBuildResult create(
        std::vector<TriggerHandlerRegistration> registrations,
        TriggerDispatchLimits limits = {});

private:
    struct ResolvedHandler {
        const TriggerCommandDescriptor* descriptor{};
        TriggerHandler handler;
    };

    TriggerDispatcher(
        std::vector<ResolvedHandler> handlers,
        TriggerDispatchLimits limits) noexcept
        : handlers_(std::move(handlers)), limits_(limits)
    {}

    [[nodiscard]] const TriggerHandler* find_handler(
        const TriggerCommandDescriptor& descriptor) const noexcept;
    [[nodiscard]] TriggerDispatchResult execute_admitted(
        const TriggerHandler& handler,
        const AdmittedTriggerRequest& request,
        std::stop_token stop_token) const;

    friend class TriggerExecutor;

    std::vector<ResolvedHandler> handlers_;
    TriggerDispatchLimits limits_;
};

struct TriggerDispatcherBuildResult {
    std::optional<TriggerDispatcher> dispatcher;
    TriggerRegistryError error{TriggerRegistryError::none};
    std::string descriptor_name;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TriggerRegistryError::none && dispatcher.has_value();
    }
};

}  // namespace baas::service::trigger
