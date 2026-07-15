#include "service/trigger/TriggerDispatch.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace baas::service::trigger {
namespace {

using trigger_protocol::PublishError;

constexpr std::string_view exception_prefix = "handler exception: ";
constexpr std::string_view incomplete_handler =
    "handler returned without terminal response";

// Keep the dependency on TriggerIngressItem::admit_to() in one place so its
// result-policy evolution does not spread through the dispatcher.
[[nodiscard]] trigger_protocol::AdmissionResult admit_ingress_item(
    const trigger_protocol::TriggerIngressItem& item,
    trigger_protocol::TriggerSession& session)
{
    return session.admit(item.admission());
}

[[nodiscard]] TriggerResponseDisposition disposition_for(
    const PublishError error) noexcept
{
    using enum PublishError;
    if (error == queue_full || error == queued_bytes_exceeded)
        return TriggerResponseDisposition::retryable_backpressure;
    return TriggerResponseDisposition::close_session;
}

[[nodiscard]] std::string bounded_exception_message(
    const char* const message, const std::size_t max_bytes)
{
    std::string result;
    result.reserve(max_bytes);
    result.append(exception_prefix.substr(0, max_bytes));
    if (message == nullptr) return result;

    for (std::size_t index = 0;
         message[index] != '\0' && result.size() < max_bytes;
         ++index) {
        const auto byte = static_cast<unsigned char>(message[index]);
        // Exception text is diagnostic only. Restricting it to printable ASCII
        // provides a deterministic valid-UTF-8 boundary without scanning an
        // unbounded or malformed multibyte sequence.
        result.push_back(byte >= 0x20U && byte <= 0x7EU
            ? static_cast<char>(byte) : '?');
    }
    return result;
}

[[nodiscard]] const TriggerCommandDescriptor* descriptor_named(
    const std::string_view name) noexcept
{
    for (const auto& descriptor : TriggerCommandCatalog::rules()) {
        if (descriptor.canonical_name == name) return &descriptor;
    }
    return nullptr;
}

}  // namespace

std::string_view trigger_registry_error_name(const TriggerRegistryError error) noexcept
{
    using enum TriggerRegistryError;
    switch (error) {
        case none: return "none";
        case invalid_limits: return "invalid_limits";
        case unknown_descriptor: return "unknown_descriptor";
        case duplicate_registration: return "duplicate_registration";
        case empty_handler: return "empty_handler";
    }
    return "unknown";
}

std::string_view trigger_response_error_name(const TriggerResponseError error) noexcept
{
    using enum TriggerResponseError;
    switch (error) {
        case none: return "none";
        case progress_for_single: return "progress_for_single";
        case terminal_already_published: return "terminal_already_published";
        case response_pending: return "response_pending";
        case no_pending_response: return "no_pending_response";
        case internal_failure: return "internal_failure";
        case encode_rejected: return "encode_rejected";
        case publish_rejected: return "publish_rejected";
    }
    return "unknown";
}

std::string_view trigger_response_disposition_name(
    const TriggerResponseDisposition disposition) noexcept
{
    using enum TriggerResponseDisposition;
    switch (disposition) {
        case staged: return "staged";
        case published: return "published";
        case retryable_backpressure: return "retryable_backpressure";
        case close_session: return "close_session";
        case handler_may_retry: return "handler_may_retry";
    }
    return "unknown";
}

std::string_view trigger_dispatch_error_name(const TriggerDispatchError error) noexcept
{
    using enum TriggerDispatchError;
    switch (error) {
        case none: return "none";
        case unregistered_command: return "unregistered_command";
        case admission_rejected: return "admission_rejected";
        case handler_exception: return "handler_exception";
        case handler_returned_without_terminal:
            return "handler_returned_without_terminal";
        case internal_failure: return "internal_failure";
    }
    return "unknown";
}

std::string_view trigger_dispatch_disposition_name(
    const TriggerDispatchDisposition disposition) noexcept
{
    using enum TriggerDispatchDisposition;
    switch (disposition) {
        case rejected_before_admission: return "rejected_before_admission";
        case completed: return "completed";
        case retry_response: return "retry_response";
        case close_session: return "close_session";
    }
    return "unknown";
}

AdmittedTriggerRequest::AdmittedTriggerRequest(
    trigger_protocol::TriggerIngressItem item,
    trigger_protocol::TriggerSession& session,
    trigger_protocol::AdmissionReceipt receipt) noexcept
    : item_(std::move(item)), session_(&session), receipt_(std::move(receipt))
{}

std::string_view AdmittedTriggerRequest::command() const noexcept
{
    return item_.envelope().command;
}

trigger_protocol::Timestamp AdmittedTriggerRequest::timestamp() const noexcept
{
    return item_.envelope().timestamp;
}

trigger_protocol::ResponseMode AdmittedTriggerRequest::response_mode() const noexcept
{
    return item_.admission().response_mode;
}

const std::optional<std::string>& AdmittedTriggerRequest::config_id() const noexcept
{
    return item_.envelope().config_id;
}

std::string_view AdmittedTriggerRequest::payload_json() const noexcept
{
    return item_.envelope().payload_json;
}

bool AdmittedTriggerRequest::has_binary() const noexcept
{
    return item_.has_binary();
}

const std::optional<std::vector<std::byte>>&
AdmittedTriggerRequest::binary() const noexcept
{
    return item_.binary();
}

const TriggerCommandDescriptor& AdmittedTriggerRequest::descriptor() const noexcept
{
    return item_.descriptor();
}

PendingTriggerResponse::PendingTriggerResponse(
    trigger_protocol::TriggerSession& session,
    trigger_protocol::AdmissionReceipt receipt,
    trigger_protocol::OutboundBatch batch) noexcept
    : session_(&session),
      receipt_(std::move(receipt)),
      batch_(std::move(batch))
{}

TriggerResponseResult PendingTriggerResponse::retry()
{
    if (!batch_) {
        return {
            TriggerResponseError::no_pending_response,
            TriggerResponseDisposition::handler_may_retry,
        };
    }
    trigger_protocol::PublishResult published;
    try {
        published = session_->publish(receipt_, std::move(*batch_));
    } catch (...) {
        return {
            TriggerResponseError::internal_failure,
            TriggerResponseDisposition::close_session,
        };
    }
    if (!published) {
        return {
            TriggerResponseError::publish_rejected,
            disposition_for(published.error),
            trigger_protocol::EnvelopeError::none,
            published.error,
        };
    }
    batch_.reset();
    return {};
}

TriggerResponseSink::TriggerResponseSink(
    const AdmittedTriggerRequest& request,
    const TriggerDispatchLimits limits) noexcept
    : request_(request), limits_(limits)
{}

TriggerResponseResult TriggerResponseSink::progress(
    std::optional<std::string> data_json,
    std::optional<std::vector<std::byte>> binary)
{
    if (request_.response_mode() == trigger_protocol::ResponseMode::single) {
        last_result_ = {
            TriggerResponseError::progress_for_single,
            TriggerResponseDisposition::handler_may_retry,
        };
        return last_result_;
    }
    return publish(
        trigger_protocol::ResponseStatus::ok, false, std::move(data_json), {},
        std::move(binary));
}

TriggerResponseResult TriggerResponseSink::success(
    std::optional<std::string> data_json,
    std::optional<std::vector<std::byte>> binary)
{
    return publish(
        trigger_protocol::ResponseStatus::ok, true, std::move(data_json), {},
        std::move(binary));
}

TriggerResponseResult TriggerResponseSink::error(std::string message)
{
    return publish(
        trigger_protocol::ResponseStatus::error, true, std::nullopt,
        std::move(message), std::nullopt);
}

TriggerResponseResult TriggerResponseSink::cancelled(std::string message)
{
    return publish(
        trigger_protocol::ResponseStatus::cancelled, true, std::nullopt,
        std::move(message), std::nullopt);
}

TriggerResponseResult TriggerResponseSink::retry_pending()
{
    if (!pending_) {
        last_result_ = {
            TriggerResponseError::no_pending_response,
            TriggerResponseDisposition::handler_may_retry,
        };
        return last_result_;
    }
    last_result_ = pending_->retry();
    if (last_result_) {
        pending_.reset();
        terminal_committed_ = true;
    }
    return last_result_;
}

TriggerResponseResult TriggerResponseSink::publish(
    const trigger_protocol::ResponseStatus status,
    const bool terminal,
    std::optional<std::string> data_json,
    std::string error,
    std::optional<std::vector<std::byte>> binary)
{
    if (terminal_committed_ || staged_terminal_) {
        last_result_ = {
            TriggerResponseError::terminal_already_published,
            TriggerResponseDisposition::handler_may_retry,
        };
        return last_result_;
    }
    if (pending_) {
        last_result_ = {
            TriggerResponseError::response_pending,
            TriggerResponseDisposition::retryable_backpressure,
        };
        return last_result_;
    }

    trigger_protocol::EncodeResponseResult encoded;
    try {
        trigger_protocol::CommandResponse response;
        response.command = std::string{request_.command()};
        response.timestamp = request_.timestamp();
        response.status = status;
        response.response_mode = request_.response_mode();
        response.terminal = terminal;
        response.data_json = std::move(data_json);
        response.error = std::move(error);
        response.binary = std::move(binary);
        encoded = trigger_protocol::encode_command_response(
            std::move(response), limits_.response_envelope);
    } catch (...) {
        last_result_ = {
            TriggerResponseError::internal_failure,
            TriggerResponseDisposition::close_session,
        };
        return last_result_;
    }
    if (!encoded) {
        last_result_ = {
            TriggerResponseError::encode_rejected,
            TriggerResponseDisposition::handler_may_retry,
            encoded.error,
        };
        return last_result_;
    }

    if (terminal) {
        staged_terminal_.emplace(std::move(encoded.batch));
        last_result_ = {
            TriggerResponseError::none,
            TriggerResponseDisposition::staged,
        };
        return last_result_;
    }

    trigger_protocol::PublishResult published;
    try {
        published = request_.session_->publish(
            request_.receipt_, std::move(encoded.batch));
    } catch (...) {
        last_result_ = {
            TriggerResponseError::internal_failure,
            TriggerResponseDisposition::close_session,
        };
        return last_result_;
    }
    if (!published) {
        const auto disposition = disposition_for(published.error);
        last_result_ = {
            TriggerResponseError::publish_rejected,
            disposition,
            trigger_protocol::EnvelopeError::none,
            published.error,
        };
        return last_result_;
    }

    last_result_ = {};
    return last_result_;
}

std::optional<PendingTriggerResponse> TriggerResponseSink::take_pending() noexcept
{
    return std::move(pending_);
}

void TriggerResponseSink::discard_staged_terminal() noexcept
{
    staged_terminal_.reset();
}

TriggerResponseResult TriggerResponseSink::commit_staged_terminal()
{
    if (!staged_terminal_) {
        last_result_ = {
            TriggerResponseError::no_pending_response,
            TriggerResponseDisposition::close_session,
        };
        return last_result_;
    }
    trigger_protocol::PublishResult published;
    try {
        published = request_.session_->publish(
            request_.receipt_, std::move(*staged_terminal_));
    } catch (...) {
        staged_terminal_.reset();
        last_result_ = {
            TriggerResponseError::internal_failure,
            TriggerResponseDisposition::close_session,
        };
        return last_result_;
    }
    if (!published) {
        const auto disposition = disposition_for(published.error);
        if (disposition == TriggerResponseDisposition::retryable_backpressure) {
            pending_.emplace(PendingTriggerResponse{
                *request_.session_, request_.receipt_,
                std::move(*staged_terminal_)});
        }
        staged_terminal_.reset();
        last_result_ = {
            TriggerResponseError::publish_rejected,
            disposition,
            trigger_protocol::EnvelopeError::none,
            published.error,
        };
        return last_result_;
    }
    staged_terminal_.reset();
    terminal_committed_ = true;
    last_result_ = {};
    return last_result_;
}

TriggerDispatcherBuildResult TriggerDispatcher::create(
    std::vector<TriggerHandlerRegistration> registrations,
    const TriggerDispatchLimits limits)
{
    if (!trigger_protocol::valid_trigger_envelope_limits(limits.response_envelope)
        || limits.max_exception_error_bytes < exception_prefix.size()
        || limits.max_exception_error_bytes
            > limits.response_envelope.max_string_bytes) {
        return {std::nullopt, TriggerRegistryError::invalid_limits, {}};
    }

    std::vector<ResolvedHandler> resolved;
    resolved.reserve(registrations.size());
    for (auto& registration : registrations) {
        const auto* const descriptor = descriptor_named(registration.descriptor_name);
        if (descriptor == nullptr) {
            return {
                std::nullopt, TriggerRegistryError::unknown_descriptor,
                std::move(registration.descriptor_name)};
        }
        if (!registration.handler) {
            return {
                std::nullopt, TriggerRegistryError::empty_handler,
                std::move(registration.descriptor_name)};
        }
        if (std::any_of(
                resolved.begin(), resolved.end(),
                [descriptor](const ResolvedHandler& existing) {
                    return existing.descriptor == descriptor;
                })) {
            return {
                std::nullopt, TriggerRegistryError::duplicate_registration,
                std::move(registration.descriptor_name)};
        }
        resolved.push_back({descriptor, std::move(registration.handler)});
    }
    return {
        TriggerDispatcher{std::move(resolved), limits},
        TriggerRegistryError::none,
        {},
    };
}

const TriggerHandler* TriggerDispatcher::find_handler(
    const TriggerCommandDescriptor& descriptor) const noexcept
{
    const auto iterator = std::find_if(
        handlers_.begin(), handlers_.end(),
        [&descriptor](const ResolvedHandler& handler) {
            return handler.descriptor == &descriptor;
        });
    return iterator == handlers_.end() ? nullptr : &iterator->handler;
}

TriggerDispatchResult TriggerDispatcher::submit(
    trigger_protocol::TriggerIngressItem item,
    trigger_protocol::TriggerSession& session) const
{
    // Handler resolution is deliberately before admission: an unregistered
    // descriptor can never reserve a timestamp/correlation.
    const auto* const handler = find_handler(item.descriptor());
    if (handler == nullptr) {
        return {
            TriggerDispatchError::unregistered_command,
            TriggerDispatchDisposition::rejected_before_admission,
        };
    }

    auto admission = admit_ingress_item(item, session);
    if (!admission || !admission.receipt) {
        return {
            TriggerDispatchError::admission_rejected,
            TriggerDispatchDisposition::rejected_before_admission,
            admission.error,
        };
    }

    AdmittedTriggerRequest request{
        std::move(item), session, std::move(*admission.receipt)};
    TriggerResponseSink sink{request, limits_};
    try {
        TriggerDispatchError dispatch_error = TriggerDispatchError::none;
        TriggerResponseResult boundary_response{};
        try {
            (*handler)(request, sink);
        } catch (const std::exception& exception) {
            dispatch_error = TriggerDispatchError::handler_exception;
            sink.discard_staged_terminal();
            boundary_response = sink.error(bounded_exception_message(
                exception.what(), limits_.max_exception_error_bytes));
        } catch (...) {
            dispatch_error = TriggerDispatchError::handler_exception;
            sink.discard_staged_terminal();
            boundary_response = sink.error(bounded_exception_message(
                "non-standard exception", limits_.max_exception_error_bytes));
        }

        if (dispatch_error == TriggerDispatchError::none
            && !sink.staged_terminal_) {
            dispatch_error = TriggerDispatchError::handler_returned_without_terminal;
            boundary_response = sink.error(std::string{incomplete_handler});
        }

        if (sink.staged_terminal_)
            boundary_response = sink.commit_staged_terminal();

        if (sink.terminal_published()) {
            return {
                dispatch_error,
                TriggerDispatchDisposition::completed,
                trigger_protocol::AdmissionError::none,
                boundary_response,
                std::nullopt,
            };
        }
        if (sink.has_pending_terminal()) {
            return {
                dispatch_error,
                TriggerDispatchDisposition::retry_response,
                trigger_protocol::AdmissionError::none,
                sink.last_result_,
                sink.take_pending(),
            };
        }
        return {
            dispatch_error,
            TriggerDispatchDisposition::close_session,
            trigger_protocol::AdmissionError::none,
            boundary_response,
            std::nullopt,
        };
    } catch (...) {
        return {
            TriggerDispatchError::internal_failure,
            TriggerDispatchDisposition::close_session,
            trigger_protocol::AdmissionError::none,
            TriggerResponseResult{
                TriggerResponseError::internal_failure,
                TriggerResponseDisposition::close_session,
            },
            std::nullopt,
        };
    }
}

}  // namespace baas::service::trigger
