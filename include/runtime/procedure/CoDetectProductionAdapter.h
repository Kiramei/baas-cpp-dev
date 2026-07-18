#pragma once

#include "runtime/procedure/CoDetectPythonCompatExecutor.h"
#include "runtime/procedure/CoDetectSupportBundle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace baas::runtime::procedure {

// Owner-created identity token. Reconnect/profile changes must publish a new token;
// a port must never mutate a token already handed to a pinned adapter.
struct CoDetectProductionDeviceIdentity final {
    std::string device_id;
    CoDetectProfile profile{};
    std::uint64_t session_epoch{};
    bool android{};
};

struct CoDetectProductionBgrFrame final {
    std::shared_ptr<const CoDetectProductionDeviceIdentity> identity;
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_stride{};
    std::shared_ptr<const std::vector<std::byte>> pixels;
};

// Narrow production device-owner boundary. It contains no config, resource path,
// definition, or global registry access; the embedding application owns those APIs.
class CoDetectProductionDevicePort {
public:
    virtual ~CoDetectProductionDevicePort() = default;
    [[nodiscard]] virtual std::shared_ptr<const CoDetectProductionDeviceIdentity>
    current_identity() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t monotonic_ms() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t screenshot_interval_ms() const noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<const CoDetectProductionBgrFrame>
    latest_frame() const noexcept = 0;
    [[nodiscard]] virtual bool publish_latest_frame(
        std::shared_ptr<const CoDetectProductionBgrFrame> frame) noexcept = 0;
    [[nodiscard]] virtual CoDetectResult<CoDetectProductionBgrFrame> capture(
        const CoDetectControl& control) = 0;
    [[nodiscard]] virtual CoDetectResult<std::monostate> click(
        CoDetectClick click, const CoDetectControl& control) = 0;
    [[nodiscard]] virtual CoDetectResult<std::monostate> wait(
        std::uint64_t milliseconds, const CoDetectControl& control) = 0;
    [[nodiscard]] virtual CoDetectResult<bool> foreground_matches(
        const CoDetectControl& control) = 0;
};

struct CoDetectProductionAdapterLimits final {
    std::uint32_t frame_width{1'280};
    std::uint32_t frame_height{720};
    std::size_t max_frame_bytes{4U * 1024U * 1024U};
    std::uint64_t max_screenshot_interval_ms{60'000};
};

struct CoDetectProductionPins final {
    std::shared_ptr<CoDetectPinnedDeviceSession> session;
    std::shared_ptr<CoDetectPinnedFeatureView> features;
};

// Throws invalid_argument before device work when identity, generation, profile,
// frame limits, or ownership is not the exact frozen production binding.
[[nodiscard]] CoDetectProductionPins make_co_detect_production_pins(
    std::shared_ptr<CoDetectProductionDevicePort> port,
    std::shared_ptr<const CoDetectProductionDeviceIdentity> identity,
    std::shared_ptr<const CoDetectSupportBundle> bundle,
    std::string_view expected_generation,
    const CoDetectProductionAdapterLimits& limits = {});

[[nodiscard]] std::shared_ptr<::baas::script::host::ProcedureExecutor>
make_activated_co_detect_production_executor(
    std::shared_ptr<const RuntimeProcedureActivation> activation,
    std::string_view procedure_id,
    std::shared_ptr<CoDetectProductionDevicePort> port,
    std::shared_ptr<const CoDetectProductionDeviceIdentity> identity,
    std::shared_ptr<const CoDetectSupportBundle> bundle,
    const CoDetectProductionAdapterLimits& limits = {});

}  // namespace baas::runtime::procedure
