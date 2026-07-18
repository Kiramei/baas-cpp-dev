//
// Created by pc on 2024/8/9.
//

#include "device/screenshot/BAASScreenshot.h"
#include "device/ExactBackendLifetime.h"
#include "device/screenshot/ScreenshotInterval.h"

#include "config/BAASStaticConfig.h"
#include "device/screenshot/AdbScreenshot.h"
#include "device/screenshot/ScrcpyScreenshot.h"
#ifdef _WIN32
#include "device/screenshot/NemuScreenshot.h"
#include "device/screenshot/LdopenglScreenshot.h"
#endif // _WIN32

using namespace std;

BAAS_NAMESPACE_BEGIN
const set<string> BAASScreenshot::available_methods = {
         "scrcpy"
        ,"adb"
#ifdef _WIN32
        ,"nemu"
        ,"ldopengl"
#endif // _WIN32
};

BAASScreenshot::BAASScreenshot(
        const std::string& method,
        BAASConnection* connection,
        const double interval
)
{
    assert(connection != nullptr);
    this->connection = connection;
    logger = this->connection->get_logger();

    logger->BAASInfo("Available screenshot methods : ");
    int cnt = 0;
    for (const auto& m: available_methods) {
        logger->BAASInfo(to_string(++cnt) + " : " + m);
    }

    last_screenshot_time = BAASChronoUtil::getCurrentTimeMS();
    screenshot_instance = nullptr;
    set_screenshot_method(method);
    set_interval(interval);
}

BAASScreenshot::~BAASScreenshot() noexcept
{
    destroy_screenshot_instance(true);
}

void BAASScreenshot::init()
{
    screenshot_instance->init();
}

void BAASScreenshot::screenshot(cv::Mat& img)
{
    ensure_interval();
    screenshot_instance->screenshot(img);
    last_screenshot_time = BAASChronoUtil::getCurrentTimeMS();
}

void BAASScreenshot::screenshot_controlled(
    cv::Mat& img, const std::function<void()>& checkpoint)
{
    ensure_interval_controlled(checkpoint);
    checkpoint();
    screenshot_instance->screenshot(img);
    last_screenshot_time = BAASChronoUtil::getCurrentTimeMS();
    checkpoint();
}

void BAASScreenshot::immediate_screenshot(cv::Mat& img)
{
    screenshot_instance->screenshot(img);
}

void BAASScreenshot::ensure_interval() const
{
    const auto current_time = BAASChronoUtil::getCurrentTimeMS();
    const auto elapsed = current_time - last_screenshot_time;
    const auto difference = screenshot_interval_remaining_ms(interval, elapsed);
    if (difference > 0) {
        BAASChronoUtil::sleepMS(difference);
    }
}

void BAASScreenshot::ensure_interval_controlled(
    const std::function<void()>& checkpoint) const
{
    const auto current_time = BAASChronoUtil::getCurrentTimeMS();
    const auto elapsed = current_time - last_screenshot_time;
    const auto difference = screenshot_interval_remaining_ms(interval, elapsed);
    wait_screenshot_interval_slices(
        difference,
        [](const int slice) { BAASChronoUtil::sleepMS(slice); },
        checkpoint);
}

void BAASScreenshot::set_interval(const double value) noexcept
{
    if (!valid_screenshot_interval_seconds(value)) {
        try {
            logger->BAASWarn(
                "Interval must be finite, non-negative, and fit milliseconds; using 0.3");
        } catch (...) {
        }
    }
    interval = normalize_screenshot_interval_ms(value);
    try {
        logger->BAASInfo(
            "Screenshot interval set to " + std::to_string(interval) + "ms");
    } catch (...) {
    }
}

void BAASScreenshot::exit()
{
    screenshot_instance->exit();
}

bool BAASScreenshot::is_lossy()
{
    return screenshot_instance->is_lossy();
}

void BAASScreenshot::set_screenshot_method(
        const std::string& method,
        bool exit
)
{
    if (available_methods.find(method) == available_methods.end()) {
        logger->BAASCritical("Unsupported screenshot method : [ " + method + " ]");
        throw RequestHumanTakeOver("Unsupported screenshot method: " + method);
    }

    if (method == screenshot_method) {
        logger->BAASWarn("Screenshot method already set to " + method);
        return;
    }

    logger->BAASInfo("Screenshot method : [ " + method + " ]");
    const auto install = [this, &method, exit]<class Backend>(
                             std::unique_ptr<Backend> owned) {
        std::string next_method{method};
        destroy_screenshot_instance(exit);
        screenshot_method.swap(next_method);
        screenshot_instance = owned.release();
    };
    if (method == "scrcpy") {
        install(detail::make_initialized_backend<ScrcpyScreenshot>(
            [](ScrcpyScreenshot& backend) { backend.init(); }, connection));
        return;
    }
    else if (method == "adb") {
        install(detail::make_initialized_backend<AdbScreenshot>(
            [](AdbScreenshot& backend) { backend.init(); }, connection));
        return;
    }
#ifdef _WIN32
    else if (method == "nemu") {
        install(detail::make_initialized_backend<NemuScreenshot>(
            [](NemuScreenshot& backend) { backend.init(); }, connection));
        return;
    }
    else if (method == "ldopengl") {
        install(detail::make_initialized_backend<LDOpenGLScreenshot>(
            [](LDOpenGLScreenshot& backend) { backend.init(); }, connection));
        return;
    }
#endif // _WIN32
}

void BAASScreenshot::destroy_screenshot_instance(const bool call_exit) noexcept
{
    if (screenshot_instance == nullptr) return;
    if (call_exit) {
        try {
            screenshot_instance->exit();
        } catch (...) {
        }
    }
    if (screenshot_method == "scrcpy") {
        detail::delete_exact_backend<ScrcpyScreenshot>(screenshot_instance);
    } else if (screenshot_method == "adb") {
        detail::delete_exact_backend<AdbScreenshot>(screenshot_instance);
    }
#ifdef _WIN32
    else if (screenshot_method == "nemu") {
        detail::delete_exact_backend<NemuScreenshot>(screenshot_instance);
    } else if (screenshot_method == "ldopengl") {
        detail::delete_exact_backend<LDOpenGLScreenshot>(screenshot_instance);
    }
#endif // _WIN32
}

double BAASScreenshot::get_screen_ratio()
{
    double benchmark = 1280;
    cv::Mat img;
    screenshot(img);
    logger->BAASInfo("Screenshot size : " + std::to_string(img.cols) + "x" + std::to_string(img.rows));
    double long_edge = max(img.cols, img.rows);
    double short_edge = min(img.cols, img.rows);
    double ls_ratio = 9.0 / 16.0;
    if (short_edge / long_edge != ls_ratio) {
        logger->BAASWarn("Screen ratio is not 16:9");
        throw RequestHumanTakeOver("Screen ratio incorrect");
    }
    double ratio = long_edge / benchmark;
    logger->BAASInfo("Screen ratio : " + std::to_string(ratio));
    return ratio;
}

BAAS_NAMESPACE_END
