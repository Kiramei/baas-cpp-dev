#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace baas {

inline constexpr int default_screenshot_interval_ms = 300;
inline constexpr int maximum_screenshot_wait_slice_ms = 50;

[[nodiscard]] inline bool valid_screenshot_interval_seconds(const double value) noexcept
{
    return std::isfinite(value) && value >= 0.0 &&
        value * 1000.0 <= static_cast<double>(std::numeric_limits<int>::max());
}

[[nodiscard]] inline int normalize_screenshot_interval_ms(const double value) noexcept
{
    if (!valid_screenshot_interval_seconds(value))
        return default_screenshot_interval_ms;
    return static_cast<int>(value * 1000.0);
}

[[nodiscard]] inline int screenshot_interval_remaining_ms(
    const int interval_ms, const long long elapsed_ms) noexcept
{
    if (interval_ms <= 0 || elapsed_ms >= interval_ms) return 0;
    if (elapsed_ms <= 0) return interval_ms;
    return interval_ms - static_cast<int>(elapsed_ms);
}

template <class Wait, class Checkpoint>
void wait_screenshot_interval_slices(
    int remaining_ms, Wait&& wait, Checkpoint&& checkpoint)
{
    while (remaining_ms > 0) {
        std::forward<Checkpoint>(checkpoint)();
        const auto slice = std::min(remaining_ms, maximum_screenshot_wait_slice_ms);
        std::forward<Wait>(wait)(slice);
        remaining_ms -= slice;
        std::forward<Checkpoint>(checkpoint)();
    }
}

}  // namespace baas
