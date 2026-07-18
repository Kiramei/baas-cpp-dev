#pragma once

#include "runtime/procedure/CoDetectPythonCompatDefinition.h"
#include "runtime/procedure/RuntimeProcedureActivation.h"
#include "script/host/ProcedureHost.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace baas::runtime::procedure {

class CoDetectFrame {
public:
    virtual ~CoDetectFrame() = default;
    [[nodiscard]] virtual std::string_view device_id() const noexcept = 0;
    [[nodiscard]] virtual CoDetectProfile profile() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t session_epoch() const noexcept = 0;
};

enum class CoDetectOperationError : std::uint8_t {
    Cancelled,
    ContextDeadlineExceeded,
    CallDeadlineExceeded,
    DeviceDisconnected,
    ResourceNotFound,
    ResourceExhausted,
    Unavailable,
    SessionChanged,
    Internal,
};

enum class CoDetectControlState : std::uint8_t {
    Proceed,
    ContextDeadlineExceeded,
    CallDeadlineExceeded,
    Cancelled,
    SessionChanged,
};

class CoDetectControl {
public:
    virtual ~CoDetectControl() = default;
    [[nodiscard]] virtual CoDetectControlState poll() const noexcept = 0;
};

template <class T>
using CoDetectResult = std::variant<T, CoDetectOperationError>;

// One immutable physical-device/profile lease. Adapters create a new object
// when reconnecting or switching profile; they must never retarget a live pin.
class CoDetectPinnedDeviceSession {
public:
    virtual ~CoDetectPinnedDeviceSession() = default;
    [[nodiscard]] virtual const std::string& device_id() const noexcept = 0;
    [[nodiscard]] virtual CoDetectProfile profile() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t session_epoch() const noexcept = 0;
    // False once the injected owner no longer publishes this exact immutable pin.
    [[nodiscard]] virtual bool identity_valid() const noexcept = 0;
    [[nodiscard]] virtual bool is_android() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t monotonic_ms() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t screenshot_interval_ms() const noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<const CoDetectFrame> latest_frame() const noexcept = 0;
    virtual void publish_latest_frame(std::shared_ptr<const CoDetectFrame> frame) noexcept = 0;
    [[nodiscard]] virtual CoDetectResult<std::shared_ptr<const CoDetectFrame>> capture(
        const CoDetectControl& control) = 0;
    [[nodiscard]] virtual CoDetectResult<std::monostate> click(
        CoDetectClick click, const CoDetectControl& control) = 0;
    [[nodiscard]] virtual CoDetectResult<std::monostate> wait(
        std::uint64_t milliseconds, const CoDetectControl& control) = 0;
    [[nodiscard]] virtual CoDetectResult<bool> foreground_matches(
        const CoDetectControl& control) = 0;
};

// One immutable activation-generation/profile feature view. Runtime repository
// updates publish a new view instead of retargeting a live execution view.
class CoDetectPinnedFeatureView {
public:
    virtual ~CoDetectPinnedFeatureView() = default;
    [[nodiscard]] virtual std::string_view generation() const noexcept = 0;
    [[nodiscard]] virtual CoDetectProfile profile() const noexcept = 0;
    [[nodiscard]] virtual bool identity_valid() const noexcept = 0;
    [[nodiscard]] virtual CoDetectResult<bool> match_rgb(
        const CoDetectFrame& frame, std::string_view feature,
        const CoDetectControl& control) = 0;
    [[nodiscard]] virtual CoDetectResult<bool> match_image(
        const CoDetectFrame& frame, std::string_view feature,
        CoDetectImageMatch match, const CoDetectControl& control) = 0;
};

struct CoDetectTerminalBinding final {
    std::string source;
    std::string id;
};

[[nodiscard]] std::shared_ptr<::baas::script::host::ProcedureExecutor>
make_co_detect_python_compat_executor(
    std::shared_ptr<const CoDetectPythonCompatDefinition> definition,
    std::vector<CoDetectTerminalBinding> terminals,
    std::shared_ptr<CoDetectPinnedDeviceSession> session,
    std::shared_ptr<CoDetectPinnedFeatureView> features,
    std::shared_ptr<const ::baas::script::host::ProcedureSnapshot> expected_snapshot,
    std::shared_ptr<const ::baas::script::host::ProcedureDescriptor> expected_descriptor,
    std::string expected_generation);

// Parses only the exact verified bytes owned by the immutable activation and
// binds execution to that activation's snapshot, descriptor identity, engine,
// and ordered source-to-terminal mapping.
[[nodiscard]] std::shared_ptr<::baas::script::host::ProcedureExecutor>
make_activated_co_detect_python_compat_executor(
    std::shared_ptr<const RuntimeProcedureActivation> activation,
    std::string_view procedure_id,
    std::shared_ptr<CoDetectPinnedDeviceSession> session,
    std::shared_ptr<CoDetectPinnedFeatureView> features);

}  // namespace baas::runtime::procedure
