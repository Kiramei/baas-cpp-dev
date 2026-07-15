#include "service/channels/TriggerHandler.h"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace auth = baas::service::auth;
namespace channels = baas::service::channels;
namespace trigger = baas::service::trigger;
namespace ws = baas::service::websocket;
using namespace std::chrono_literals;

namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] auth::SecretBuffer secret(const std::string_view value)
{
    return auth::SecretBuffer{
        std::as_bytes(std::span{value.data(), value.size()})};
}

class RecordingSink final : public ws::BusinessPlaintextSink {
public:
    explicit RecordingSink(const bool automatic = true) : automatic_(automatic) {}

    ws::BusinessEmitResult emit(ws::BusinessOutboundMessage message) noexcept override
    {
        std::vector<ws::BusinessOutboundMessage> batch;
        batch.push_back(std::move(message));
        return emit_batch(std::move(batch));
    }

    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages) noexcept override
    {
        return emit_batch(std::move(messages), {});
    }

    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept override
    {
        {
            std::lock_guard lock{mutex_};
            batches_.push_back(std::move(messages));
            if (completion) completions_.push_back(completion);
        }
        changed_.notify_all();
        if (completion && automatic_)
            completion->complete(ws::BusinessBatchWriteResult::written);
        return ws::BusinessEmitResult::accepted;
    }

    [[nodiscard]] bool wait_for_batches(const std::size_t count)
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, 3s, [&] { return batches_.size() >= count; });
    }

    [[nodiscard]] std::vector<ws::BusinessOutboundMessage> first_batch()
    {
        std::lock_guard lock{mutex_};
        return batches_.empty() ? std::vector<ws::BusinessOutboundMessage>{}
                                : batches_.front();
    }

    [[nodiscard]] std::vector<ws::BusinessOutboundMessage> batch(
        const std::size_t index)
    {
        std::lock_guard lock{mutex_};
        return index < batches_.size()
            ? batches_[index] : std::vector<ws::BusinessOutboundMessage>{};
    }

    void complete(
        const std::size_t index,
        const ws::BusinessBatchWriteResult result =
            ws::BusinessBatchWriteResult::written)
    {
        std::shared_ptr<ws::BusinessBatchCompletion> completion;
        {
            std::lock_guard lock{mutex_};
            if (index < completions_.size()) completion = completions_[index];
        }
        if (completion) completion->complete(result);
    }

    [[nodiscard]] std::size_t batch_count()
    {
        std::lock_guard lock{mutex_};
        return batches_.size();
    }

private:
    bool automatic_{};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::vector<ws::BusinessOutboundMessage>> batches_;
    std::vector<std::shared_ptr<ws::BusinessBatchCompletion>> completions_;
};

class RejectingSink final : public ws::BusinessPlaintextSink {
public:
    ws::BusinessEmitResult emit(ws::BusinessOutboundMessage message) noexcept override
    {
        std::vector<ws::BusinessOutboundMessage> batch;
        batch.push_back(std::move(message));
        return emit_batch(std::move(batch));
    }
    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages) noexcept override
    {
        return emit_batch(std::move(messages), {});
    }
    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage>,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept override
    {
        ++calls;
        if (completion)
            completion->complete(ws::BusinessBatchWriteResult::failed);
        return ws::BusinessEmitResult::queue_full;
    }
    std::atomic_size_t calls{};
};

class BlockingSink final : public ws::BusinessPlaintextSink {
public:
    ws::BusinessEmitResult emit(ws::BusinessOutboundMessage message) noexcept override
    {
        std::vector<ws::BusinessOutboundMessage> batch;
        batch.push_back(std::move(message));
        return emit_batch(std::move(batch));
    }
    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage> messages) noexcept override
    {
        return emit_batch(std::move(messages), {});
    }
    ws::BusinessEmitResult emit_batch(
        std::vector<ws::BusinessOutboundMessage>,
        std::shared_ptr<ws::BusinessBatchCompletion> completion) noexcept override
    {
        {
            std::lock_guard lock{mutex};
            ++calls;
            entered = true;
        }
        changed.notify_all();
        {
            std::unique_lock lock{mutex};
            changed.wait(lock, [this] { return released; });
        }
        if (completion)
            completion->complete(ws::BusinessBatchWriteResult::written);
        return ws::BusinessEmitResult::accepted;
    }
    bool wait_entered()
    {
        std::unique_lock lock{mutex};
        return changed.wait_for(lock, 3s, [this] { return entered; });
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
    std::size_t calls{};
};

struct AdmissionGate {
    static void pause(void* context) noexcept
    {
        auto& gate = *static_cast<AdmissionGate*>(context);
        std::unique_lock lock{gate.mutex};
        gate.entered = true;
        gate.changed.notify_all();
        gate.changed.wait(lock, [&] { return gate.released; });
    }

    bool wait_entered()
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

[[nodiscard]] std::shared_ptr<const trigger::TriggerDispatcher> dispatcher()
{
    auto built = trigger::TriggerDispatcher::create({
        {"status", [](const trigger::AdmittedTriggerRequest&,
                      trigger::TriggerResponseSink& output,
                      std::stop_token) {
            static_cast<void>(output.success(std::string{"{\"ready\":true}"}));
        }},
        {"import_config", [](const trigger::AdmittedTriggerRequest& request,
                             trigger::TriggerResponseSink& output,
                             std::stop_token) {
            check(request.has_binary() && request.binary()
                      && request.binary()->size() == 3,
                  "import handler must receive the adjacent binary frame");
            std::vector<std::byte> response{
                std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
            static_cast<void>(output.success(
                std::string{"{}"}, std::move(response)));
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

void test_single_connection_bridge()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{1, 8, 8, 8});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RecordingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created),
          "trigger factory must create the trigger channel handler");
    if (!created) return;
    check(created.handler->ready({}).status == ws::BusinessHandlerStatus::ok,
          "new trigger connection must be ready without an unsolicited frame");

    const auto result = created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"status\","
        "\"timestamp\":7,\"payload\":{}}"), false, {});
    check(result.status == ws::BusinessHandlerStatus::ok && result.messages.empty(),
          "accepted command must execute asynchronously");
    check(sink->wait_for_batches(1), "executor output must reach plaintext sink");
    const auto batch = sink->first_batch();
    check(batch.size() == 1
              && batch.front().payload.find("\"type\":\"command_response\"")
                    != std::string::npos
              && batch.front().payload.find("\"timestamp\":7")
                    != std::string::npos,
          "single response identity must survive the complete bridge");
    created.handler->closed(ws::BusinessCloseReason::stopped);
    executor->shutdown();
}

void test_binary_batch_and_send_confirmation_order()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{2, 8, 8, 8});
    channels::TriggerHandlerLimits limits;
    limits.session.max_queued_batches = 1;
    channels::TriggerHandlerFactory factory{executor, limits};
    auto sink = std::make_shared<RecordingSink>(false);
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "binary fixture must create a handler");
    if (!created) return;

    auto first = created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"import_config\","
        "\"timestamp\":10,\"payload\":{\"binary\":true}}"), false, {});
    check(first.status == ws::BusinessHandlerStatus::ok,
          "binary declaration must wait for its adjacent payload");
    const std::string raw{"\x01\x02\x03", 3};
    auto binary = created.handler->input(secret(raw), false, {});
    check(binary.status == ws::BusinessHandlerStatus::ok,
          "adjacent binary must submit import_config");
    check(sink->wait_for_batches(1), "binary response must reach the sink");
    const auto binary_batch = sink->batch(0);
    check(binary_batch.size() == 2
              && binary_batch[0].payload.find("\"binary\":{\"size\":3}")
                    != std::string::npos
              && binary_batch[1].payload == std::string{"\x10\x20\x30", 3},
          "JSON and binary response must remain one ordered plaintext batch");

    auto second = created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"status\","
        "\"timestamp\":11,\"payload\":{}}"), false, {});
    check(second.status == ws::BusinessHandlerStatus::ok,
          "a second command may execute while the first write is pending");
    std::this_thread::sleep_for(50ms);
    check(sink->batch_count() == 1,
          "one active send lease must retain later output until confirmation");
    sink->complete(0);
    check(sink->wait_for_batches(2),
          "write confirmation must release the next queued response");
    sink->complete(1);
    created.handler->closed(ws::BusinessCloseReason::stopped);
    executor->shutdown();
}

void test_stream_rejection_retains_terminal_shape()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RecordingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "stream rejection fixture must create");
    if (!created) return;
    const auto rejected = created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"test_all_sha_stream\","
        "\"timestamp\":12,\"payload\":{\"binary\":true}}"), false, {});
    check(rejected.status == ws::BusinessHandlerStatus::ok
              && rejected.messages.size() == 1
              && rejected.messages[0].payload.find("\"status\":\"error\"")
                    != std::string::npos
              && rejected.messages[0].payload.find("\"done\":true")
                    != std::string::npos,
          "stream command rejection must remain a correlated terminal stream item");
    created.handler->closed(ws::BusinessCloseReason::stopped);
    executor->shutdown();
}

void test_stream_progress_is_fifo_and_terminal()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RecordingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "stream fixture must create");
    if (!created) return;
    const auto submitted = created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"test_all_sha_stream\","
        "\"timestamp\":13,\"payload\":{}}"), false, {});
    check(submitted.status == ws::BusinessHandlerStatus::ok,
          "stream command must submit asynchronously");
    check(sink->wait_for_batches(3), "all stream items must drain through one lease");
    const auto first = sink->batch(0);
    const auto second = sink->batch(1);
    const auto third = sink->batch(2);
    check(first.size() == 1 && second.size() == 1 && third.size() == 1
              && first[0].payload.find("\"step\":1") != std::string::npos
              && second[0].payload.find("\"step\":2") != std::string::npos
              && third[0].payload.find("\"done\":true") != std::string::npos
              && first[0].payload.find("\"done\":true") == std::string::npos,
          "progress and terminal items must preserve stream FIFO and shape");
    created.handler->closed(ws::BusinessCloseReason::stopped);
    executor->shutdown();
}

void test_peer_final_and_stop_prevent_new_emission()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RecordingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "peer-final fixture must create");
    if (!created) return;
    const auto finished = created.handler->input(secret(""), true, {});
    check(finished.status == ws::BusinessHandlerStatus::complete,
          "authenticated peer final must close the trigger owner");
    const auto late = created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"status\","
        "\"timestamp\":14,\"payload\":{}}"), false, {});
    check(late.status == ws::BusinessHandlerStatus::complete
              && sink->batch_count() == 0,
          "peer final must prevent later admission and plaintext emission");
    created.handler->closed(ws::BusinessCloseReason::clean_final);

    std::stop_source stopping;
    auto stopped = factory.create(context, sink, stopping.get_token());
    check(static_cast<bool>(stopped), "stop-race fixture must initially create");
    stopping.request_stop();
    check(stopped.handler->heartbeat(stopping.get_token()).status
              == ws::BusinessHandlerStatus::complete
              && sink->batch_count() == 0,
          "connection stop must close before any new plaintext emission");
    stopped.handler->closed(ws::BusinessCloseReason::stopped);
    executor->shutdown();
}

void test_synchronous_sink_failure_is_terminal_without_recursion()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RejectingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "failure fixture must create");
    if (!created) return;
    static_cast<void>(created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"status\","
        "\"timestamp\":20,\"payload\":{}}"), false, {}));
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (sink->calls.load() == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    check(sink->calls.load() == 1,
          "synchronous observed rejection must attempt exactly one leased batch");
    auto heartbeat = created.handler->heartbeat({});
    while (heartbeat.status == ws::BusinessHandlerStatus::ok
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
        heartbeat = created.handler->heartbeat({});
    }
    check(heartbeat.status == ws::BusinessHandlerStatus::internal_error,
          "failed observed completion must make the handler terminal");
    created.handler->closed(ws::BusinessCloseReason::internal_error);
    executor->shutdown();
}

void test_close_linearizes_with_output_callback()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<BlockingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "close-race fixture must create");
    if (!created) return;
    static_cast<void>(created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"status\","
        "\"timestamp\":21,\"payload\":{}}"), false, {}));
    check(sink->wait_entered(), "output callback must enter the external sink");
    std::atomic_bool close_returned{};
    std::thread closer([&] {
        created.handler->closed(ws::BusinessCloseReason::stopped);
        close_returned = true;
    });
    std::this_thread::sleep_for(20ms);
    check(!close_returned.load(),
          "close must wait for a previously admitted external sink call");
    sink->release();
    closer.join();
    check(close_returned.load(), "close must finish after the sink call exits");
    std::this_thread::sleep_for(20ms);
    {
        std::lock_guard lock{sink->mutex};
        check(sink->calls == 1,
              "closed handler must reject every later output-ready callback");
    }
    created.handler.reset();
    executor->shutdown();
}

void test_disconnect_cancels_running_command_without_output()
{
    std::atomic_bool started{};
    std::atomic_bool stopped{};
    auto built = trigger::TriggerDispatcher::create({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& output,
                       std::stop_token stop) {
            started = true;
            while (!stop.stop_requested()) std::this_thread::yield();
            stopped = true;
            static_cast<void>(output.cancelled());
        }},
    });
    check(static_cast<bool>(built), "cancel dispatcher must build");
    if (!built) return;
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        std::make_shared<const trigger::TriggerDispatcher>(
            std::move(*built.dispatcher)),
        trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RecordingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "cancel fixture must create");
    if (!created) return;
    static_cast<void>(created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"status\","
        "\"timestamp\":22,\"payload\":{}}"), false, {}));
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!started.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    check(started.load(), "running handler must start before disconnect");
    created.handler->closed(ws::BusinessCloseReason::truncated);
    check(stopped.load(), "disconnect must request stop and drain the task owner");
    check(sink->batch_count() == 0,
          "cancel completion after disconnect must not emit plaintext");
    executor->shutdown();
}

void test_factory_classifies_capacity_and_stopped_executor()
{
    auto limits = trigger::TriggerExecutorLimits{};
    limits.worker_threads = 1;
    limits.max_tasks = 4;
    limits.max_queued_tasks = 4;
    limits.max_tasks_per_connection = 4;
    limits.max_connections = 1;
    auto executor = std::make_shared<trigger::TriggerExecutor>(dispatcher(), limits);
    channels::TriggerHandlerFactory factory{executor};
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto first = factory.create(context, std::make_shared<RecordingSink>(), {});
    auto saturated = factory.create(
        context, std::make_shared<RecordingSink>(), {});
    check(static_cast<bool>(first)
              && saturated.error == ws::BusinessHandlerCreateError::capacity,
          "connection-table exhaustion must be reported as capacity");
    first.handler->closed(ws::BusinessCloseReason::stopped);
    first.handler.reset();
    executor->shutdown();
    auto stopped = factory.create(context, std::make_shared<RecordingSink>(), {});
    check(stopped.error == ws::BusinessHandlerCreateError::internal_error,
          "stopped executor must not be mislabeled as transient capacity");
}

void test_shutdown_linearizes_before_final_submit()
{
    std::atomic_size_t handler_effects{};
    auto built = trigger::TriggerDispatcher::create({
        {"status", [&](const trigger::AdmittedTriggerRequest&,
                       trigger::TriggerResponseSink& output,
                       std::stop_token) {
            ++handler_effects;
            static_cast<void>(output.success());
        }},
    });
    check(static_cast<bool>(built), "admission-race dispatcher must build");
    if (!built) return;
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        std::make_shared<const trigger::TriggerDispatcher>(
            std::move(*built.dispatcher)),
        trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RecordingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "admission-race fixture must create");
    if (!created) return;

    AdmissionGate gate;
    channels::detail::set_trigger_before_submit_hook_for_test(
        &AdmissionGate::pause, &gate);
    ws::BusinessHandlerResult input_result;
    std::thread input([&] {
        input_result = created.handler->input(secret(
            "{\"type\":\"command\",\"command\":\"status\","
            "\"timestamp\":30,\"payload\":{}}"), false, {});
    });
    check(gate.wait_entered(), "input must pause after parsing and before admission");
    created.handler->closed(ws::BusinessCloseReason::truncated);
    gate.release();
    input.join();
    channels::detail::set_trigger_before_submit_hook_for_test(nullptr, nullptr);
    std::this_thread::sleep_for(20ms);
    check(input_result.status == ws::BusinessHandlerStatus::complete
              && handler_effects.load() == 0 && sink->batch_count() == 0,
          "shutdown linearization must prevent every later submit side effect");
    executor->shutdown();
}

void test_completion_allocation_failure_fails_active_lease()
{
    auto executor = std::make_shared<trigger::TriggerExecutor>(
        dispatcher(), trigger::TriggerExecutorLimits{1, 4, 4, 4});
    channels::TriggerHandlerFactory factory{executor};
    auto sink = std::make_shared<RecordingSink>();
    ws::BusinessSessionContext context;
    context.channel = auth::BusinessChannel::trigger;
    auto created = factory.create(context, sink, {});
    check(static_cast<bool>(created), "allocation-failure fixture must create");
    if (!created) return;
    channels::detail::fail_next_trigger_completion_allocation_for_test();
    static_cast<void>(created.handler->input(secret(
        "{\"type\":\"command\",\"command\":\"status\","
        "\"timestamp\":31,\"payload\":{}}"), false, {}));
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    auto heartbeat = created.handler->heartbeat({});
    while (heartbeat.status == ws::BusinessHandlerStatus::ok
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
        heartbeat = created.handler->heartbeat({});
    }
    check(heartbeat.status == ws::BusinessHandlerStatus::internal_error
              && sink->batch_count() == 0,
          "completion allocation failure must fail the lease without terminate or emit");
    created.handler->closed(ws::BusinessCloseReason::internal_error);
    executor->shutdown();
}

}  // namespace

int main()
{
    test_single_connection_bridge();
    test_binary_batch_and_send_confirmation_order();
    test_stream_rejection_retains_terminal_shape();
    test_stream_progress_is_fifo_and_terminal();
    test_peer_final_and_stop_prevent_new_emission();
    test_synchronous_sink_failure_is_terminal_without_recursion();
    test_close_linearizes_with_output_callback();
    test_disconnect_cancels_running_command_without_output();
    test_factory_classifies_capacity_and_stopped_executor();
    test_shutdown_linearizes_before_final_submit();
    test_completion_allocation_failure_fails_active_lease();
    if (failures != 0) {
        std::cerr << failures << " trigger handler test(s) failed\n";
        return 1;
    }
    std::cout << "trigger handler tests passed\n";
    return 0;
}
