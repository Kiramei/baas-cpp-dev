#pragma once

#include "script/runtime/LogHost.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace baas {

class GlobalLogger;

namespace script::host {

// Application adapter. GlobalLogger remains owned by the BAAS application and
// must outlive this sink; queued LogHost shutdown drains before releasing it.
class BAASLoggerLogSink final : public runtime::StructuredLogSink {
public:
    explicit BAASLoggerLogSink(
        GlobalLogger& logger,
        std::size_t max_serialized_bytes = 256U * 1'024U) noexcept;

    void write(const runtime::StructuredLogEvent& event) override;

private:
    GlobalLogger* logger_;
    std::size_t max_serialized_bytes_;
};

[[nodiscard]] runtime::SynchronousNativeBinding make_baas_logger_log_binding(
    GlobalLogger& logger,
    runtime::LogHostIdentity identity,
    std::vector<std::string> secrets = {},
    runtime::QueuedLogHostLimits limits = {});

}  // namespace script::host
}  // namespace baas
