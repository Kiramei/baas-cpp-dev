#include "CoDetectProductionAdapterFixture.h"
#include "runtime/procedure/CoDetectProductionAdapter.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace procedure = baas::runtime::procedure;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

class Control final : public procedure::CoDetectControl {
public:
    [[nodiscard]] procedure::CoDetectControlState poll() const noexcept override
    {
        ++polls;
        return drift_after != 0 && polls > drift_after
            ? procedure::CoDetectControlState::SessionChanged
            : state;
    }
    procedure::CoDetectControlState state{procedure::CoDetectControlState::Proceed};
    std::size_t drift_after{};
    mutable std::size_t polls{};
};

class Port final : public procedure::CoDetectProductionDevicePort {
public:
    [[nodiscard]] std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity>
    current_identity() const noexcept override { return identity; }
    [[nodiscard]] std::uint64_t monotonic_ms() const noexcept override { return now; }
    [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override {
        return interval;
    }
    [[nodiscard]] std::shared_ptr<const procedure::CoDetectProductionBgrFrame>
    latest_frame() const noexcept override { return latest; }
    [[nodiscard]] bool publish_latest_frame(
        std::shared_ptr<const procedure::CoDetectProductionBgrFrame> frame) noexcept override
    {
        if (identity.get() != frame->identity.get()) return false;
        latest = std::move(frame);
        return true;
    }
    [[nodiscard]] procedure::CoDetectResult<procedure::CoDetectProductionBgrFrame> capture(
        const procedure::CoDetectControl&) override
    {
        ++captures;
        procedure::CoDetectProductionBgrFrame result{
            identity, width, height, stride,
            std::shared_ptr<const std::vector<std::byte>>{pixels}};
        if (retarget_after_capture) identity = std::move(retarget_after_capture);
        return result;
    }
    [[nodiscard]] procedure::CoDetectResult<std::monostate> click(
        const procedure::CoDetectClick value, const procedure::CoDetectControl&) override
    {
        clicks.push_back(value);
        return std::monostate{};
    }
    [[nodiscard]] procedure::CoDetectResult<std::monostate> wait(
        const std::uint64_t milliseconds, const procedure::CoDetectControl&) override
    {
        now += milliseconds;
        return std::monostate{};
    }
    [[nodiscard]] procedure::CoDetectResult<bool> foreground_matches(
        const procedure::CoDetectControl&) override
    {
        return foreground;
    }

    std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity> identity;
    std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity> retarget_after_capture;
    std::uint64_t now{};
    std::uint64_t interval{10};
    std::uint32_t width{1'280};
    std::uint32_t height{720};
    std::size_t stride{1'280U * 3U};
    std::shared_ptr<std::vector<std::byte>> pixels;
    std::shared_ptr<const procedure::CoDetectProductionBgrFrame> latest;
    std::vector<procedure::CoDetectClick> clicks;
    std::size_t captures{};
    bool foreground{true};
};

[[nodiscard]] std::shared_ptr<const procedure::CoDetectProductionDeviceIdentity> identity(
    const std::uint64_t epoch = 1,
    const procedure::CoDetectProfile profile = procedure::CoDetectProfile::jp)
{
    return std::make_shared<const procedure::CoDetectProductionDeviceIdentity>(
        procedure::CoDetectProductionDeviceIdentity{"device", profile, epoch, true});
}

[[nodiscard]] std::shared_ptr<std::vector<std::byte>> matching_pixels(
    const int channel_offset = 0)
{
    auto pixels = std::make_shared<std::vector<std::byte>>(1'280U * 720U * 3U);
    constexpr std::uint8_t pattern[2][2][3]{
        {{10, 20, 30}, {40, 50, 60}},
        {{70, 80, 90}, {100, 110, 120}},
    };
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const auto offset = (row * 1'280U + column) * 3U;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const auto value = static_cast<int>(pattern[row / 2][column / 2][channel]) +
                    channel_offset;
                (*pixels)[offset + channel] = static_cast<std::byte>(value);
            }
        }
    }
    return pixels;
}

[[nodiscard]] std::shared_ptr<const procedure::CoDetectFrame> capture(
    const procedure::CoDetectProductionPins& pins, const Control& control)
{
    auto result = pins.session->capture(control);
    if (const auto* frame =
            std::get_if<std::shared_ptr<const procedure::CoDetectFrame>>(&result))
        return *frame;
    return {};
}

void test_owned_matching_and_missing_feature()
{
    const auto bundle = make_co_detect_production_test_bundle();
    auto port = std::make_shared<Port>();
    port->identity = identity();
    port->pixels = matching_pixels();
    const auto pins = procedure::make_co_detect_production_pins(
        port, port->identity, bundle, bundle->generation());
    check(pins.session->identity_valid() && pins.features->identity_valid() &&
              pins.session->profile() == procedure::CoDetectProfile::jp &&
              pins.features->generation() == bundle->generation(),
          "production pins retain exact bundle/device identity");

    Control control;
    const auto frame = capture(pins, control);
    check(static_cast<bool>(frame), "valid owned BGR capture must publish a frame");
    if (!frame) return;
    auto rgb = pins.features->match_rgb(*frame, "rgb-hit", control);
    auto missing = pins.features->match_rgb(*frame, "absent", control);
    auto image = pins.features->match_image(*frame, "image-hit", {}, control);
    check(std::get_if<bool>(&rgb) && std::get<bool>(rgb),
          "bundle RGB samples match packed BGR frame pixels");
    check(std::get_if<bool>(&missing) && !std::get<bool>(missing),
          "missing bundle feature is a normal false result");
    check(std::get_if<bool>(&image) && std::get<bool>(image),
          "different-sized exact crop is resized before single-position comparison");

    procedure::CoDetectImageMatch equal_threshold;
    equal_threshold.threshold = 1.0;
    equal_threshold.rgb_diff = 255;
    image = pins.features->match_image(*frame, "image-hit", equal_threshold, control);
    check(std::get_if<bool>(&image) && !std::get<bool>(image),
          "similarity exactly equal to the threshold is false");

    (*port->pixels)[0] = std::byte{0};
    rgb = pins.features->match_rgb(*frame, "rgb-hit", control);
    check(std::get_if<bool>(&rgb) && std::get<bool>(rgb),
          "captured frame copies port memory into immutable owned pixels");

    pins.session->publish_latest_frame(frame);
    const auto latest = pins.session->latest_frame();
    check(latest && latest->device_id() == "device" && latest->session_epoch() == 1,
          "latest-frame cache remains owned by the exact device pin");
}

void test_tolerance_control_and_frame_bounds()
{
    const auto bundle = make_co_detect_production_test_bundle();
    auto port = std::make_shared<Port>();
    port->identity = identity();
    port->pixels = matching_pixels(100);
    const auto pins = procedure::make_co_detect_production_pins(
        port, port->identity, bundle, bundle->generation());
    Control control;
    const auto frame = capture(pins, control);
    check(static_cast<bool>(frame), "offset frame must remain structurally valid");
    if (!frame) return;
    auto result = pins.features->match_image(*frame, "image-hit", {}, control);
    check(std::get_if<bool>(&result) && !std::get<bool>(result),
          "bundle mean RGB tolerance rejects a globally shifted crop");
    procedure::CoDetectImageMatch relaxed;
    relaxed.threshold = 0.8;
    relaxed.rgb_diff = 255;
    result = pins.features->match_image(*frame, "image-hit", relaxed, control);
    check(std::get_if<bool>(&result) && std::get<bool>(result),
          "definition override applies strict threshold and RGB tolerance");

    Control drifting;
    drifting.drift_after = 2;
    result = pins.features->match_image(*frame, "image-hit", relaxed, drifting);
    check(std::get_if<procedure::CoDetectOperationError>(&result) &&
              std::get<procedure::CoDetectOperationError>(result) ==
                  procedure::CoDetectOperationError::SessionChanged,
          "vision polls identity before and after the bounded OpenCV operation");

    port->width = 1'279;
    port->stride = 1'279U * 3U;
    port->pixels = std::make_shared<std::vector<std::byte>>(port->stride * port->height);
    auto invalid = pins.session->capture(control);
    check(std::get_if<procedure::CoDetectOperationError>(&invalid) &&
              std::get<procedure::CoDetectOperationError>(invalid) ==
                  procedure::CoDetectOperationError::Unavailable,
          "non-canonical frame dimensions fail closed before vision");
}

void test_immutable_retarget_and_factory_guards()
{
    const auto bundle = make_co_detect_production_test_bundle();
    auto port = std::make_shared<Port>();
    const auto first = identity();
    port->identity = first;
    port->pixels = matching_pixels();
    const auto old_pins = procedure::make_co_detect_production_pins(
        port, first, bundle, bundle->generation());

    const auto second = identity(2);
    port->identity = second;
    check(!old_pins.session->identity_valid() && old_pins.session->session_epoch() == 1,
          "retargeting the owner invalidates rather than mutates an existing pin");
    const auto new_pins = procedure::make_co_detect_production_pins(
        port, second, bundle, bundle->generation());
    check(new_pins.session->identity_valid() && new_pins.session->session_epoch() == 2,
          "owner updates require a newly constructed pinned session");

    bool rejected_generation{};
    try {
        static_cast<void>(procedure::make_co_detect_production_pins(
            port, second, bundle, "wrong-generation"));
    } catch (const std::invalid_argument&) {
        rejected_generation = true;
    }
    check(rejected_generation, "support bundle generation mismatch is rejected");

    bool rejected_token{};
    try {
        static_cast<void>(procedure::make_co_detect_production_pins(
            port, identity(2), bundle, bundle->generation()));
    } catch (const std::invalid_argument&) {
        rejected_token = true;
    }
    check(rejected_token, "equal-looking but non-owned device token is rejected");

    bool rejected_unbounded_interval{};
    try {
        procedure::CoDetectProductionAdapterLimits limits;
        limits.max_screenshot_interval_ms = std::numeric_limits<std::uint64_t>::max();
        static_cast<void>(procedure::make_co_detect_production_pins(
            port, second, bundle, bundle->generation(), limits));
    } catch (const std::invalid_argument&) {
        rejected_unbounded_interval = true;
    }
    check(rejected_unbounded_interval,
          "unbounded screenshot interval limits are rejected before wait multiplication");

    port->identity = first;
    port->retarget_after_capture = second;
    const auto pins = procedure::make_co_detect_production_pins(
        port, first, bundle, bundle->generation());
    Control control;
    const auto result = pins.session->capture(control);
    check(std::get_if<procedure::CoDetectOperationError>(&result) &&
              std::get<procedure::CoDetectOperationError>(result) ==
                  procedure::CoDetectOperationError::SessionChanged,
          "capture postflight rejects an owner epoch switch");
}

}  // namespace

int main()
{
    try {
        test_owned_matching_and_missing_feature();
        test_tolerance_control_and_frame_bounds();
        test_immutable_retarget_and_factory_guards();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }
    if (failures != 0) {
        std::cerr << failures << " production adapter test(s) failed\n";
        return 1;
    }
    std::cout << "co-detect production adapter tests passed\n";
    return 0;
}
