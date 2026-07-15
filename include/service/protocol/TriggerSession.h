#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::protocol::trigger {

using Timestamp = std::uint64_t;
inline constexpr Timestamp maximum_safe_timestamp = 9'007'199'254'740'991ULL;

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
};

[[nodiscard]] std::string_view admission_error_name(AdmissionError error) noexcept;

struct AdmissionResult {
    AdmissionError error{AdmissionError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == AdmissionError::none;
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
struct OutboundBatch {
    std::string command;
    Timestamp timestamp{};
    ResponseStatus status{ResponseStatus::ok};
    bool terminal{true};
    std::string json;
    std::vector<std::byte> binary;
};

enum class PublishError : std::uint8_t {
    none,
    closed,
    unknown_timestamp,
    command_mismatch,
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

struct TriggerSessionStats {
    std::size_t accepted{};
    std::size_t admission_rejections{};
    std::size_t published_batches{};
    std::size_t publish_rejections{};
    std::size_t popped_batches{};
    std::size_t cancellations_requested{};
    std::size_t queue_backpressure{};
    std::size_t active_correlations{};
    std::size_t queued_batches{};
    std::size_t queued_bytes{};
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
    [[nodiscard]] CancelDecision request_cancel(Timestamp timestamp);
    [[nodiscard]] PublishResult publish(OutboundBatch batch);
    [[nodiscard]] std::optional<OutboundBatch> pop();

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
    };

    [[nodiscard]] static std::size_t batch_bytes(const OutboundBatch& batch) noexcept;

    TriggerSessionLimits limits_;
    mutable std::mutex mutex_;
    std::map<Timestamp, Entry> entries_;
    std::deque<OutboundBatch> outbound_;
    std::size_t queued_bytes_{};
    std::size_t accepted_{};
    std::size_t admission_rejections_{};
    std::size_t published_batches_{};
    std::size_t publish_rejections_{};
    std::size_t popped_batches_{};
    std::size_t cancellations_requested_{};
    std::size_t queue_backpressure_{};
    bool closed_{};
};

}  // namespace baas::service::protocol::trigger
