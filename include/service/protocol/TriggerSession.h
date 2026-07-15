#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace baas::service::protocol::trigger {

struct CommandResponse;
struct TriggerEnvelopeLimits;
struct EncodeResponseResult;

using Timestamp = std::uint64_t;
inline constexpr Timestamp maximum_safe_timestamp = 9'007'199'254'740'991ULL;

class TriggerSession;

// Session-minted correlation capability. The generation prevents an old
// handler from publishing into a later command that reuses the same timestamp.
// Only TriggerSession can create or inspect a receipt.
class AdmissionReceipt final {
public:
    AdmissionReceipt(const AdmissionReceipt&) = default;
    AdmissionReceipt& operator=(const AdmissionReceipt&) = default;
    AdmissionReceipt(AdmissionReceipt&&) noexcept = default;
    AdmissionReceipt& operator=(AdmissionReceipt&&) noexcept = default;

private:
    AdmissionReceipt(
        std::uint64_t owner_id,
        Timestamp timestamp,
        std::uint64_t generation) noexcept
        : owner_id_(owner_id), timestamp_(timestamp), generation_(generation)
    {}

    friend class TriggerSession;

    std::uint64_t owner_id_{};
    Timestamp timestamp_{};
    std::uint64_t generation_{};
};

enum class ResponseMode : std::uint8_t {
    single,
    stream,
};

// Metadata admitted before command-specific dispatch. Payload ownership stays
// with the transport/dispatcher; this boundary records only bounded sizes and
// correlation state.
struct CommandAdmission {
    std::string command;
    Timestamp timestamp{};
    std::optional<std::string> config_id;
    std::size_t payload_bytes{};
    std::size_t binary_bytes{};
    ResponseMode response_mode{ResponseMode::single};
};

struct TriggerSessionLimits {
    std::size_t max_in_flight{256};
    std::size_t max_command_bytes{128};
    std::size_t max_config_id_bytes{256};
    std::size_t max_request_payload_bytes{1U * 1'024U * 1'024U};
    std::size_t max_request_binary_bytes{64U * 1'024U * 1'024U};
    std::size_t max_response_json_bytes{1U * 1'024U * 1'024U};
    std::size_t max_response_binary_bytes{64U * 1'024U * 1'024U};
    std::size_t max_queued_batches{256};
    std::size_t max_queued_bytes{72U * 1'024U * 1'024U};
};

enum class AdmissionError : std::uint8_t {
    none,
    closed,
    invalid_command,
    invalid_timestamp,
    invalid_config_id,
    payload_too_large,
    binary_too_large,
    duplicate_timestamp,
    in_flight_limit,
    admission_generation_exhausted,
};

[[nodiscard]] std::string_view admission_error_name(AdmissionError error) noexcept;

struct AdmissionResult {
    AdmissionError error{AdmissionError::none};
    std::optional<AdmissionReceipt> receipt;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AdmissionError::none && receipt.has_value();
    }
};

enum class ResponseStatus : std::uint8_t {
    ok,
    error,
    cancelled,
};

// One queue element is the indivisible send unit. A transport must serialize
// json and the optional immediately-following binary frame while holding its
// per-connection send lock.
class OutboundBatch final {
public:
    OutboundBatch() = default;
    OutboundBatch(const OutboundBatch&) = default;
    OutboundBatch& operator=(const OutboundBatch&) = default;
    OutboundBatch(OutboundBatch&&) noexcept = default;
    OutboundBatch& operator=(OutboundBatch&&) noexcept = default;

    [[nodiscard]] const std::string& command() const noexcept { return command_; }
    [[nodiscard]] Timestamp timestamp() const noexcept { return timestamp_; }
    [[nodiscard]] ResponseStatus status() const noexcept { return status_; }
    [[nodiscard]] ResponseMode response_mode() const noexcept { return response_mode_; }
    [[nodiscard]] bool terminal() const noexcept { return terminal_; }
    [[nodiscard]] const std::string& json() const noexcept { return json_; }
    [[nodiscard]] bool has_binary() const noexcept { return has_binary_; }
    [[nodiscard]] const std::vector<std::byte>& binary() const noexcept { return binary_; }

private:
    OutboundBatch(
        std::string command,
        Timestamp timestamp,
        ResponseStatus status,
        ResponseMode response_mode,
        bool terminal,
        std::string json,
        bool has_binary,
        std::vector<std::byte> binary)
        : command_(std::move(command)),
          timestamp_(timestamp),
          status_(status),
          response_mode_(response_mode),
          terminal_(terminal),
          json_(std::move(json)),
          has_binary_(has_binary),
          binary_(std::move(binary))
    {}

    friend EncodeResponseResult encode_command_response(
        CommandResponse response, TriggerEnvelopeLimits limits);

    std::string command_;
    Timestamp timestamp_{};
    ResponseStatus status_{ResponseStatus::ok};
    ResponseMode response_mode_{ResponseMode::single};
    bool terminal_{true};
    std::string json_;
    // Distinguishes no binary frame from a declared zero-byte binary frame.
    // TriggerEnvelope sets this flag from optional binary presence.
    bool has_binary_{};
    std::vector<std::byte> binary_;
};

enum class PublishError : std::uint8_t {
    none,
    closed,
    invalid_admission_receipt,
    unknown_timestamp,
    command_mismatch,
    response_mode_mismatch,
    terminal_already_queued,
    single_response_must_be_terminal,
    error_response_must_be_terminal,
    cancellation_response_required,
    invalid_json_utf8,
    json_too_large,
    binary_too_large,
    queue_full,
    queued_bytes_exceeded,
};

[[nodiscard]] std::string_view publish_error_name(PublishError error) noexcept;

struct PublishResult {
    PublishError error{PublishError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == PublishError::none;
    }
};

enum class RollbackError : std::uint8_t {
    none,
    closed,
    invalid_admission_receipt,
    response_already_queued,
};

[[nodiscard]] std::string_view rollback_error_name(RollbackError error) noexcept;

struct RollbackResult {
    RollbackError error{RollbackError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RollbackError::none;
    }
};

enum class CancelDecision : std::uint8_t {
    requested,
    already_requested,
    unknown_timestamp,
    terminal_already_queued,
    closed,
};

struct ActiveCommand {
    std::string command;
    Timestamp timestamp{};
    bool cancel_requested{};
};

using SendLeaseId = std::uint64_t;

// A read-only, shared ownership view of the queue head. The session retains the
// same batch and its byte charge until complete_send() confirms the entire
// JSON-plus-optional-binary unit. Copies are safe; stale acknowledgements are
// rejected by the unique lease id.
class SendLease final {
public:
    SendLease(const SendLease&) = default;
    SendLease& operator=(const SendLease&) = default;
    SendLease(SendLease&&) noexcept = default;
    SendLease& operator=(SendLease&&) noexcept = default;

    [[nodiscard]] SendLeaseId id() const noexcept { return id_; }
    [[nodiscard]] const OutboundBatch& batch() const noexcept { return *batch_; }

private:
    SendLease(SendLeaseId id, std::shared_ptr<const OutboundBatch> batch)
        : id_(id), batch_(std::move(batch))
    {}

    friend class TriggerSession;

    SendLeaseId id_{};
    std::shared_ptr<const OutboundBatch> batch_;
};

enum class BeginSendError : std::uint8_t {
    none,
    closed,
    queue_empty,
    send_in_progress,
};

[[nodiscard]] std::string_view begin_send_error_name(BeginSendError error) noexcept;

struct BeginSendResult {
    std::optional<SendLease> lease;
    BeginSendError error{BeginSendError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == BeginSendError::none && lease.has_value();
    }
};

enum class SendTransitionError : std::uint8_t {
    none,
    closed,
    no_active_lease,
    lease_mismatch,
};

[[nodiscard]] std::string_view send_transition_error_name(
    SendTransitionError error) noexcept;

struct CompleteSendResult {
    SendTransitionError error{SendTransitionError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SendTransitionError::none;
    }
};

struct FailSendResult {
    SendTransitionError error{SendTransitionError::none};
    std::vector<ActiveCommand> cancel;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SendTransitionError::none;
    }
};

struct TriggerSessionStats {
    std::size_t accepted{};
    std::size_t admission_rejections{};
    std::size_t rolled_back_admissions{};
    std::size_t published_batches{};
    std::size_t publish_rejections{};
    std::size_t popped_batches{};
    std::size_t send_leases_started{};
    std::size_t send_failures{};
    std::size_t dropped_batches{};
    std::size_t dropped_bytes{};
    std::size_t cancellations_requested{};
    std::size_t queue_backpressure{};
    std::size_t active_correlations{};
    std::size_t queued_batches{};
    std::size_t queued_bytes{};
    bool send_in_progress{};
    bool closed{};
};

// Thread-safe correlation and outbound ordering core shared by WebSocket and
// BPIP trigger transports. It does not parse JSON or execute commands.
class TriggerSession final {
public:
    explicit TriggerSession(TriggerSessionLimits limits = {});

    TriggerSession(const TriggerSession&) = delete;
    TriggerSession& operator=(const TriggerSession&) = delete;
    TriggerSession(TriggerSession&&) = delete;
    TriggerSession& operator=(TriggerSession&&) = delete;

    [[nodiscard]] AdmissionResult admit(CommandAdmission command);
    [[nodiscard]] RollbackResult rollback(const AdmissionReceipt& receipt);
    [[nodiscard]] CancelDecision request_cancel(Timestamp timestamp);
    [[nodiscard]] PublishResult publish(
        const AdmissionReceipt& receipt, OutboundBatch&& batch);
    [[nodiscard]] BeginSendResult begin_send();

    // Confirms that every frame in the leased batch was written successfully.
    // Only this transition removes the queue head and releases a terminal
    // correlation. Duplicate or stale confirmations never affect a newer lease.
    [[nodiscard]] CompleteSendResult complete_send(const SendLease& lease);

    // A write failure is connection-fatal because a JSON/binary batch may be
    // partially visible. A matching failure atomically closes the session,
    // drops all unsendable output, and returns still-running commands to cancel.
    [[nodiscard]] FailSendResult fail_send(const SendLease& lease);

    // Stops admission, drops unsendable queued output, and returns every command
    // whose executor/task owner must be cancelled because the connection ended.
    [[nodiscard]] std::vector<ActiveCommand> close();

    [[nodiscard]] TriggerSessionStats stats() const;
    [[nodiscard]] const TriggerSessionLimits& limits() const noexcept { return limits_; }

private:
    struct Entry {
        std::string command;
        ResponseMode response_mode{ResponseMode::single};
        bool cancel_requested{};
        bool terminal_queued{};
        bool response_queued{};
        std::uint64_t generation{};
    };

    [[nodiscard]] static std::size_t batch_bytes(const OutboundBatch& batch) noexcept;

    TriggerSessionLimits limits_;
    const std::uint64_t instance_id_;
    mutable std::mutex mutex_;
    std::map<Timestamp, Entry> entries_;
    [[nodiscard]] std::vector<ActiveCommand> close_locked();
    [[nodiscard]] SendLeaseId next_lease_id() noexcept;

    std::deque<std::shared_ptr<const OutboundBatch>> outbound_;
    std::optional<SendLeaseId> active_lease_id_;
    SendLeaseId next_lease_id_{1};
    std::size_t queued_bytes_{};
    std::size_t accepted_{};
    std::size_t admission_rejections_{};
    std::size_t rolled_back_admissions_{};
    std::size_t published_batches_{};
    std::size_t publish_rejections_{};
    std::size_t popped_batches_{};
    std::size_t send_leases_started_{};
    std::size_t send_failures_{};
    std::size_t dropped_batches_{};
    std::size_t dropped_bytes_{};
    std::size_t cancellations_requested_{};
    std::size_t queue_backpressure_{};
    std::uint64_t next_admission_generation_{1};
    bool closed_{};
};

}  // namespace baas::service::protocol::trigger
