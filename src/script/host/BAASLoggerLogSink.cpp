#include "script/host/BAASLoggerLogSink.h"

#include "BAASLogger.h"

#include <stdexcept>
#include <utility>

namespace baas::script::host {

BAASLoggerLogSink::BAASLoggerLogSink(
    GlobalLogger& logger, const std::size_t max_serialized_bytes) noexcept
    : logger_(&logger), max_serialized_bytes_(max_serialized_bytes)
{
}

void BAASLoggerLogSink::write(const runtime::StructuredLogEvent& event)
{
    const auto serialized = runtime::serialize_structured_log_event(
        event, max_serialized_bytes_);
    if (!serialized)
        throw std::runtime_error("structured log serialization failed");

    int legacy_level{};
    switch (event.level) {
        case runtime::StructuredLogLevel::Trace: legacy_level = 0; break;
        case runtime::StructuredLogLevel::Debug: legacy_level = 1; break;
        case runtime::StructuredLogLevel::Info: legacy_level = 2; break;
        case runtime::StructuredLogLevel::Warning: legacy_level = 3; break;
        case runtime::StructuredLogLevel::Error: legacy_level = 4; break;
        case runtime::StructuredLogLevel::Critical: legacy_level = 5; break;
        default: throw std::runtime_error("invalid structured log level");
    }
    logger_->_out(*serialized, legacy_level);
}

runtime::SynchronousNativeBinding make_baas_logger_log_binding(
    GlobalLogger& logger,
    runtime::LogHostIdentity identity,
    std::vector<std::string> secrets,
    const runtime::QueuedLogHostLimits limits)
{
    auto sink = std::make_shared<BAASLoggerLogSink>(
        logger, limits.max_event_bytes);
    auto host = std::make_shared<runtime::QueuedLogHost>(
        std::move(sink), std::move(identity), std::move(secrets), limits);
    return runtime::make_queued_log_binding(std::move(host));
}

}  // namespace baas::script::host
