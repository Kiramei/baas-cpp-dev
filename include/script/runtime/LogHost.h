#pragma once

#include "script/runtime/JsonBridge.h"
#include "script/runtime/SynchronousHost.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace baas::script::runtime {

enum class StructuredLogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

[[nodiscard]] std::string_view structured_log_level_name(
    StructuredLogLevel level) noexcept;

struct LogHostIdentity {
    std::string task_id;
    std::string session_id;
    std::string config_name;
};

struct StructuredLogEvent {
    StructuredLogLevel level{StructuredLogLevel::Info};
    std::string message;
    std::optional<JsonObject> fields;
    LogHostIdentity identity;
    friend bool operator==(const StructuredLogEvent&, const StructuredLogEvent&) = default;
};

class StructuredLogSink {
public:
    virtual ~StructuredLogSink() = default;
    virtual void write(const StructuredLogEvent& event) = 0;
};

struct QueuedLogHostLimits {
    std::size_t queue_capacity{1'024};
    std::size_t max_secret_count{64};
    std::size_t max_secret_bytes{64U * 1'024U};
    std::size_t max_event_bytes{256U * 1'024U};
    std::size_t max_field_nodes{10'000};
    std::size_t max_redaction_work{500'000};
};

enum class LogHostConfigErrorCode : std::uint8_t {
    InvalidLimits,
    MissingSink,
    InvalidIdentity,
    SecretLimitExceeded,
    InvalidSecret,
};

class LogHostConfigError final : public std::runtime_error {
public:
    LogHostConfigError(LogHostConfigErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] LogHostConfigErrorCode code() const noexcept { return code_; }

private:
    LogHostConfigErrorCode code_;
};

struct QueuedLogHostStats {
    std::size_t accepted{};
    std::size_t delivered{};
    std::size_t sink_failures{};
    std::size_t backpressure_rejections{};
    std::size_t unavailable_rejections{};
};

// Production-capable baas/log.emit owner. Calls only validate, redact, copy,
// and enqueue. One bounded worker preserves event order and owns sink calls.
class QueuedLogHost final {
public:
    QueuedLogHost(
        std::shared_ptr<StructuredLogSink> sink,
        LogHostIdentity identity,
        std::vector<std::string> secrets = {},
        QueuedLogHostLimits limits = {});
    ~QueuedLogHost();

    QueuedLogHost(const QueuedLogHost&) = delete;
    QueuedLogHost& operator=(const QueuedLogHost&) = delete;
    QueuedLogHost(QueuedLogHost&&) = delete;
    QueuedLogHost& operator=(QueuedLogHost&&) = delete;

    void shutdown() noexcept;
    [[nodiscard]] QueuedLogHostStats stats() const noexcept;

private:
    friend SynchronousNativeBinding make_queued_log_binding(
        std::shared_ptr<QueuedLogHost> host);
    [[nodiscard]] HostResult emit(const HostCallContext&, const HostArguments&);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] SynchronousNativeBinding make_queued_log_binding(
    std::shared_ptr<QueuedLogHost> host);

// Fixed-key ordered JSON for legacy text sinks and diagnostics. A disengaged
// result means the value is invalid or exceeds max_bytes; allocation failures
// are not swallowed.
[[nodiscard]] std::optional<std::string> serialize_structured_log_event(
    const StructuredLogEvent& event,
    std::size_t max_bytes);

}  // namespace baas::script::runtime
