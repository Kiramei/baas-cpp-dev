#include "service/channels/SyncHandler.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace channels = baas::service::channels;
namespace auth = baas::service::auth;
namespace ws = baas::service::websocket;
using Json = nlohmann::json;
using namespace std::chrono_literals;
namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

auth::SecretBuffer secret(const std::string_view text)
{
    return auth::SecretBuffer{std::as_bytes(std::span{text.data(), text.size()})};
}

class RecordingSink final : public ws::BusinessPlaintextSink {
public:
    ws::BusinessEmitResult emit(ws::BusinessOutboundMessage message) noexcept override
    {
        std::unique_lock lock(mutex_);
        ++calls_;
        if (blocking_) {
            entered_ = true;
            condition_.notify_all();
            condition_.wait(lock, [this] { return released_; });
        }
        if (reject_) return ws::BusinessEmitResult::queue_full;
        messages_.push_back(std::move(message.payload));
        return ws::BusinessEmitResult::accepted;
    }

    ws::BusinessEmitResult emit_batch(std::vector<ws::BusinessOutboundMessage> messages) noexcept override
    {
        for (auto& message : messages) {
            if (emit(std::move(message)) != ws::BusinessEmitResult::accepted)
                return ws::BusinessEmitResult::queue_full;
        }
        return ws::BusinessEmitResult::accepted;
    }

    void block()
    {
        std::lock_guard lock(mutex_);
        blocking_ = true;
    }

    void reject()
    {
        std::lock_guard lock(mutex_);
        reject_ = true;
    }

    bool wait_entered()
    {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 2s, [this] { return entered_; });
    }

    void release()
    {
        std::lock_guard lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }

    std::vector<std::string> messages() const
    {
        std::lock_guard lock(mutex_);
        return messages_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::string> messages_;
    std::size_t calls_{};
    bool blocking_{};
    bool entered_{};
    bool released_{};
    bool reject_{};
};

class EmptySubscription final : public channels::ResourceSubscription {};

class AdversarialStore final : public channels::ResourceStore {
public:
    channels::ResourceStoreResult<channels::ResourceSnapshot> config_list(std::stop_token) override
    {
        return {channels::ResourceSnapshot{"1", "[]"}, channels::ResourceStoreError::none};
    }

    channels::ResourceStoreResult<channels::ResourceSnapshot> pull(
        const channels::ResourceKey&, std::stop_token) override
    {
        return {channels::ResourceSnapshot{"1", "{}"}, channels::ResourceStoreError::none};
    }

    channels::ResourceStoreResult<channels::ResourcePatchResult> apply_patch(
        channels::ResourcePatchRequest, std::stop_token) override
    {
        return {channels::ResourcePatchResult{
                    channels::ResourcePatchDisposition::conflict,
                    {"1", "{}"}, conflict_error},
                channels::ResourceStoreError::none};
    }

    channels::ResourceSubscribeResult subscribe_updates(UpdateCallback supplied) override
    {
        callback = std::move(supplied);
        return {std::make_unique<EmptySubscription>(), channels::ResourceStoreError::none};
    }

    void push(channels::ResourceUpdate update) { callback(std::move(update)); }

    std::string conflict_error;
    UpdateCallback callback;
};

std::shared_ptr<channels::InMemoryResourceStore> make_store(
    channels::InMemoryResourceStore::Clock clock = [] { return 200.25; },
    channels::ResourceStoreLimits limits = {})
{
    std::vector<channels::InitialResource> initial{
        {{channels::SyncResource::config, std::string{"main"}},
         {"100.5", R"({"unknown":{"future":1.25},"nested":{"x":1},"arr":[1,2]})"}},
        {{channels::SyncResource::event, std::nullopt}, {"10", R"({"enabled":true})"}},
        {{channels::SyncResource::gui, std::nullopt}, {"11", R"({"theme":"dark"})"}},
        {{channels::SyncResource::static_data, std::nullopt}, {"12", R"({"version":3})"}},
        {{channels::SyncResource::setup_toml, std::nullopt}, {"13", R"("a = 1")"}},
    };
    return std::make_shared<channels::InMemoryResourceStore>(
        std::move(initial), channels::ResourceSnapshot{"99", R"(["main"])"},
        std::move(clock), limits);
}

std::unique_ptr<ws::BusinessChannelHandler> make_handler(
    const std::shared_ptr<channels::ResourceStore>& store,
    const std::shared_ptr<RecordingSink>& sink,
    channels::SyncHandlerLimits limits = {})
{
    channels::SyncHandlerFactory factory(store, limits);
    ws::BusinessSessionContext context;
    context.channel = baas::service::auth::BusinessChannel::sync;
    auto created = factory.create(std::move(context), sink, {});
    check(static_cast<bool>(created), "handler is created");
    if (!created) return {};
    check(created.handler->ready({}).status == ws::BusinessHandlerStatus::ok,
          "handler subscribes on ready");
    return std::move(created.handler);
}

Json response(ws::BusinessChannelHandler& handler, const std::string_view request,
              ws::BusinessHandlerStatus expected = ws::BusinessHandlerStatus::ok)
{
    auto result = handler.input(secret(request), false, {});
    check(result.status == expected, "request returns expected handler status");
    if (result.messages.empty()) return Json{};
    check(result.messages.size() == 1, "request emits one direct response");
    return Json::parse(result.messages.front().payload);
}

void test_list_pull_patch_and_push()
{
    auto store = make_store();
    auto sink = std::make_shared<RecordingSink>();
    auto handler = make_handler(store, sink);
    if (!handler) return;

    auto list = response(*handler, R"({"type":"list"})");
    check(list == Json({{"type", "config_list"}, {"timestamp", 99}, {"data", Json::array({"main"})}}),
          "list maps to config_list");

    auto pulled = response(*handler, R"({"type":"pull","resource":"config","resource_id":"main"})");
    check(pulled["type"] == "snapshot" && pulled["unknown"].is_null(), "pull maps to snapshot envelope");
    check(pulled["data"]["unknown"]["future"] == 1.25, "pull preserves float and unknown fields");

    auto ack = response(*handler,
        R"({"type":"patch","resource":"config","resource_id":"main","timestamp":100.5,"ops":[{"op":"add","path":"/added","value":{"v":2.75}},{"op":"replace","path":"/nested/x","value":7},{"op":"remove","path":"/arr/0"}]})");
    check(ack["type"] == "patch_ack" && ack["timestamp"] == 100.5,
          "patch ack echoes request timestamp");

    pulled = response(*handler, R"({"type":"pull","resource":"config","resource_id":"main"})");
    check(pulled["data"]["added"]["v"] == 2.75 && pulled["data"]["nested"]["x"] == 7
              && pulled["data"]["arr"] == Json::array({2}),
          "add replace remove commit atomically");
    check(pulled["data"]["unknown"]["future"] == 1.25, "patch retains unrelated data");

    const auto pushes = sink->messages();
    check(pushes.size() == 1, "frontend patch is published");
    if (!pushes.empty()) {
        const auto push = Json::parse(pushes.front());
        check(push["type"] == "patch" && push["direction"] == "push"
                  && push["origin"] == "frontend" && push["ops"].size() == 3,
              "published patch has Python-compatible shape");
    }
}

void test_conflicts_and_protocol_failures()
{
    auto store = make_store();
    auto handler = make_handler(store, std::make_shared<RecordingSink>());
    if (!handler) return;
    (void)response(*handler,
        R"({"type":"patch","resource":"config","resource_id":"main","timestamp":100.5,"ops":[{"op":"replace","path":"/nested/x","value":2}]})");
    auto conflict = response(*handler,
        R"({"type":"patch","resource":"config","resource_id":"main","timestamp":100,"ops":[{"op":"replace","path":"/nested/x","value":3}]})");
    check(conflict["type"] == "patch_conflict" && conflict["request_timestamp"] == 100
              && conflict["timestamp"] == 200.25 && conflict["data"]["nested"]["x"] == 2
              && conflict["error"].is_string(),
          "stale patch returns current snapshot and error");

    auto apply_failure = response(*handler,
        R"({"type":"patch","resource":"event","timestamp":10,"ops":[{"op":"remove","path":"/missing"}]})");
    check(apply_failure["type"] == "patch_conflict" && apply_failure["data"]["enabled"] == true,
          "patch application failure does not commit");

    response(*handler, R"({"type":"patch","resource":"static","timestamp":12,"ops":[]})",
             ws::BusinessHandlerStatus::protocol_failed);
    response(*handler, R"({"type":"patch","resource":"gui","timestamp":11,"ops":[{"op":"copy","path":"/x"}]})",
             ws::BusinessHandlerStatus::protocol_failed);
    response(*handler, R"({"type":"patch","resource":"gui","timestamp":11,"ops":[{"op":"add","path":"x","value":1}]})",
             ws::BusinessHandlerStatus::protocol_failed);
    response(*handler, R"({"type":"patch","resource":"gui","timestamp":11,"ops":[{"op":"add","path":"/x~2y","value":1}]})",
             ws::BusinessHandlerStatus::protocol_failed);
    response(*handler, R"({"type":"pull","resource":"secret"})",
             ws::BusinessHandlerStatus::protocol_failed);
    response(*handler, R"({"type":"wat"})", ws::BusinessHandlerStatus::protocol_failed);
    response(*handler, "{", ws::BusinessHandlerStatus::protocol_failed);
    response(*handler, R"({"type":"list","type":"pull"})",
             ws::BusinessHandlerStatus::protocol_failed);
}

void test_root_pointer_and_backend_push()
{
    auto store = make_store();
    auto sink = std::make_shared<RecordingSink>();
    auto handler = make_handler(store, sink);
    if (!handler) return;
    (void)response(*handler,
        R"({"type":"patch","resource":"gui","timestamp":11,"ops":[{"op":"replace","path":"/","value":{"whole":1.5}}]})");
    auto pulled = response(*handler, R"({"type":"pull","resource":"gui"})");
    check(pulled["data"] == Json({{"whole", 1.5}}), "slash pointer replaces root for Python parity");

    check(store->replace_and_publish({channels::SyncResource::event, std::nullopt},
                                     {"500.75", R"({"enabled":false,"new":4.5})"},
                                     R"([{"op":"replace","path":"/enabled","value":false}])",
                                     "backend"),
          "backend replacement commits");
    const auto messages = sink->messages();
    check(messages.size() == 2, "backend replacement publishes to subscriber");
    if (messages.size() == 2) {
        const auto push = Json::parse(messages.back());
        check(push["direction"] == "push" && push["origin"] == "backend"
                  && push["timestamp"] == 500.75,
              "backend update uses push envelope");
    }
}

void test_atomic_timestamp_conflict()
{
    auto store = make_store([] { return 300.0; });
    channels::ResourcePatchRequest request{
        {channels::SyncResource::config, std::string{"main"}}, "100.5",
        {{"replace", "/nested/x", std::string{"8"}}}};
    std::atomic<int> applied{};
    std::atomic<int> conflicts{};
    auto run = [&] {
        auto result = store->apply_patch(request, {});
        check(static_cast<bool>(result), "concurrent patch returns store result");
        if (!result) return;
        if (result->disposition == channels::ResourcePatchDisposition::applied) ++applied;
        else ++conflicts;
    };
    std::thread first(run);
    std::thread second(run);
    first.join();
    second.join();
    check(applied == 1 && conflicts == 1, "timestamp compare and commit are atomic");
}

void test_bounds_raii_and_close_race()
{
    auto store = make_store();
    channels::SyncHandlerLimits limits;
    limits.max_input_json_bytes = 64;
    limits.max_in_flight_pushes = 1;
    auto sink = std::make_shared<RecordingSink>();
    sink->block();
    auto handler = make_handler(store, sink, limits);
    if (!handler) return;
    response(*handler, std::string(65, 'x'), ws::BusinessHandlerStatus::protocol_failed);

    std::thread publisher([&] {
        (void)store->replace_and_publish({channels::SyncResource::event, std::nullopt},
            {"20", R"({"enabled":false})"}, R"([])", "backend");
    });
    check(sink->wait_entered(), "push entered blocking sink");
    check(store->replace_and_publish({channels::SyncResource::gui, std::nullopt},
                                     {"21", R"({"theme":"light"})"}, R"([])", "backend"),
          "second backend commit succeeds even if delivery capacity fails closed");
    check(handler->heartbeat({}).status == ws::BusinessHandlerStatus::internal_error,
          "push overflow fails handler closed");

    std::atomic<bool> closed{};
    std::thread closer([&] {
        handler->closed(ws::BusinessCloseReason::stopped);
        closed = true;
    });
    std::this_thread::sleep_for(20ms);
    check(!closed.load(), "close waits for admitted callback");
    sink->release();
    publisher.join();
    closer.join();
    check(closed.load(), "close completes after admitted callback");
    const auto before = sink->messages().size();
    (void)store->replace_and_publish({channels::SyncResource::event, std::nullopt},
                                    {"22", R"({"enabled":true})"}, R"([])", "backend");
    check(sink->messages().size() == before, "RAII unsubscribe prevents post-close push");
}

void test_store_capacity_and_validation()
{
    channels::ResourceStoreLimits limits;
    limits.max_resources = 0;
    bool rejected{};
    try { (void)make_store([] { return 1.0; }, limits); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "store rejects initial resources over capacity");

    auto store = make_store();
    check(!store->replace_and_publish({channels::SyncResource::event, std::nullopt},
                                      {"bad", "{}"}, "[]", "backend"),
          "backend update validates timestamp");
    auto subscription = store->subscribe_updates({});
    check(!subscription && subscription.error == channels::ResourceStoreError::invalid_data,
          "empty callback is rejected");
    check(!store->replace_and_publish({channels::SyncResource::event, std::nullopt},
                                      {"23", "{}"}, "[]", std::string(65, 'o')),
          "backend origin is bounded before publication");
}

void test_channel_gate_and_adversarial_store_fields()
{
    auto regular_store = make_store();
    auto sink = std::make_shared<RecordingSink>();
    channels::SyncHandlerFactory factory(regular_store);
    ws::BusinessSessionContext wrong_context;
    wrong_context.channel = baas::service::auth::BusinessChannel::provider;
    auto wrong = factory.create(std::move(wrong_context), sink, {});
    check(!wrong && wrong.error == ws::BusinessHandlerCreateError::internal_error,
          "sync factory rejects provider channel context");

    auto custom = std::make_shared<AdversarialStore>();
    auto custom_sink = std::make_shared<RecordingSink>();
    auto handler = make_handler(custom, custom_sink);
    if (!handler) return;
    custom->push({{channels::SyncResource::event, std::nullopt}, "2", "[]",
                  std::string(65, 'o')});
    check(handler->heartbeat({}).status == ws::BusinessHandlerStatus::internal_error
              && custom_sink->messages().empty(),
          "oversized custom-store push origin fails before envelope construction");

    auto id_store = std::make_shared<AdversarialStore>();
    auto id_sink = std::make_shared<RecordingSink>();
    auto id_handler = make_handler(id_store, id_sink);
    if (id_handler) {
        id_store->push({{channels::SyncResource::config, std::string(257, 'i')},
                        "2", "[]", "backend"});
        check(id_handler->heartbeat({}).status == ws::BusinessHandlerStatus::internal_error
                  && id_sink->messages().empty(),
              "oversized custom-store push resource id fails before envelope construction");
    }

    auto error_store = std::make_shared<AdversarialStore>();
    error_store->conflict_error = std::string(4U * 1'024U + 1U, 'e');
    auto error_handler = make_handler(error_store, std::make_shared<RecordingSink>());
    if (error_handler) {
        auto result = error_handler->input(secret(
            R"({"type":"patch","resource":"event","timestamp":1,"ops":[]})"), false, {});
        check(result.status == ws::BusinessHandlerStatus::internal_error && result.messages.empty(),
              "oversized custom-store conflict error fails before response construction");
    }
}

} // namespace

int main()
{
    test_list_pull_patch_and_push();
    test_conflicts_and_protocol_failures();
    test_root_pointer_and_backend_push();
    test_atomic_timestamp_conflict();
    test_bounds_raii_and_close_race();
    test_store_capacity_and_validation();
    test_channel_gate_and_adversarial_store_fields();
    if (failures != 0) {
        std::cerr << failures << " sync handler test(s) failed\n";
        return 1;
    }
    std::cout << "All sync handler tests passed\n";
    return 0;
}
