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
        ++device_reads;
        return device;
    }

    [[nodiscard]] const std::string& profile_name() const noexcept override
    {
        ++profile_reads;
        return profile;
    }

    [[nodiscard]] bool identity_valid() const noexcept override
    {
        ++identity_checks;
        return valid.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override
    {
        ++interval_reads;
        return interval;
    }

    [[nodiscard]] procedure::CoDetectResult<procedure::BAASConnectionCoDetectRawFrame>
    capture(const procedure::BAASConnectionCoDetectCheckpoint& checkpoint) override
    {
        ++captures;
        {
            std::unique_lock lock(capture_mutex);
            capture_started = true;
            capture_condition.notify_all();
            capture_condition.wait(lock, [this] { return !block_capture; });
        }
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
        std::unique_lock lock(foreground_mutex);
        foreground_started = true;
        foreground_condition.notify_all();
        foreground_condition.wait(lock, [this] { return !block_foreground; });
        return foreground;
    }

    [[nodiscard]] std::size_t total_accesses() const noexcept
    {
        return device_reads.load() + profile_reads.load() + identity_checks.load() +
            interval_reads.load() + captures.load() + clicks.load() +
            foreground_checks.load();
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
    mutable std::atomic<std::size_t> device_reads{};
    mutable std::atomic<std::size_t> profile_reads{};
    mutable std::atomic<std::size_t> identity_checks{};
    mutable std::atomic<std::size_t> interval_reads{};
    procedure::CoDetectClick last_click{};
    std::mutex click_mutex;
    std::condition_variable click_condition;
    bool block_click{};
    bool click_started{};
    std::mutex capture_mutex;
    std::condition_variable capture_condition;
    bool block_capture{};
    bool capture_started{};
    std::mutex foreground_mutex;
    std::condition_variable foreground_condition;
    bool block_foreground{};
    bool foreground_started{};
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
    const auto initial_capture = first.port->capture(control);
    check(std::holds_alternative<procedure::CoDetectProductionBgrFrame>(initial_capture) &&
              first.port->latest_frame(),
          "the session has a valid latest frame before identity drift");
    backend->invalidate_after_capture = true;
    const auto captured = first.port->capture(control);
    check(error_is(captured, procedure::CoDetectOperationError::SessionChanged),
          "in-place connection/config drift fails capture postflight");
    check(!first.port->current_identity(),
          "in-place drift withdraws identity before cached-frame-only vision");
    backend->valid.store(true);
    backend->invalidate_after_capture = false;
    const auto poisoned_accesses = backend->total_accesses();
    check(!first.port->current_identity() && !first.port->latest_frame() &&
              first.port->screenshot_interval_ms() == 0,
          "identity drift permanently tombstones the token after backend ABA recovery");
    check(backend->total_accesses() == poisoned_accesses,
          "a tombstoned token never probes its backend again");

    backend->interval = 0;
    check(first.port->screenshot_interval_ms() == 0,
          "a dynamically invalid screenshot interval fails closed instead of clamping");
    backend->interval = 60'001;
    check(first.port->screenshot_interval_ms() == 0,
          "an over-limit dynamic screenshot interval fails closed");
    backend->interval = 300;

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
    const auto retired_accesses = first_backend->total_accesses();
    const auto stale_click = first.port->click({1, 1}, control);
    check(error_is(stale_click, procedure::CoDetectOperationError::SessionChanged) &&
              first_backend->clicks.load() == clicks_before,
          "no stale effect reaches the old backend after replacement");
    static_cast<void>(first.port->current_identity());
    static_cast<void>(first.port->latest_frame());
    static_cast<void>(first.port->screenshot_interval_ms());
    static_cast<void>(first.port->publish_latest_frame({}));
    static_cast<void>(first.port->capture(control));
    static_cast<void>(first.port->foreground_matches(control));
    static_cast<void>(first.port->wait(0, control));
    check(first_backend->total_accesses() == retired_accesses,
          "every retired port entry point rejects before touching its backend");

    auto wait_future = std::async(std::launch::async, [&] {
        return second.port->wait(5'000, control);
    });
    std::this_thread::sleep_for(20ms);
    const auto third = owner.activate(
        std::make_shared<Backend>(), procedure::CoDetectProfile::jp, 3);
    const auto replaced_wait_accesses = second_backend->total_accesses();
    const auto wait_result = wait_future.get();
    std::this_thread::sleep_for(70ms);
    check(error_is(wait_result, procedure::CoDetectOperationError::SessionChanged) &&
              third.port->current_identity().get() == third.identity.get(),
          "replacement wakes bounded waits and makes them fail closed");
    check(second_backend->total_accesses() == replaced_wait_accesses,
          "replacement return is a no-further-wait-probe backend handoff barrier");

    owner.invalidate();
    check(!third.port->current_identity(), "explicit invalidation withdraws the final token");
}

void test_operation_and_wait_handoff_barriers()
{
    using namespace std::chrono_literals;
    procedure::BAASConnectionCoDetectOwner owner;
    Control control;

    auto capture_backend = std::make_shared<Backend>();
    capture_backend->block_capture = true;
    const auto capture_binding = owner.activate(
        capture_backend, procedure::CoDetectProfile::jp, 20);
    auto capture_future = std::async(std::launch::async, [&] {
        return capture_binding.port->capture(control);
    });
    {
        std::unique_lock lock(capture_backend->capture_mutex);
        capture_backend->capture_condition.wait(
            lock, [&] { return capture_backend->capture_started; });
    }
    auto invalidate_future =
        std::async(std::launch::async, [&] { owner.invalidate(); });
    check(invalidate_future.wait_for(30ms) == std::future_status::timeout,
          "invalidate waits for an already-linearized capture");
    {
        std::scoped_lock lock(capture_backend->capture_mutex);
        capture_backend->block_capture = false;
    }
    capture_backend->capture_condition.notify_all();
    check(std::holds_alternative<procedure::CoDetectProductionBgrFrame>(
              capture_future.get()),
          "the in-flight capture completes before invalidation retires it");
    invalidate_future.get();
    const auto capture_retired_accesses = capture_backend->total_accesses();
    static_cast<void>(capture_binding.port->capture(control));
    check(capture_backend->total_accesses() == capture_retired_accesses,
          "invalidate return is a no-further-capture backend handoff barrier");

    auto foreground_backend = std::make_shared<Backend>();
    foreground_backend->block_foreground = true;
    const auto foreground_binding = owner.activate(
        foreground_backend, procedure::CoDetectProfile::jp, 21);
    auto foreground_future = std::async(std::launch::async, [&] {
        return foreground_binding.port->foreground_matches(control);
    });
    {
        std::unique_lock lock(foreground_backend->foreground_mutex);
        foreground_backend->foreground_condition.wait(
            lock, [&] { return foreground_backend->foreground_started; });
    }
    auto foreground_invalidate_future =
        std::async(std::launch::async, [&] { owner.invalidate(); });
    check(foreground_invalidate_future.wait_for(30ms) == std::future_status::timeout,
          "invalidate waits for an already-linearized foreground query");
    {
        std::scoped_lock lock(foreground_backend->foreground_mutex);
        foreground_backend->block_foreground = false;
    }
    foreground_backend->foreground_condition.notify_all();
    const auto foreground_result = foreground_future.get();
    check(std::get_if<bool>(&foreground_result) != nullptr,
          "foreground query completes before invalidation handoff");
    foreground_invalidate_future.get();
    const auto foreground_retired_accesses = foreground_backend->total_accesses();
    static_cast<void>(foreground_binding.port->foreground_matches(control));
    check(foreground_backend->total_accesses() == foreground_retired_accesses,
          "invalidate return is a no-further-foreground backend handoff barrier");

    auto replacement_backend = std::make_shared<Backend>();
    const auto replacement = owner.activate(
        replacement_backend, procedure::CoDetectProfile::jp, 22);
    auto wait_future = std::async(std::launch::async, [&] {
        return replacement.port->wait(5'000, control);
    });
    std::this_thread::sleep_for(70ms);
    owner.invalidate();
    const auto wait_barrier_accesses = replacement_backend->total_accesses();
    const auto wait_result = wait_future.get();
    std::this_thread::sleep_for(70ms);
    check(error_is(wait_result, procedure::CoDetectOperationError::SessionChanged),
          "wait observes invalidation through operation-mutex probes");
    check(replacement_backend->total_accesses() == wait_barrier_accesses,
          "invalidate return is a no-further-wait-probe backend handoff barrier");
}

void test_activation_allocation_failure_is_strong()
{
    procedure::BAASConnectionCoDetectOwner owner;
    auto original_backend = std::make_shared<Backend>();
    const auto original = owner.activate(
        original_backend, procedure::CoDetectProfile::jp, 30);
    auto replacement_backend = std::make_shared<Backend>();
    procedure::detail::fail_activation_allocation_after(2);
    bool allocation_failed{};
    try {
        static_cast<void>(owner.activate(
            replacement_backend, procedure::CoDetectProfile::jp, 31));
    } catch (const std::bad_alloc&) {
        allocation_failed = true;
    }
    procedure::detail::clear_activation_allocation_failure();
    check(allocation_failed &&
              original.port->current_identity().get() == original.identity.get(),
          "activation allocation failure leaves the old binding published");
    const auto replacement = owner.activate(
        replacement_backend, procedure::CoDetectProfile::jp, 31);
    check(replacement.port->current_identity().get() == replacement.identity.get(),
          "activation allocation failure does not consume the requested epoch");
}

}  // namespace

int main()
{
    try {
        test_owned_capture_cache_and_boundaries();
        test_postflight_identity_and_activation_guards();
        test_replacement_linearizes_effects_and_wakes_waits();
        test_operation_and_wait_handoff_barriers();
        test_activation_allocation_failure_is_strong();
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
