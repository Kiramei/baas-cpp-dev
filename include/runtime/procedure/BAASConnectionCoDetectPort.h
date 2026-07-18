#pragma once

#include "runtime/procedure/CoDetectProductionAdapter.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace baas {

class BAAS;

namespace runtime::procedure {

inline constexpr std::uint32_t baas_connection_co_detect_width = 1'280;
inline constexpr std::uint32_t baas_connection_co_detect_height = 720;
inline constexpr std::size_t baas_connection_co_detect_row_stride =
    static_cast<std::size_t>(baas_connection_co_detect_width) * 3U;
inline constexpr std::size_t baas_connection_co_detect_frame_bytes =
    baas_connection_co_detect_row_stride * baas_connection_co_detect_height;

struct BAASConnectionCoDetectRawFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t row_stride{};
    std::vector<std::byte> pixels;
};

using BAASConnectionCoDetectCheckpoint =
    std::function<CoDetectResult<std::monostate>()>;

// Narrow seam around one already-created BAAS application. Implementations must
// freeze the exact connection/device/profile source they were created from and
// report false from identity_valid() after any in-place connection/config change.
// The production owner must invalidate the lease before performing such mutation.
class BAASConnectionCoDetectBackend {
public:
    virtual ~BAASConnectionCoDetectBackend() = default;
    [[nodiscard]] virtual const std::string& device_id() const noexcept = 0;
    [[nodiscard]] virtual const std::string& profile_name() const noexcept = 0;
    [[nodiscard]] virtual bool identity_valid() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t screenshot_interval_ms() const noexcept = 0;
    [[nodiscard]] virtual CoDetectResult<BAASConnectionCoDetectRawFrame> capture(
        const BAASConnectionCoDetectCheckpoint& checkpoint) = 0;
    [[nodiscard]] virtual CoDetectResult<std::monostate> click(CoDetectClick click) = 0;
    [[nodiscard]] virtual CoDetectResult<bool> foreground_matches() = 0;
};

struct BAASConnectionCoDetectBinding final {
    std::shared_ptr<CoDetectProductionDevicePort> port;
    std::shared_ptr<const CoDetectProductionDeviceIdentity> identity;
};

// Sole lifecycle owner for production device leases. activate() and invalidate()
// linearize with capture/click/foreground work. Every activation creates a new
// immutable token; old ports cannot retarget to the replacement backend. Once
// identity invalidity is observed, that token is permanently tombstoned.
class BAASConnectionCoDetectOwner final {
public:
    BAASConnectionCoDetectOwner();
    ~BAASConnectionCoDetectOwner();

    BAASConnectionCoDetectOwner(const BAASConnectionCoDetectOwner&) = delete;
    BAASConnectionCoDetectOwner& operator=(const BAASConnectionCoDetectOwner&) = delete;
    BAASConnectionCoDetectOwner(BAASConnectionCoDetectOwner&&) = delete;
    BAASConnectionCoDetectOwner& operator=(BAASConnectionCoDetectOwner&&) = delete;

    // Throws invalid_argument before publication when the backend snapshot,
    // profile, Android flag, or strictly increasing non-zero epoch is invalid.
    [[nodiscard]] BAASConnectionCoDetectBinding activate(
        std::shared_ptr<BAASConnectionCoDetectBackend> backend,
        CoDetectProfile profile,
        std::uint64_t session_epoch,
        bool android = true);

    // Idempotent. In-flight device effects finish before the token is withdrawn;
    // bounded waits are woken and fail closed as SessionChanged.
    void invalidate() noexcept;

private:
    class State;
    std::shared_ptr<State> state_;
};

// Binds only public BAAS/BAASConnection/BAASScreenshot/BAASControl capabilities.
// The caller supplies the already-created application; no config/resource/path is
// read or retained by this factory.
[[nodiscard]] std::shared_ptr<BAASConnectionCoDetectBackend>
make_baas_application_co_detect_backend(std::shared_ptr<BAAS> application);

#if defined(BAAS_CONNECTION_CO_DETECT_PORT_TESTING)
namespace detail {
void fail_activation_allocation_after(std::size_t successful_checkpoints) noexcept;
void clear_activation_allocation_failure() noexcept;
}  // namespace detail
#endif

}  // namespace runtime::procedure
}  // namespace baas
