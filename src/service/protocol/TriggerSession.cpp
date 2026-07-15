#include "service/protocol/TriggerSession.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace baas::service::protocol::trigger {
namespace {

[[nodiscard]] bool is_continuation(const unsigned char byte) noexcept
{
    return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    std::size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1 >= value.size()
                || !is_continuation(static_cast<unsigned char>(value[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU) {
            if (index + 2 >= value.size()) return false;
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            if (!is_continuation(second) || !is_continuation(third)) return false;
            if ((first == 0xE0U && second < 0xA0U)
                || (first == 0xEDU && second >= 0xA0U)) {
                return false;
            }
            index += 3;
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U) {
            if (index + 3 >= value.size()) return false;
            const auto second = static_cast<unsigned char>(value[index + 1]);
            const auto third = static_cast<unsigned char>(value[index + 2]);
            const auto fourth = static_cast<unsigned char>(value[index + 3]);
            if (!is_continuation(second) || !is_continuation(third)
                || !is_continuation(fourth)) {
                return false;
            }
            if ((first == 0xF0U && second < 0x90U)
                || (first == 0xF4U && second > 0x8FU)) {
                return false;
            }
            index += 4;
            continue;
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool is_command_name(const std::string_view value) noexcept
{
    if (value.empty() || value.front() < 'a' || value.front() > 'z') return false;
    return std::all_of(value.begin() + 1, value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z')
            || (character >= '0' && character <= '9') || character == '_';
    });
}

[[nodiscard]] bool checked_add(
    const std::size_t left, const std::size_t right, std::size_t& result
) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    result = left + right;
    return true;
}

void validate_limits(const TriggerSessionLimits& limits)
{
    if (limits.max_in_flight == 0 || limits.max_command_bytes == 0
        || limits.max_config_id_bytes == 0 || limits.max_request_payload_bytes == 0
        || limits.max_request_binary_bytes == 0 || limits.max_response_json_bytes == 0
        || limits.max_response_binary_bytes == 0 || limits.max_queued_batches == 0
        || limits.max_queued_bytes == 0) {
        throw std::invalid_argument("trigger session limits must be positive");
    }
    if (limits.max_response_json_bytes > limits.max_queued_bytes
        || limits.max_response_binary_bytes > limits.max_queued_bytes) {
        throw std::invalid_argument(
            "individual trigger response limits must fit the queued byte budget");
    }
}

}  // namespace

std::string_view admission_error_name(const AdmissionError error) noexcept
{
    using enum AdmissionError;
    switch (error) {
        case none: return "none";
        case closed: return "closed";
        case invalid_command: return "invalid_command";
        case invalid_timestamp: return "invalid_timestamp";
        case invalid_config_id: return "invalid_config_id";
        case payload_too_large: return "payload_too_large";
        case binary_too_large: return "binary_too_large";
        case duplicate_timestamp: return "duplicate_timestamp";
        case in_flight_limit: return "in_flight_limit";
    }
    return "unknown";
}

std::string_view publish_error_name(const PublishError error) noexcept
{
    using enum PublishError;
    switch (error) {
        case none: return "none";
        case closed: return "closed";
        case unknown_timestamp: return "unknown_timestamp";
        case command_mismatch: return "command_mismatch";
        case response_mode_mismatch: return "response_mode_mismatch";
        case terminal_already_queued: return "terminal_already_queued";
        case single_response_must_be_terminal: return "single_response_must_be_terminal";
        case error_response_must_be_terminal: return "error_response_must_be_terminal";
        case cancellation_response_required: return "cancellation_response_required";
        case invalid_json_utf8: return "invalid_json_utf8";
        case json_too_large: return "json_too_large";
        case binary_too_large: return "binary_too_large";
        case queue_full: return "queue_full";
        case queued_bytes_exceeded: return "queued_bytes_exceeded";
    }
    return "unknown";
}

std::string_view begin_send_error_name(const BeginSendError error) noexcept
{
    using enum BeginSendError;
    switch (error) {
        case none: return "none";
        case closed: return "closed";
        case queue_empty: return "queue_empty";
        case send_in_progress: return "send_in_progress";
    }
    return "unknown";
}

std::string_view send_transition_error_name(const SendTransitionError error) noexcept
{
    using enum SendTransitionError;
    switch (error) {
        case none: return "none";
        case closed: return "closed";
        case no_active_lease: return "no_active_lease";
        case lease_mismatch: return "lease_mismatch";
    }
    return "unknown";
}

TriggerSession::TriggerSession(const TriggerSessionLimits limits) : limits_(limits)
{
    validate_limits(limits_);
}

AdmissionResult TriggerSession::admit(CommandAdmission command)
{
    const auto reject = [this](const AdmissionError error) {
        std::lock_guard lock(mutex_);
        ++admission_rejections_;
        return AdmissionResult{error};
    };

    if (command.command.size() > limits_.max_command_bytes
        || !is_command_name(command.command)) {
        return reject(AdmissionError::invalid_command);
    }
    if (command.timestamp > maximum_safe_timestamp)
        return reject(AdmissionError::invalid_timestamp);
    if (command.config_id
        && (command.config_id->empty()
            || command.config_id->size() > limits_.max_config_id_bytes
            || !is_valid_utf8(*command.config_id))) {
        return reject(AdmissionError::invalid_config_id);
    }
    if (command.payload_bytes > limits_.max_request_payload_bytes)
        return reject(AdmissionError::payload_too_large);
    if (command.binary_bytes > limits_.max_request_binary_bytes)
        return reject(AdmissionError::binary_too_large);

    std::lock_guard lock(mutex_);
    if (closed_) {
        ++admission_rejections_;
        return {AdmissionError::closed};
    }
    if (entries_.contains(command.timestamp)) {
        ++admission_rejections_;
        return {AdmissionError::duplicate_timestamp};
    }
    if (entries_.size() >= limits_.max_in_flight) {
        ++admission_rejections_;
        return {AdmissionError::in_flight_limit};
    }
    entries_.emplace(
        command.timestamp,
        Entry{std::move(command.command), command.response_mode, false, false});
    ++accepted_;
    return {};
}

CancelDecision TriggerSession::request_cancel(const Timestamp timestamp)
{
    std::lock_guard lock(mutex_);
    if (closed_) return CancelDecision::closed;
    const auto iterator = entries_.find(timestamp);
    if (iterator == entries_.end()) return CancelDecision::unknown_timestamp;
    if (iterator->second.terminal_queued)
        return CancelDecision::terminal_already_queued;
    if (iterator->second.cancel_requested)
        return CancelDecision::already_requested;
    iterator->second.cancel_requested = true;
    ++cancellations_requested_;
    return CancelDecision::requested;
}

std::size_t TriggerSession::batch_bytes(const OutboundBatch& batch) noexcept
{
    std::size_t result{};
    if (!checked_add(batch.json().size(), batch.binary().size(), result))
        return std::numeric_limits<std::size_t>::max();
    return result;
}

PublishResult TriggerSession::publish(OutboundBatch batch)
{
    if (batch.json().empty() || !is_valid_utf8(batch.json())) {
        std::lock_guard lock(mutex_);
        ++publish_rejections_;
        return {PublishError::invalid_json_utf8};
    }
    if (batch.json().size() > limits_.max_response_json_bytes) {
        std::lock_guard lock(mutex_);
        ++publish_rejections_;
        return {PublishError::json_too_large};
    }
    if (batch.binary().size() > limits_.max_response_binary_bytes) {
        std::lock_guard lock(mutex_);
        ++publish_rejections_;
        return {PublishError::binary_too_large};
    }
    const auto bytes = batch_bytes(batch);

    std::lock_guard lock(mutex_);
    const auto reject = [this](const PublishError error) {
        ++publish_rejections_;
        if (error == PublishError::queue_full
            || error == PublishError::queued_bytes_exceeded) {
            ++queue_backpressure_;
        }
        return PublishResult{error};
    };
    if (closed_) return reject(PublishError::closed);
    const auto iterator = entries_.find(batch.timestamp());
    if (iterator == entries_.end()) return reject(PublishError::unknown_timestamp);
    auto& entry = iterator->second;
    if (entry.command != batch.command()) return reject(PublishError::command_mismatch);
    if (entry.response_mode != batch.response_mode())
        return reject(PublishError::response_mode_mismatch);
    if (entry.terminal_queued) return reject(PublishError::terminal_already_queued);
    if (entry.response_mode == ResponseMode::single && !batch.terminal())
        return reject(PublishError::single_response_must_be_terminal);
    if (batch.status() != ResponseStatus::ok && !batch.terminal())
        return reject(PublishError::error_response_must_be_terminal);
    if (entry.cancel_requested && batch.status() != ResponseStatus::cancelled)
        return reject(PublishError::cancellation_response_required);
    if (outbound_.size() >= limits_.max_queued_batches)
        return reject(PublishError::queue_full);
    if (bytes > limits_.max_queued_bytes
        || queued_bytes_ > limits_.max_queued_bytes - bytes) {
        return reject(PublishError::queued_bytes_exceeded);
    }

    outbound_.push_back(std::make_shared<const OutboundBatch>(std::move(batch)));
    queued_bytes_ += bytes;
    if (outbound_.back()->terminal()) entry.terminal_queued = true;
    ++published_batches_;
    return {};
}

SendLeaseId TriggerSession::next_lease_id() noexcept
{
    const auto result = next_lease_id_++;
    if (next_lease_id_ == 0) next_lease_id_ = 1;
    return result;
}

BeginSendResult TriggerSession::begin_send()
{
    std::lock_guard lock(mutex_);
    if (closed_) return {std::nullopt, BeginSendError::closed};
    if (active_lease_id_)
        return {std::nullopt, BeginSendError::send_in_progress};
    if (outbound_.empty()) return {std::nullopt, BeginSendError::queue_empty};

    const auto id = next_lease_id();
    active_lease_id_ = id;
    ++send_leases_started_;
    return {SendLease{id, outbound_.front()}, BeginSendError::none};
}

CompleteSendResult TriggerSession::complete_send(const SendLease& lease)
{
    std::lock_guard lock(mutex_);
    if (closed_) return {SendTransitionError::closed};
    if (!active_lease_id_) return {SendTransitionError::no_active_lease};
    if (*active_lease_id_ != lease.id())
        return {SendTransitionError::lease_mismatch};

    const auto& batch = *outbound_.front();
    queued_bytes_ -= batch_bytes(batch);
    if (batch.terminal()) entries_.erase(batch.timestamp());
    outbound_.pop_front();
    active_lease_id_.reset();
    ++popped_batches_;
    return {};
}

FailSendResult TriggerSession::fail_send(const SendLease& lease)
{
    std::lock_guard lock(mutex_);
    if (closed_) return {SendTransitionError::closed, {}};
    if (!active_lease_id_)
        return {SendTransitionError::no_active_lease, {}};
    if (*active_lease_id_ != lease.id())
        return {SendTransitionError::lease_mismatch, {}};

    ++send_failures_;
    return {SendTransitionError::none, close_locked()};
}

std::vector<ActiveCommand> TriggerSession::close_locked()
{
    if (closed_) return {};
    std::vector<ActiveCommand> active;
    active.reserve(entries_.size());
    for (const auto& [timestamp, entry] : entries_) {
        if (!entry.terminal_queued) {
            active.push_back({entry.command, timestamp, entry.cancel_requested});
        }
    }
    dropped_batches_ += outbound_.size();
    dropped_bytes_ += queued_bytes_;
    closed_ = true;
    entries_.clear();
    outbound_.clear();
    queued_bytes_ = 0;
    active_lease_id_.reset();
    return active;
}

std::vector<ActiveCommand> TriggerSession::close()
{
    std::lock_guard lock(mutex_);
    return close_locked();
}

TriggerSessionStats TriggerSession::stats() const
{
    std::lock_guard lock(mutex_);
    return {
        accepted_,
        admission_rejections_,
        published_batches_,
        publish_rejections_,
        popped_batches_,
        send_leases_started_,
        send_failures_,
        dropped_batches_,
        dropped_bytes_,
        cancellations_requested_,
        queue_backpressure_,
        entries_.size(),
        outbound_.size(),
        queued_bytes_,
        active_lease_id_.has_value(),
        closed_,
    };
}

}  // namespace baas::service::protocol::trigger
