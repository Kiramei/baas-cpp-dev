#include "service/pipe/TriggerPipeChannel.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace service_pipe = baas::service::pipe;
namespace bpip = baas::service::protocol::bpip;
namespace trigger = baas::service::trigger;
using namespace std::chrono_literals;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value)
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

[[nodiscard]] bpip::Bytes frame(
    const bpip::FrameKind kind, const std::span<const std::byte> payload)
{
    return bpip::encode_frame(kind, payload).bytes;
}

[[nodiscard]] bpip::Bytes frame(
    const bpip::FrameKind kind, const std::string_view payload)
{
    return frame(kind, bytes(payload));
}

void append(bpip::Bytes& destination, const bpip::Bytes& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

struct StreamState {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<bpip::Bytes> reads;
    std::vector<bpip::Bytes> writes;
    std::size_t read_offset{};
    std::size_t fail_write_call{};
    std::size_t block_write_call{};
    bool write_entered{};
    bool closed{};
};

void push_read(const std::shared_ptr<StreamState>& state, bpip::Bytes input)
{
    {
        std::lock_guard lock{state->mutex};
        state->reads.push_back(std::move(input));
    }
    state->changed.notify_all();
}

struct IdleGate {
    static void pause(void* context) noexcept
    {
        auto& gate = *static_cast<IdleGate*>(context);
        std::unique_lock lock{gate.mutex};
        gate.entered = true;
        gate.changed.notify_all();
        gate.changed.wait(lock, [&] { return gate.released; });
    }

    [[nodiscard]] bool wait_entered()
    {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, 3s, [&] { return entered; });
    }

    void release()
    {
        {
            std::lock_guard lock{mutex};
            released = true;
        }
        changed.notify_all();
    }

    std::mutex mutex;
    std::condition_variable changed;
    bool entered{};
    bool released{};
};

class FakeStream final : public service_pipe::PipeStream {
public:
    explicit FakeStream(std::shared_ptr<StreamState> state)
        : state_(std::move(state))
    {}

    service_pipe::PipeIoResult read(
        const std::span<std::byte> output,
        const std::chrono::milliseconds timeout) override
    {
        std::unique_lock lock{state_->mutex};
        if (!state_->changed.wait_for(lock, timeout, [this] {
                return state_->closed || !state_->reads.empty();
            })) return {0, false, false, true};
        if (state_->closed) return {0, true, false, false};
        auto& source = state_->reads.front();
        const auto count = std::min(output.size(), source.size() - state_->read_offset);
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(state_->read_offset),
            count, output.begin());
        state_->read_offset += count;
        if (state_->read_offset == source.size()) {
            state_->reads.pop_front();
            state_->read_offset = 0;
        }
        return {count, false, false, false};
    }

    service_pipe::PipeIoResult write_all(
        const std::span<const std::byte> input,
        std::chrono::milliseconds) override
    {
        std::unique_lock lock{state_->mutex};
        if (state_->closed) return {0, false, true, false};
        state_->writes.emplace_back(input.begin(), input.end());
        if (state_->block_write_call == state_->writes.size()) {
            state_->write_entered = true;
            state_->changed.notify_all();
            state_->changed.wait(lock, [this] { return state_->closed; });
            return {0, false, true, false};
        }
        const bool fail = state_->fail_write_call != 0
            && state_->writes.size() == state_->fail_write_call;
        state_->changed.notify_all();
        return fail ? service_pipe::PipeIoResult{0, false, true, false}
                    : service_pipe::PipeIoResult{input.size(), false, false, false};
    }

    void close() noexcept override
    {
        std::lock_guard lock{state_->mutex};
        state_->closed = true;
        state_->changed.notify_all();
    }

private:
    std::shared_ptr<StreamState> state_;
};

class FakeListener final : public service_pipe::PipeListener {
public:
    explicit FakeListener(std::unique_ptr<service_pipe::PipeStream> stream)
        : stream_(std::move(stream))
    {}

    std::unique_ptr<service_pipe::PipeStream> accept() override
    {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] { return closed_ || stream_; });
        if (closed_) return {};
        return std::move(stream_);
    }

    void close() noexcept override
    {
        std::lock_guard lock{mutex_};
        closed_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::unique_ptr<service_pipe::PipeStream> stream_;
    bool closed_{};
};

template <class Predicate>
bool wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

[[nodiscard]] std::shared_ptr<const trigger::TriggerDispatcher> dispatcher(
    std::atomic_bool* cancel_started = nullptr,
    std::atomic_bool* cancel_stopped = nullptr)
{
    auto built = trigger::TriggerDispatcher::create({
        {"status", [=](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& output,
                       const std::stop_token stop) {
            if (cancel_started) {
                *cancel_started = true;
                while (!stop.stop_requested()) std::this_thread::yield();
                *cancel_stopped = true;
                static_cast<void>(output.cancelled());
            } else {
                std::vector<std::byte> binary{std::byte{0x44}, std::byte{0x55}};
                static_cast<void>(output.success(
                    std::string{"{\"ready\":true}"}, std::move(binary)));
            }
        }},
        {"import_config", [](const trigger::AdmittedTriggerRequest& request,
                             trigger::TriggerResponseSink& output,
                             std::stop_token) {
            check(request.has_binary() && request.binary()
                    && *request.binary() == std::vector<std::byte>{
                        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
                "import_config must own its immediately following BYTES frame");
            static_cast<void>(output.success(std::string{"{}"}));
        }},
        {"test_all_sha_stream", [](const trigger::AdmittedTriggerRequest&,
                                   trigger::TriggerResponseSink& output,
                                   std::stop_token) {
            static_cast<void>(output.progress(std::string{"{\"step\":1}"}));
            static_cast<void>(output.progress(std::string{"{\"step\":2}"}));
            static_cast<void>(output.success(std::string{"{}"}));
        }},
    });
    if (!built) throw std::runtime_error("dispatcher build failed");
    return std::make_shared<const trigger::TriggerDispatcher>(
        std::move(*built.dispatcher));
}

[[nodiscard]] bpip::Bytes open_wire(const std::string_view channel = "trigger")
{
    return frame(bpip::FrameKind::json,
        std::string{"{\"type\":\"open\",\"channel\":\""}
            + std::string{channel} + "\",\"name\":\"test\"}");
}

void test_factory_is_trigger_only()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher());
    service_pipe::TriggerPipeChannelFactory factory{executor};
    for (const auto channel : {service_pipe::PipeChannel::provider, service_pipe::PipeChannel::sync,
                              service_pipe::PipeChannel::remote}) {
        check(!factory.create({channel, "x"}, {}),
            "unfinished Pipe channels must fail closed");
    }
    check(static_cast<bool>(factory.create({service_pipe::PipeChannel::trigger, "x"}, {})),
        "trigger must be the only production Pipe business channel");
    executor->shutdown();
}

void test_binary_pair_and_atomic_output()
{
    auto state = std::make_shared<StreamState>();
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"import_config","timestamp":10,"payload":{"binary":true}})"));
    const std::vector<std::byte> binary{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    append(wire, frame(bpip::FrameKind::bytes, binary));
    state->reads.push_back(std::move(wire));
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher());
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor)};
    check(host.start(), "binary Pipe host must start");
    check(wait_until([&] {
        std::lock_guard lock{state->mutex};
        return state->writes.size() >= 2;
    }), "import_config response must be emitted");
    host.stop();
    host.join();
    executor->shutdown();
    std::lock_guard lock{state->mutex};
    check(state->writes.size() == 2,
        "open_ok and one business response must be separate writes");
    if (state->writes.size() >= 2) {
        const auto decoded = bpip::Decoder{}.feed(state->writes[1]);
        check(decoded.frames.size() == 1
                && decoded.frames[0].kind == bpip::kind_value(bpip::FrameKind::json)
                && std::string_view{reinterpret_cast<const char*>(
                    decoded.frames[0].payload.data()), decoded.frames[0].payload.size()}
                    .find("\"timestamp\":10") != std::string_view::npos,
            "paired import response must retain correlation");
    }
}

void test_json_binary_response_is_one_write()
{
    auto state = std::make_shared<StreamState>();
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":11,"payload":{}})"));
    state->reads.push_back(std::move(wire));
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher());
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor)};
    check(host.start(), "response Pipe host must start");
    check(wait_until([&] {
        std::lock_guard lock{state->mutex};
        return state->writes.size() >= 2;
    }), "binary response must be emitted");
    host.stop(); host.join(); executor->shutdown();
    std::lock_guard lock{state->mutex};
    if (state->writes.size() >= 2) {
        const auto decoded = bpip::Decoder{}.feed(state->writes[1]);
        check(decoded.frames.size() == 2
                && decoded.frames[0].kind == bpip::kind_value(bpip::FrameKind::json)
                && decoded.frames[1].kind == bpip::kind_value(bpip::FrameKind::bytes)
                && decoded.frames[1].payload
                    == std::vector<std::byte>{std::byte{0x44}, std::byte{0x55}},
            "JSON+BYTES must be one atomic PipeConnectionWriter batch");
    }
}

void test_stream_backpressure_completes_all_leases()
{
    auto state = std::make_shared<StreamState>();
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"test_all_sha_stream","timestamp":12,"payload":{}})"));
    state->reads.push_back(std::move(wire));
    service_pipe::TriggerPipeChannelLimits channel_limits;
    channel_limits.session.max_queued_batches = 1;
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher());
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor, channel_limits)};
    check(host.start(), "stream Pipe host must start");
    check(wait_until([&] {
        std::lock_guard lock{state->mutex};
        return state->writes.size() >= 4;
    }), "progress and retained terminal responses must all drain");
    host.stop(); host.join(); executor->shutdown();
    std::lock_guard lock{state->mutex};
    check(state->writes.size() == 4,
        "stream output must produce open_ok plus three confirmed leases");
}

void test_peer_close_cancels_and_drains_running_task()
{
    std::atomic_bool started{};
    std::atomic_bool stopped{};
    auto state = std::make_shared<StreamState>();
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":13,"payload":{}})"));
    state->reads.push_back(std::move(wire));
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(&started, &stopped));
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor)};
    check(host.start(), "cancel Pipe host must start");
    check(wait_until([&] { return started.load(); }),
        "cancel fixture must start the handler before sending peer CLOSE");
    push_read(state, frame(
        bpip::FrameKind::close, std::span<const std::byte>{}));
    check(wait_until([&] { return host.stats().completed == 1; }),
        "peer CLOSE must wait for owner cancellation and complete");
    check(started.load() && stopped.load(),
        "peer CLOSE must request stop and strongly drain the running handler");
    host.stop(); host.join(); executor->shutdown();
}

void test_write_failure_fails_lease_and_closes_connection()
{
    auto state = std::make_shared<StreamState>();
    state->fail_write_call = 2;
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":14,"payload":{}})"));
    state->reads.push_back(std::move(wire));
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher());
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor)};
    check(host.start(), "failure Pipe host must start");
    check(wait_until([&] { return host.stats().completed == 1; }),
        "writer failure must fail_send and close without a retry loop");
    {
        std::lock_guard lock{state->mutex};
        check(state->closed && state->writes.size() == 2,
            "failed leased batch must poison and close after one attempt");
    }
    host.stop(); host.join(); executor->shutdown();
}

void test_connection_task_limit_rejects_without_overcommit()
{
    std::atomic_bool started{};
    std::atomic_bool stopped{};
    auto state = std::make_shared<StreamState>();
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":20,"payload":{}})"));
    state->reads.push_back(std::move(wire));
    service_pipe::TriggerPipeChannelLimits channel_limits;
    channel_limits.max_tasks_per_connection = 1;
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(&started, &stopped));
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor, channel_limits)};
    check(host.start(), "task-limited Pipe host must start");
    check(wait_until([&] { return started.load(); }),
        "task-limit fixture must start its first admitted handler");
    push_read(state, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":21,"payload":{}})"));
    check(wait_until([&] {
        std::lock_guard lock{state->mutex};
        return state->writes.size() >= 2;
    }), "second command must receive a bounded admission rejection");
    host.stop(); host.join(); executor->shutdown();
    check(started.load() && stopped.load(),
        "the one admitted task must be cancelled and drained on stop");
    std::lock_guard lock{state->mutex};
    if (state->writes.size() >= 2) {
        const auto decoded = bpip::Decoder{}.feed(state->writes[1]);
        const std::string_view json{
            reinterpret_cast<const char*>(decoded.frames[0].payload.data()),
            decoded.frames[0].payload.size()};
        check(json.find("connection_task_limit") != std::string_view::npos,
            "per-connection task capacity must fail with a correlated response");
    }
}

void test_ingress_budget_is_strict()
{
    auto state = std::make_shared<StreamState>();
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":30,"payload":{}})"));
    state->reads.push_back(std::move(wire));
    service_pipe::TriggerPipeChannelLimits channel_limits;
    channel_limits.ingress.max_json_frame_bytes = 32;
    channel_limits.ingress.max_aggregate_bytes = 32;
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher());
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor, channel_limits)};
    check(host.start(), "budget Pipe host must start");
    check(wait_until([&] { return host.stats().completed == 1; }),
        "oversized trigger ingress must close immediately");
    host.stop(); host.join(); executor->shutdown();
    std::lock_guard lock{state->mutex};
    check(state->closed && state->writes.size() == 1,
        "strict ingress budget must emit no business response after open_ok");
}

void test_stop_interrupts_write_and_waits_for_pump_barrier()
{
    auto state = std::make_shared<StreamState>();
    state->block_write_call = 2;
    auto wire = open_wire();
    append(wire, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":40,"payload":{}})"));
    state->reads.push_back(std::move(wire));
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher());
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)),
        std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor)};
    check(host.start(), "blocking-write Pipe host must start");
    check(wait_until([&] {
        std::lock_guard lock{state->mutex};
        return state->write_entered;
    }), "output pump must enter the blocking transport write");
    const auto start = std::chrono::steady_clock::now();
    host.stop();
    host.join();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    executor->shutdown();
    check(elapsed < 2s && host.stats().completed == 1,
        "stop must interrupt write, fail the lease, and join past the pump barrier");
}

void test_pump_idle_transition_has_no_lost_wakeup_under_stress()
{
    constexpr std::size_t burst_count = 128;
    std::atomic_size_t handler_effects{};
    std::atomic_bool second_output_attempted{};
    std::mutex coordinated_mutex;
    std::condition_variable coordinated_changed;
    std::size_t coordinated_started{};
    IdleGate gate;
    auto built = trigger::TriggerDispatcher::create({
        {"status", [&](const trigger::AdmittedTriggerRequest& request,
                       trigger::TriggerResponseSink& output,
                       std::stop_token) {
            ++handler_effects;
            if (request.timestamp() <= 101) {
                std::unique_lock lock{coordinated_mutex};
                ++coordinated_started;
                coordinated_changed.notify_all();
                if (request.timestamp() == 100) {
                    coordinated_changed.wait(
                        lock, [&] { return coordinated_started == 2; });
                } else {
                    lock.unlock();
                    static_cast<void>(gate.wait_entered());
                    second_output_attempted = true;
                }
            }
            static_cast<void>(output.success(std::string{"{}"}));
        }},
    });
    check(static_cast<bool>(built), "idle-race dispatcher must build");
    if (!built) return;

    auto state = std::make_shared<StreamState>();
    auto first = open_wire();
    append(first, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":100,"payload":{}})"));
    append(first, frame(bpip::FrameKind::json,
        R"({"type":"command","command":"status","timestamp":101,"payload":{}})"));
    state->reads.push_back(std::move(first));
    trigger::TriggerExecutorLimits executor_limits;
    executor_limits.worker_threads = 4;
    executor_limits.max_tasks = burst_count + 8;
    executor_limits.max_queued_tasks = burst_count + 8;
    executor_limits.max_tasks_per_connection = burst_count + 8;
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        std::make_shared<const trigger::TriggerDispatcher>(
            std::move(*built.dispatcher)), executor_limits);
    auto factory = std::make_shared<service_pipe::TriggerPipeChannelFactory>(executor);
    service_pipe::PipeHost host{std::make_unique<FakeListener>(
        std::make_unique<FakeStream>(state)), factory};

    service_pipe::detail::set_trigger_pipe_before_idle_hook_for_test(
        &IdleGate::pause, &gate);
    check(host.start(), "idle-race Pipe host must start");
    const bool entered = gate.wait_entered();
    check(entered,
        "pump must pause inside the locked empty-to-idle transition");

    check(wait_until([&] { return second_output_attempted.load(); }),
        "second admitted handler must publish while the prior pump holds idle lock");
    gate.release();
    service_pipe::detail::set_trigger_pipe_before_idle_hook_for_test(nullptr, nullptr);

    bpip::Bytes burst;
    for (std::size_t index = 0; index < burst_count; ++index) {
        const auto command = std::string{
            "{\"type\":\"command\",\"command\":\"status\",\"timestamp\":"}
            + std::to_string(102 + index) + ",\"payload\":{}}";
        append(burst, frame(bpip::FrameKind::json, command));
    }
    push_read(state, std::move(burst));

    check(wait_until([&] {
        std::lock_guard lock{state->mutex};
        return state->writes.size() == burst_count + 3;
    }), "every raced and stressed output must start or join a pump");
    check(handler_effects.load() == burst_count + 2,
        "stress fixture must execute every unique admitted command exactly once");
    host.stop(); host.join(); executor->shutdown();
}

}  // namespace

int main()
{
    test_factory_is_trigger_only();
    test_binary_pair_and_atomic_output();
    test_json_binary_response_is_one_write();
    test_stream_backpressure_completes_all_leases();
    test_peer_close_cancels_and_drains_running_task();
    test_write_failure_fails_lease_and_closes_connection();
    test_connection_task_limit_rejects_without_overcommit();
    test_ingress_budget_is_strict();
    test_stop_interrupts_write_and_waits_for_pump_barrier();
    test_pump_idle_transition_has_no_lost_wakeup_under_stress();
    if (failures != 0) {
        std::cerr << failures << " Trigger Pipe test(s) failed\n";
        return 1;
    }
    std::cout << "Trigger Pipe channel tests passed\n";
    return 0;
}
