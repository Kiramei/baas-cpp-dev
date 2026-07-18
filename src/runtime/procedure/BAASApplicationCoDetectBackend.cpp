#include "runtime/procedure/BAASConnectionCoDetectPort.h"

#include "BAAS.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>

namespace baas::runtime::procedure {
namespace {

class CheckpointFailure final {
public:
    explicit CheckpointFailure(const CoDetectOperationError value) noexcept : value_(value) {}
    [[nodiscard]] CoDetectOperationError value() const noexcept { return value_; }
private:
    CoDetectOperationError value_;
};

[[nodiscard]] std::string production_profile_name(
    const std::string& server, const std::string& language)
{
    if (server == "CN" || server == "JP") return server;
    if (server != "Global") return {};
    if (language == "en-us" || language == "zh-tw" || language == "ko-kr")
        return server + "_" + language;
    return {};
}

class BAASApplicationCoDetectBackend final : public BAASConnectionCoDetectBackend {
public:
    explicit BAASApplicationCoDetectBackend(std::shared_ptr<BAAS> application)
        : application_(std::move(application))
    {
        if (!application_ || !application_->get_connection() ||
            !application_->get_screenshot() || !application_->get_control())
            throw std::invalid_argument("BAAS application has no complete device stack");
        connection_ = application_->get_connection();
        screenshot_ = application_->get_screenshot();
        control_ = application_->get_control();
        device_id_ = connection_->get_serial();
        package_name_ = connection_->get_package_name();
        server_ = connection_->get_server();
        language_ = connection_->get_language();
        profile_name_ = production_profile_name(server_, language_);
        if (device_id_.empty() || package_name_.empty() || profile_name_.empty())
            throw std::invalid_argument("BAAS application device identity is incomplete");
    }

    [[nodiscard]] const std::string& device_id() const noexcept override
    {
        return device_id_;
    }

    [[nodiscard]] const std::string& profile_name() const noexcept override
    {
        return profile_name_;
    }

    [[nodiscard]] bool identity_valid() const noexcept override
    {
        try {
            return application_ && application_->get_connection() == connection_ &&
                application_->get_screenshot() == screenshot_ &&
                application_->get_control() == control_ &&
                connection_->get_serial() == device_id_ &&
                connection_->get_package_name() == package_name_ &&
                connection_->get_server() == server_ &&
                connection_->get_language() == language_ &&
                production_profile_name(connection_->get_server(),
                                        connection_->get_language()) == profile_name_;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override
    {
        try {
            if (!identity_valid()) return 0;
            const auto value = screenshot_->get_interval();
            return value < 0 ? 0U : static_cast<std::uint64_t>(value);
        } catch (...) {
            return 0;
        }
    }

    [[nodiscard]] CoDetectResult<BAASConnectionCoDetectRawFrame> capture(
        const BAASConnectionCoDetectCheckpoint& checkpoint) override
    {
        try {
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            checkpoint_or_throw(checkpoint);
            application_->update_screenshot_array_controlled(
                [&checkpoint] { checkpoint_or_throw(checkpoint); });
            cv::Mat source;
            application_->get_latest_screenshot_clone(source);
            checkpoint_or_throw(checkpoint);
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            if (source.empty() || source.type() != CV_8UC3 || source.cols <= 0 ||
                source.rows <= 0 || source.cols > 7'680 || source.rows > 4'320 ||
                static_cast<std::uint64_t>(source.cols) * 9U !=
                    static_cast<std::uint64_t>(source.rows) * 16U)
                return CoDetectOperationError::Unavailable;

            cv::Mat canonical;
            if (source.cols == static_cast<int>(baas_connection_co_detect_width) &&
                source.rows == static_cast<int>(baas_connection_co_detect_height))
                canonical = source;
            else
                cv::resize(source, canonical,
                           cv::Size{static_cast<int>(baas_connection_co_detect_width),
                                    static_cast<int>(baas_connection_co_detect_height)},
                           0.0, 0.0, cv::INTER_AREA);
            checkpoint_or_throw(checkpoint);
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            if (canonical.empty() || canonical.type() != CV_8UC3 ||
                canonical.cols != static_cast<int>(baas_connection_co_detect_width) ||
                canonical.rows != static_cast<int>(baas_connection_co_detect_height))
                return CoDetectOperationError::Unavailable;

            BAASConnectionCoDetectRawFrame result;
            result.width = baas_connection_co_detect_width;
            result.height = baas_connection_co_detect_height;
            result.row_stride = baas_connection_co_detect_row_stride;
            result.pixels.resize(baas_connection_co_detect_frame_bytes);
            for (std::uint32_t row = 0; row < result.height; ++row)
                std::memcpy(result.pixels.data() +
                                static_cast<std::size_t>(row) * result.row_stride,
                            canonical.ptr(static_cast<int>(row)), result.row_stride);
            checkpoint_or_throw(checkpoint);
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            return result;
        } catch (const CheckpointFailure& error) {
            return error.value();
        } catch (const cv::Exception& error) {
            return error.code == cv::Error::StsNoMem
                ? CoDetectOperationError::ResourceExhausted
                : CoDetectOperationError::DeviceDisconnected;
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::DeviceDisconnected;
        }
    }

    [[nodiscard]] CoDetectResult<std::monostate> click(
        const CoDetectClick click) override
    {
        try {
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            application_->click(click.x, click.y, 1, 0, 0, 0.0, 0.0, 0.0,
                                "co-detect runtime");
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            return std::monostate{};
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::DeviceDisconnected;
        }
    }

    [[nodiscard]] CoDetectResult<bool> foreground_matches() override
    {
        try {
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            std::string package;
            std::string activity;
            int pid{};
            connection_->current_app(package, activity, pid);
            if (!identity_valid()) return CoDetectOperationError::SessionChanged;
            return package == package_name_;
        } catch (const std::bad_alloc&) {
            return CoDetectOperationError::ResourceExhausted;
        } catch (...) {
            return CoDetectOperationError::DeviceDisconnected;
        }
    }

private:
    static void checkpoint_or_throw(const BAASConnectionCoDetectCheckpoint& checkpoint)
    {
        auto result = checkpoint();
        if (const auto* error = std::get_if<CoDetectOperationError>(&result))
            throw CheckpointFailure{*error};
    }

    std::shared_ptr<BAAS> application_;
    BAASConnection* connection_{};
    BAASScreenshot* screenshot_{};
    BAASControl* control_{};
    std::string device_id_;
    std::string package_name_;
    std::string server_;
    std::string language_;
    std::string profile_name_;
};

}  // namespace

std::shared_ptr<BAASConnectionCoDetectBackend>
make_baas_application_co_detect_backend(std::shared_ptr<BAAS> application)
{
    return std::make_shared<BAASApplicationCoDetectBackend>(std::move(application));
}

}  // namespace baas::runtime::procedure
