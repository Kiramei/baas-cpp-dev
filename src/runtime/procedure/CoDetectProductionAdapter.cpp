#include "runtime/procedure/CoDetectProductionAdapter.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace baas::runtime::procedure {
namespace {

[[nodiscard]] std::optional<CoDetectOperationError> control_error(
    const CoDetectControl& control) noexcept
{
    switch (control.poll()) {
    case CoDetectControlState::Proceed: return std::nullopt;
    case CoDetectControlState::ContextDeadlineExceeded:
        return CoDetectOperationError::ContextDeadlineExceeded;
    case CoDetectControlState::CallDeadlineExceeded:
        return CoDetectOperationError::CallDeadlineExceeded;
    case CoDetectControlState::Cancelled:
        return CoDetectOperationError::Cancelled;
    case CoDetectControlState::SessionChanged:
        return CoDetectOperationError::SessionChanged;
    }
    return CoDetectOperationError::Internal;
}

[[nodiscard]] bool same_identity(
    const CoDetectProductionDeviceIdentity& left,
    const CoDetectProductionDeviceIdentity& right) noexcept
{
    return left.device_id == right.device_id && left.profile == right.profile &&
        left.session_epoch == right.session_epoch && left.android == right.android;
}

[[nodiscard]] bool valid_limits(const CoDetectProductionAdapterLimits& limits) noexcept
{
    if (limits.frame_width == 0 || limits.frame_width > 1'280 ||
        limits.frame_height == 0 || limits.frame_height > 720 ||
        limits.max_screenshot_interval_ms == 0 ||
        limits.max_screenshot_interval_ms > 60'000)
        return false;
    const auto pixels = static_cast<std::uint64_t>(limits.frame_width) *
        limits.frame_height * 3U;
    return pixels <= limits.max_frame_bytes && limits.max_frame_bytes <= 16U * 1024U * 1024U;
}

class ProductionFrame final : public CoDetectFrame {
public:
    ProductionFrame(
        std::shared_ptr<const CoDetectProductionBgrFrame> frame,
        CoDetectProductionDeviceIdentity frozen)
        : frame_(std::move(frame)), frozen_(std::move(frozen)) {}
    [[nodiscard]] std::string_view device_id() const noexcept override {
        return frozen_.device_id;
    }
    [[nodiscard]] CoDetectProfile profile() const noexcept override {
        return frozen_.profile;
    }
    [[nodiscard]] std::uint64_t session_epoch() const noexcept override {
        return frozen_.session_epoch;
    }
    [[nodiscard]] const CoDetectProductionBgrFrame& frame() const noexcept { return *frame_; }
    [[nodiscard]] const std::shared_ptr<const CoDetectProductionBgrFrame>& owned() const noexcept {
        return frame_;
    }
private:
    std::shared_ptr<const CoDetectProductionBgrFrame> frame_;
    CoDetectProductionDeviceIdentity frozen_;
};

class ProductionSession final : public CoDetectPinnedDeviceSession {
public:
    ProductionSession(
        std::shared_ptr<CoDetectProductionDevicePort> port,
        std::shared_ptr<const CoDetectProductionDeviceIdentity> identity,
        const CoDetectProductionAdapterLimits limits)
        : port_(std::move(port)), identity_(std::move(identity)),
          frozen_(*identity_), limits_(limits) {}

    [[nodiscard]] const std::string& device_id() const noexcept override {
        return frozen_.device_id;
    }
    [[nodiscard]] CoDetectProfile profile() const noexcept override { return frozen_.profile; }
    [[nodiscard]] std::uint64_t session_epoch() const noexcept override {
        return frozen_.session_epoch;
    }
    [[nodiscard]] bool identity_valid() const noexcept override {
        const auto current = port_->current_identity();
        return current && current.get() == identity_.get() && same_identity(*current, frozen_);
    }
    [[nodiscard]] bool is_android() const noexcept override { return frozen_.android; }
    [[nodiscard]] std::uint64_t monotonic_ms() const noexcept override {
        return port_->monotonic_ms();
    }
    [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override {
        const auto value = port_->screenshot_interval_ms();
        return value != 0 && value <= limits_.max_screenshot_interval_ms
            ? value : limits_.max_screenshot_interval_ms;
    }

    [[nodiscard]] std::shared_ptr<const CoDetectFrame> latest_frame() const noexcept override
    {
        try {
            if (!identity_valid()) return {};
            const auto latest = port_->latest_frame();
            if (!latest || latest->identity.get() != identity_.get()) return {};
            auto owned = own_frame(*latest);
            if (!identity_valid()) return {};
            return owned;
        } catch (...) {
            return {};
        }
    }

    void publish_latest_frame(std::shared_ptr<const CoDetectFrame> frame) noexcept override
    {
        if (!identity_valid()) return;
        const auto production = std::dynamic_pointer_cast<const ProductionFrame>(frame);
        if (!production || production->owned()->identity.get() != identity_.get()) return;
        static_cast<void>(port_->publish_latest_frame(production->owned()));
    }

    [[nodiscard]] CoDetectResult<std::shared_ptr<const CoDetectFrame>> capture(
        const CoDetectControl& control) override
    {
        if (const auto error = guard(control)) return *error;
        try {
            auto captured = port_->capture(control);
            if (const auto* error = std::get_if<CoDetectOperationError>(&captured)) return *error;
            if (const auto error = guard(control)) return *error;
            auto owned = own_frame(std::get<CoDetectProductionBgrFrame>(captured));
            if (!owned) return CoDetectOperationError::Unavailable;
            if (const auto error = guard(control)) return *error;
            return std::shared_ptr<const CoDetectFrame>{std::move(owned)};
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::Internal;
        }
    }

    [[nodiscard]] CoDetectResult<std::monostate> click(
        const CoDetectClick click, const CoDetectControl& control) override
    {
        if (const auto error = guard(control)) return *error;
        try {
            auto result = port_->click(click, control);
            if (const auto error = guard(control)) return *error;
            return result;
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::Internal;
        }
    }

    [[nodiscard]] CoDetectResult<std::monostate> wait(
        const std::uint64_t milliseconds, const CoDetectControl& control) override
    {
        if (milliseconds > limits_.max_screenshot_interval_ms * 64U)
            return CoDetectOperationError::Unavailable;
        if (const auto error = guard(control)) return *error;
        try {
            auto result = port_->wait(milliseconds, control);
            if (const auto error = guard(control)) return *error;
            return result;
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::Internal;
        }
    }

    [[nodiscard]] CoDetectResult<bool> foreground_matches(
        const CoDetectControl& control) override
    {
        if (const auto error = guard(control)) return *error;
        try {
            auto result = port_->foreground_matches(control);
            if (const auto error = guard(control)) return *error;
            return result;
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::Internal;
        }
    }

private:
    [[nodiscard]] std::optional<CoDetectOperationError> guard(
        const CoDetectControl& control) const noexcept
    {
        if (const auto error = control_error(control)) return error;
        if (!identity_valid()) return CoDetectOperationError::SessionChanged;
        return std::nullopt;
    }

    [[nodiscard]] std::shared_ptr<const ProductionFrame> own_frame(
        const CoDetectProductionBgrFrame& source) const
    {
        if (!source.identity || source.identity.get() != identity_.get() ||
            !same_identity(*source.identity, frozen_) ||
            source.width != limits_.frame_width || source.height != limits_.frame_height ||
            !source.pixels || source.row_stride < static_cast<std::size_t>(source.width) * 3U ||
            source.row_stride > limits_.max_frame_bytes ||
            source.height > limits_.max_frame_bytes / source.row_stride ||
            source.pixels->size() != source.row_stride * source.height)
            return {};
        const auto packed_stride = static_cast<std::size_t>(source.width) * 3U;
        const auto packed_size = packed_stride * source.height;
        if (packed_size > limits_.max_frame_bytes) return {};
        auto pixels = std::make_shared<std::vector<std::byte>>(packed_size);
        for (std::uint32_t row = 0; row < source.height; ++row)
            std::memcpy(pixels->data() + static_cast<std::size_t>(row) * packed_stride,
                        source.pixels->data() + static_cast<std::size_t>(row) * source.row_stride,
                        packed_stride);
        auto owned = std::make_shared<CoDetectProductionBgrFrame>(CoDetectProductionBgrFrame{
            identity_, source.width, source.height, packed_stride,
            std::shared_ptr<const std::vector<std::byte>>{std::move(pixels)}});
        return std::make_shared<const ProductionFrame>(std::move(owned), frozen_);
    }

    std::shared_ptr<CoDetectProductionDevicePort> port_;
    std::shared_ptr<const CoDetectProductionDeviceIdentity> identity_;
    CoDetectProductionDeviceIdentity frozen_;
    CoDetectProductionAdapterLimits limits_;
};

class ProductionFeatureView final : public CoDetectPinnedFeatureView {
public:
    ProductionFeatureView(
        std::shared_ptr<const CoDetectSupportBundle> bundle,
        std::string generation, const CoDetectProfile profile) noexcept
        : bundle_(std::move(bundle)), generation_(std::move(generation)), profile_(profile) {}

    [[nodiscard]] std::string_view generation() const noexcept override { return generation_; }
    [[nodiscard]] CoDetectProfile profile() const noexcept override { return profile_; }
    [[nodiscard]] bool identity_valid() const noexcept override {
        return bundle_ && bundle_->generation() == generation_ && bundle_->profile() == profile_;
    }

    [[nodiscard]] CoDetectResult<bool> match_rgb(
        const CoDetectFrame& base, const std::string_view feature,
        const CoDetectControl& control) override
    {
        if (const auto error = guard(base, control)) return *error;
        const auto* source = bundle_->find_rgb(feature);
        if (!source) return false;
        const auto& frame = static_cast<const ProductionFrame&>(base).frame();
        for (std::size_t index = 0; index < source->samples.size(); ++index) {
            if ((index & 63U) == 0U)
                if (const auto error = control_error(control)) return *error;
            const auto& sample = source->samples[index];
            if (sample.x >= frame.width || sample.y >= frame.height) return false;
            const auto offset = static_cast<std::size_t>(sample.y) * frame.row_stride +
                static_cast<std::size_t>(sample.x) * 3U;
            const auto* pixel = frame.pixels->data() + offset;
            const auto blue = std::to_integer<std::uint8_t>(pixel[0]);
            const auto green = std::to_integer<std::uint8_t>(pixel[1]);
            const auto red = std::to_integer<std::uint8_t>(pixel[2]);
            if (red < sample.red[0] || red > sample.red[1] ||
                green < sample.green[0] || green > sample.green[1] ||
                blue < sample.blue[0] || blue > sample.blue[1])
                return false;
        }
        if (const auto error = guard(base, control)) return *error;
        return true;
    }

    [[nodiscard]] CoDetectResult<bool> match_image(
        const CoDetectFrame& base, const std::string_view feature,
        const CoDetectImageMatch match, const CoDetectControl& control) override
    {
        if (const auto error = guard(base, control)) return *error;
        const auto* source = bundle_->find_image(feature);
        if (!source) return false;
        try {
            const auto& frame = static_cast<const ProductionFrame&>(base).frame();
            const auto crop = source->crop();
            if (crop.right > frame.width || crop.bottom > frame.height ||
                crop.left >= crop.right || crop.top >= crop.bottom)
                return false;
            const auto crop_width = static_cast<int>(crop.right - crop.left);
            const auto crop_height = static_cast<int>(crop.bottom - crop.top);
            const cv::Mat image(
                static_cast<int>(frame.height), static_cast<int>(frame.width), CV_8UC3,
                const_cast<std::byte*>(frame.pixels->data()), frame.row_stride);
            const cv::Mat cropped = image(cv::Rect{
                static_cast<int>(crop.left), static_cast<int>(crop.top),
                crop_width, crop_height});
            const cv::Mat templ(
                static_cast<int>(source->height()), static_cast<int>(source->width()), CV_8UC3,
                const_cast<std::byte*>(source->bgr_pixels().data()), source->row_stride());
            const auto tolerance = match.rgb_diff.value_or(source->mean_rgb_tolerance());
            const auto crop_mean = cv::mean(cropped);
            const auto template_mean = cv::mean(templ);
            for (std::size_t channel = 0; channel < 3; ++channel)
                if (std::abs(crop_mean[static_cast<int>(channel)] -
                             template_mean[static_cast<int>(channel)]) > tolerance)
                    return false;
            if (const auto error = control_error(control)) return *error;
            cv::Mat resized;
            cv::resize(cropped, resized, templ.size(), 0.0, 0.0, cv::INTER_AREA);
            if (const auto error = control_error(control)) return *error;
            cv::Mat result;
            cv::matchTemplate(resized, templ, result, cv::TM_CCOEFF_NORMED);
            const auto similarity = static_cast<double>(result.at<float>(0, 0));
            if (const auto error = guard(base, control)) return *error;
            const auto threshold = match.threshold.value_or(
                static_cast<double>(source->threshold_milli()) / 1'000.0);
            return std::isfinite(similarity) && similarity > threshold;
        } catch (const cv::Exception& error) {
            return error.code == cv::Error::StsNoMem
                ? CoDetectOperationError::ResourceExhausted
                : CoDetectOperationError::Internal;
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::Internal;
        }
    }

private:
    [[nodiscard]] std::optional<CoDetectOperationError> guard(
        const CoDetectFrame& base, const CoDetectControl& control) const noexcept
    {
        if (const auto error = control_error(control)) return error;
        if (!identity_valid()) return CoDetectOperationError::SessionChanged;
        const auto* frame = dynamic_cast<const ProductionFrame*>(&base);
        if (!frame || frame->profile() != profile_) return CoDetectOperationError::SessionChanged;
        return std::nullopt;
    }

    std::shared_ptr<const CoDetectSupportBundle> bundle_;
    std::string generation_;
    CoDetectProfile profile_{};
};

}  // namespace

CoDetectProductionPins make_co_detect_production_pins(
    std::shared_ptr<CoDetectProductionDevicePort> port,
    std::shared_ptr<const CoDetectProductionDeviceIdentity> identity,
    std::shared_ptr<const CoDetectSupportBundle> bundle,
    const std::string_view expected_generation,
    const CoDetectProductionAdapterLimits& limits)
{
    if (!port || !identity || !bundle || identity->device_id.empty() ||
        identity->session_epoch == 0 || co_detect_profile_name(identity->profile).empty() ||
        expected_generation.empty() || !valid_limits(limits) ||
        bundle->generation() != expected_generation || bundle->profile() != identity->profile ||
        port->current_identity().get() != identity.get())
        throw std::invalid_argument("co-detect production pin identity is invalid");
    const auto interval = port->screenshot_interval_ms();
    if (interval == 0 || interval > limits.max_screenshot_interval_ms)
        throw std::invalid_argument("co-detect screenshot interval is invalid");
    CoDetectProductionPins result;
    result.session = std::make_shared<ProductionSession>(port, identity, limits);
    result.features = std::make_shared<ProductionFeatureView>(
        std::move(bundle), std::string{expected_generation}, identity->profile);
    return result;
}

std::shared_ptr<::baas::script::host::ProcedureExecutor>
make_activated_co_detect_production_executor(
    std::shared_ptr<const RuntimeProcedureActivation> activation,
    const std::string_view procedure_id,
    std::shared_ptr<CoDetectProductionDevicePort> port,
    std::shared_ptr<const CoDetectProductionDeviceIdentity> identity,
    std::shared_ptr<const CoDetectSupportBundle> bundle,
    const CoDetectProductionAdapterLimits& limits)
{
    if (!activation || !bundle || activation->generation() != bundle->generation())
        throw std::invalid_argument("co-detect production activation identity is invalid");
    auto pins = make_co_detect_production_pins(
        std::move(port), std::move(identity), bundle, activation->generation(), limits);
    return make_activated_co_detect_python_compat_executor(
        std::move(activation), procedure_id, std::move(pins.session),
        std::move(pins.features));
}

}  // namespace baas::runtime::procedure
