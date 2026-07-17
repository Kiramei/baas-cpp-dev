#pragma once

#include "service/runtime/RuntimeTaskOwner.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace baas::service::app {

struct RuntimeTaskStatusJsonLimits {
    std::size_t max_configs{256};
    std::size_t max_waiting_tasks{256};
    std::size_t max_button_json_bytes{64U * 1'024U};
    std::size_t max_button_json_depth{32};
    std::size_t max_button_json_nodes{4'096};
    std::size_t max_output_bytes{1U * 1'024U * 1'024U};
};

enum class RuntimeTaskStatusJsonError : std::uint8_t {
    none,
    invalid_limits,
    invalid_snapshot,
    duplicate_config,
    capacity,
    resource_exhausted,
};

[[nodiscard]] std::string_view runtime_task_status_json_error_name(
    RuntimeTaskStatusJsonError error) noexcept;

struct RuntimeTaskStatusJsonResult {
    std::string json;
    RuntimeTaskStatusJsonError error{RuntimeTaskStatusJsonError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RuntimeTaskStatusJsonError::none;
    }
};

// Encodes the exact Python-compatible current_status() object. Input ordering
// is irrelevant; config keys are emitted in bytewise order. The internal
// `stopping` state is intentionally not exposed on this v1 wire shape.
//
// RuntimeTaskProgress::button accepts either already-serialized JSON or a
// legacy plain string. Valid bounded JSON is retained as a JSON value; any
// other valid UTF-8 text is encoded as a string.
[[nodiscard]] RuntimeTaskStatusJsonResult encode_runtime_task_status_json(
    std::span<const runtime::RuntimeTaskSnapshot> snapshots,
    RuntimeTaskStatusJsonLimits limits = {}) noexcept;

}  // namespace baas::service::app
