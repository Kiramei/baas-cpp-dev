#include "runtime/procedure/BAASConnectionCoDetectPort.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

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
        return state.load(std::memory_order_acquire);
    }

    std::atomic<procedure::CoDetectControlState> state{
        procedure::CoDetectControlState::Proceed};
    mutable std::atomic<std::size_t> polls{};
};

class Backend final : public procedure::BAASConnectionCoDetectBackend {
public:
    [[nodiscard]] const std::string& device_id() const noexcept override
    {
        return device;
    }

    [[nodiscard]] const std::string& profile_name() const noexcept override
    {
        return profile;
    }

    [[nodiscard]] bool identity_valid() const noexcept override
    {
        return valid.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override
    {
        return interval;
    }

    [[nodiscard]] procedure::CoDetectResult<procedure::BAASConnectionCoDetectRawFrame>
    capture(const procedure::BAASConnectionCoDetectCheckpoint& checkpoint) override
    {
        ++captures;
        if (auto checked = checkpoint();
            std::holds_alternative<procedure::CoDetectOperationError>(checked))
            return std::get<procedure::CoDetectOperationError>(checked);
        procedure::BAASConnectionCoDetectRawFrame frame;
        frame.width = width;
        frame.height = height;
        frame.row_stride = stride;
        frame.pixels.assign(bytes, std::byte{0x2a});
        if (invalidate_after_capture) valid.store(false, std::memory_order_release);
        return frame;
    }

    [[nodiscard]] procedure::CoDetectResult<std::monostate> click(
        const procedure::CoDetectClick value) override
    {
        ++clicks;
        last_click = value;
        std::unique_lock lock(click_mutex);
        click_started = true;
        click_condition.notify_all();
        click_condition.wait(lock, [this] { return !block_click; });
        return std::monostate{};
    }

    [[nodiscard]] procedure::CoDetectResult<bool> foreground_matches() override
    {
        ++foreground_checks;
        return foreground;
    }

    std::string device{"emulator-5556"};
    std::string profile{"JP"};
    std::atomic_bool valid{true};
    std::uint64_t interval{300};
    std::uint32_t width{procedure::baas_connection_co_detect_width};
    std::uint32_t height{procedure::baas_connection_co_detect_height};
    std::size_t stride{procedure::baas_connection_co_detect_row_stride};
    std::size_t bytes{procedure::baas_connection_co_detect_frame_bytes};
    bool invalidate_after_capture{};
    bool foreground{true};
    std::atomic<std::size_t> captures{};
    std::atomic<std::size_t> clicks{};
    std::atomic<std::size_t> foreground_checks{};
    procedure::CoDetectClick last_click{};
    std::mutex click_mutex;
    std::condition_variable click_condition;
    bool block_click{};
    bool click_started{};
};

[[nodiscard]] bool error_is(
    const auto& result, const procedure::CoDetectOperationError expected)
{
    const auto* error = std::get_if<procedure::CoDetectOperationError>(&result);
    return error && *error == expected;
}

void test_owned_capture_cache_and_boundaries()
{
    procedure::BAASConnectionCoDetectOwner owner;
    auto backend = std::make_shared<Backend>();
    const auto binding = owner.activate(backend, procedure::CoDetectProfile::jp, 1);
    check(binding.port && binding.identity && binding.identity->device_id == "emulator-5556" &&
              binding.identity->profile == procedure::CoDetectProfile::jp &&
              binding.identity->session_epoch == 1 && binding.identity->android,
          "activation publishes one exact immutable Android identity");
    check(binding.port->current_identity().get() == binding.identity.get() &&
              binding.port->screenshot_interval_ms() == 300,
          "port exposes the exact owner token and frozen backend interval");

    Control control;
    auto captured = binding.port->capture(control);
    const auto* frame = std::get_if<procedure::CoDetectProductionBgrFrame>(&captured);
    check(frame && frame->identity.get() == binding.identity.get() &&
              frame->width == 1'280 && frame->height == 720 &&
              frame->row_stride == 1'280U * 3U && frame->pixels &&
              frame->pixels->size() == 1'280U * 720U * 3U,
          "capture returns owned canonical packed BGR bytes");
    const auto latest = binding.port->latest_frame();
    check(latest && latest->pixels && latest->identity.get() == binding.identity.get(),
          "capture publishes only an owned same-token latest frame");

    backend->width = 1'279;
    backend->stride = 1'279U * 3U;
    backend->bytes = backend->stride * backend->height;
    captured = binding.port->capture(control);
    check(error_is(captured, procedure::CoDetectOperationError::Unavailable),
          "non-1280x720 or non-packed backend frames fail closed");
    backend->width = procedure::baas_connection_co_detect_width;
    backend->stride = procedure::baas_connection_co_detect_row_stride;
    backend->bytes = procedure::baas_connection_co_detect_frame_bytes;

    const auto clicks_before = backend->clicks.load();
    auto click = binding.port->click({-1, 2}, control);
    check(error_is(click, procedure::CoDetectOperationError::Unavailable) &&
              backend->clicks.load() == clicks_before,
          "match-only and out-of-frame clicks never reach the device backend");
    click = binding.port->click({2, -1}, control);
    check(error_is(click, procedure::CoDetectOperationError::Unavailable) &&
              backend->clicks.load() == clicks_before,
          "a negative y match-only sentinel never reaches the device backend");
    click = binding.port->click({-1, -1}, control);
    check(error_is(click, procedure::CoDetectOperationError::Unavailable) &&
              backend->clicks.load() == clicks_before,
          "the two-negative match-only sentinel never reaches the device backend");
    click = binding.port->click({640, 360}, control);
    check(std::holds_alternative<std::monostate>(click) &&
              backend->last_click == procedure::CoDetectClick{640, 360},
          "bounded canonical clicks reach BAAS control exactly once");
    click = binding.port->click({1'280, 720}, control);
    check(std::holds_alternative<std::monostate>(click) &&
              backend->last_click == procedure::CoDetectClick{1'280, 720},
          "the Python-compatible inclusive clamp boundary reaches BAAS control");
    const auto bounded_clicks = backend->clicks.load();
    click = binding.port->click({1'281, 720}, control);
    check(error_is(click, procedure::CoDetectOperationError::Unavailable) &&
              backend->clicks.load() == bounded_clicks,
          "coordinates beyond the inclusive clamp boundary fail closed");
    const auto foreground = binding.port->foreground_matches(control);
    check(std::get_if<bool>(&foreground) && std::get<bool>(foreground),
          "foreground check delegates to the frozen BAAS connection package");

    control.state.store(procedure::CoDetectControlState::Cancelled);
    const auto captures_before = backend->captures.load();
    captured = binding.port->capture(control);
    check(error_is(captured, procedure::CoDetectOperationError::Cancelled) &&
              backend->captures.load() == captures_before,
          "control cancellation is checked before device capture");
}

void test_postflight_identity_and_activation_guards()
{
    procedure::BAASConnectionCoDetectOwner owner;
    auto backend = std::make_shared<Backend>();
    const auto first = owner.activate(backend, procedure::CoDetectProfile::jp, 7);
    Control control;
    backend->invalidate_after_capture = true;
    const auto captured = first.port->capture(control);
    check(error_is(captured, procedure::CoDetectOperationError::SessionChanged),
          "in-place connection/config drift fails capture postflight");
    check(!first.port->current_identity(),
          "in-place drift withdraws identity before cached-frame-only vision");
    backend->valid.store(true);
    backend->invalidate_after_capture = false;
    check(first.port->current_identity().get() == first.identity.get(),
          "restoring the exact backend snapshot restores the owner identity view");
    check(!first.port->latest_frame(),
          "a failed postflight capture is never published after identity ABA recovery");

    backend->interval = 0;
    check(first.port->screenshot_interval_ms() == 0,
          "a dynamically invalid screenshot interval fails closed instead of clamping");
    backend->interval = 60'001;
    check(first.port->screenshot_interval_ms() == 0,
          "an over-limit dynamic screenshot interval fails closed");
    backend->interval = 300;

    backend->valid.store(false);
    check(!first.port->current_identity(),
          "in-place backend drift withdraws identity before cached-frame-only vision");
    backend->valid.store(true);

    bool rejected_epoch{};
    try {
        static_cast<void>(owner.activate(std::make_shared<Backend>(),
                                         procedure::CoDetectProfile::jp, 7));
    } catch (const std::invalid_argument&) {
        rejected_epoch = true;
    }
    check(rejected_epoch, "session epochs must strictly increase");

    auto wrong_profile = std::make_shared<Backend>();
    wrong_profile->profile = "CN";
    bool rejected_profile{};
    try {
        static_cast<void>(owner.activate(wrong_profile,
                                         procedure::CoDetectProfile::jp, 8));
    } catch (const std::invalid_argument&) {
        rejected_profile = true;
    }
    check(rejected_profile, "profile enum must match the frozen BAAS server/locale key");

    bool rejected_non_android{};
    try {
        static_cast<void>(owner.activate(std::make_shared<Backend>(),
                                         procedure::CoDetectProfile::jp, 8, false));
    } catch (const std::invalid_argument&) {
        rejected_non_android = true;
    }
    check(rejected_non_android, "the BAASConnection production port rejects non-Android leases");

    auto zero_interval = std::make_shared<Backend>();
    zero_interval->interval = 0;
    bool rejected_zero_interval{};
    try {
        static_cast<void>(owner.activate(zero_interval,
                                         procedure::CoDetectProfile::jp, 8));
    } catch (const std::invalid_argument&) {
        rejected_zero_interval = true;
    }
    check(rejected_zero_interval, "activation rejects a zero screenshot interval");
}

void test_replacement_linearizes_effects_and_wakes_waits()
{
    using namespace std::chrono_literals;
    procedure::BAASConnectionCoDetectOwner owner;
    auto first_backend = std::make_shared<Backend>();
    first_backend->block_click = true;
    const auto first = owner.activate(first_backend, procedure::CoDetectProfile::jp, 1);
    Control control;

    auto click_future = std::async(std::launch::async, [&] {
        return first.port->click({12, 34}, control);
    });
    {
        std::unique_lock lock(first_backend->click_mutex);
        first_backend->click_condition.wait(lock, [&] { return first_backend->click_started; });
    }
    auto second_backend = std::make_shared<Backend>();
    auto activation_future = std::async(std::launch::async, [&] {
        return owner.activate(second_backend, procedure::CoDetectProfile::jp, 2);
    });
    check(activation_future.wait_for(30ms) == std::future_status::timeout,
          "replacement waits for an already-linearized device effect");
    {
        std::scoped_lock lock(first_backend->click_mutex);
        first_backend->block_click = false;
    }
    first_backend->click_condition.notify_all();
    const auto click_result = click_future.get();
    const auto second = activation_future.get();
    check(std::holds_alternative<std::monostate>(click_result) &&
              !first.port->current_identity() &&
              second.port->current_identity().get() == second.identity.get(),
          "replacement invalidates rather than retargets the old port");
    const auto clicks_before = first_backend->clicks.load();
    const auto stale_click = first.port->click({1, 1}, control);
    check(error_is(stale_click, procedure::CoDetectOperationError::SessionChanged) &&
              first_backend->clicks.load() == clicks_before,
          "no stale effect reaches the old backend after replacement");

    auto wait_future = std::async(std::launch::async, [&] {
        return second.port->wait(5'000, control);
    });
    std::this_thread::sleep_for(20ms);
    const auto third = owner.activate(
        std::make_shared<Backend>(), procedure::CoDetectProfile::jp, 3);
    const auto wait_result = wait_future.get();
    check(error_is(wait_result, procedure::CoDetectOperationError::SessionChanged) &&
              third.port->current_identity().get() == third.identity.get(),
          "replacement wakes bounded waits and makes them fail closed");

    owner.invalidate();
    check(!third.port->current_identity(), "explicit invalidation withdraws the final token");
}

}  // namespace

int main()
{
    try {
        test_owned_capture_cache_and_boundaries();
        test_postflight_identity_and_activation_guards();
        test_replacement_linearizes_effects_and_wakes_waits();
    } catch (const std::exception& error) {
        ++failures;
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
    }
    if (failures != 0) {
        std::cerr << failures << " BAAS connection co-detect port test(s) failed\n";
        return 1;
    }
    std::cout << "BAAS connection co-detect port tests passed\n";
    return 0;
}
