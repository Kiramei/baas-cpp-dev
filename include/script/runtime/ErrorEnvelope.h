#pragma once

#include "script/runtime/ValueHeap.h"

#include <cstddef>
#include <span>

namespace baas::script::runtime {

// Independent serializer budgets. The heap applies ERR-008 while publishing an
// Error; these limits protect the later public-envelope boundary from corrupt
// state and from otherwise-valid but excessively broad Error/detail graphs.
struct ErrorEnvelopeLimits {
    std::size_t max_depth{256};
    std::size_t max_nodes{100'000};
    std::size_t max_output_bytes{1024U * 1024U};
    std::size_t max_string_bytes{512U * 1024U};
    std::size_t max_work{500'000};
    std::size_t max_cause_depth{16};
    std::size_t max_suppressed_errors{16};
    std::size_t max_message_bytes{4096};
    std::size_t max_detail_bytes{65536};
};

enum class ErrorEnvelopeStatus {
    Complete,
    Fallback,
    InsufficientCapacity,
};

struct ErrorEnvelopeResult {
    ErrorEnvelopeStatus status{ErrorEnvelopeStatus::InsufficientCapacity};
    std::size_t bytes_written{};

    [[nodiscard]] bool complete() const noexcept {
        return status == ErrorEnvelopeStatus::Complete;
    }
    [[nodiscard]] bool used_fallback() const noexcept {
        return status == ErrorEnvelopeStatus::Fallback;
    }
};

// Writes one compact UTF-8 JSON object with the exact ERR-003 field order.
// No terminator is appended. The function never lets C++ exceptions cross the
// public boundary and never writes beyond min(output.size(), max_output_bytes).
[[nodiscard]] ErrorEnvelopeResult serialize_error_envelope(
    const Heap& heap,
    Value error,
    std::span<char> output,
    const ErrorEnvelopeLimits& limits = {}) noexcept;

}  // namespace baas::script::runtime
