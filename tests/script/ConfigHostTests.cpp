#include "script/host/ConfigHost.h"
#include "script/host/HostRuntimeComposition.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace host = baas::script::host;
namespace runtime = baas::script::runtime;
namespace {

int failures{};

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

runtime::JsonValue json_object(runtime::JsonObject value)
{
    return runtime::JsonValue(std::move(value));
}

class AtomicConfigPort final : public host::ConfigHostPort {
public:
    AtomicConfigPort(host::ConfigHostIdentity identity, runtime::JsonObject values,
                     const std::int64_t revision)
        : identity_(std::move(identity)), values_(std::move(values)), revision_(revision) {}

    const host::ConfigHostIdentity& identity() const noexcept override
    {
        return identity_;
    }

    host::ConfigPortTransactionResult transact(
        host::ConfigTransactionRequest request) override
    {
        calls_.fetch_add(1, std::memory_order_relaxed);
        if (throw_on_call_) throw std::runtime_error("secret persistence failure");
        if (forced_commit_) return *forced_commit_;
        if (forced_error_) return *forced_error_;
        std::scoped_lock lock(mutex_);
        if (request.expected_revision != revision_)
            return host::ConfigPortError{
                host::ConfigPortErrorCode::Conflict, false,
                runtime::HostEffectState::NotStarted};
        auto candidate = values_;
        for (const auto& entry : request.patch) {
            runtime::JsonObject* object = &candidate;
            for (std::size_t index{}; index < entry.path.size(); ++index) {
                const auto found = std::find_if(
                    object->begin(), object->end(), [&](const auto& item) {
                        return item.first == entry.path[index];
                    });
                if (index + 1 == entry.path.size()) {
                    if (found == object->end())
                        object->emplace_back(entry.path[index], entry.value);
                    else
                        found->second = entry.value;
                    break;
                }
                if (found == object->end() ||
                    found->second.kind() != runtime::JsonKind::Object)
                    return host::ConfigPortError{
                        host::ConfigPortErrorCode::InvalidPatch, false,
                        runtime::HostEffectState::NotStarted};
                object = &std::get<runtime::JsonObject>(found->second.value());
            }
        }
        values_ = std::move(candidate);
        ++revision_;
        return host::ConfigCommit{
            revision_, "snapshot-" + std::to_string(revision_)};
    }

    [[nodiscard]] std::size_t calls() const noexcept
    {
        return calls_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::int64_t revision() const
    {
        std::scoped_lock lock(mutex_);
        return revision_;
    }

    [[nodiscard]] runtime::JsonObject values() const
    {
        std::scoped_lock lock(mutex_);
        return values_;
    }

    void force(host::ConfigPortError error) { forced_error_ = error; }
    void force(host::ConfigCommit commit) { forced_commit_ = std::move(commit); }
    void throw_on_call() noexcept { throw_on_call_ = true; }

private:
    mutable std::mutex mutex_;
    host::ConfigHostIdentity identity_;
    runtime::JsonObject values_;
    std::int64_t revision_{};
    std::atomic<std::size_t> calls_{};
    std::optional<host::ConfigPortError> forced_error_;
    std::optional<host::ConfigCommit> forced_commit_;
    bool throw_on_call_{};
};

std::shared_ptr<const host::ConfigHostSnapshot> snapshot()
{
    return std::make_shared<const host::ConfigHostSnapshot>(host::ConfigHostSnapshot{
        {"user-1", "snapshot-7"}, 7,
        {{"bounty_coin", runtime::JsonValue(std::int64_t{3})},
         {"unknown", runtime::JsonValue("keep")},
         {"nested", json_object({
             {"value", runtime::JsonValue(std::int64_t{5})},
             {"a/b", runtime::JsonValue("slash")},
             {"a~b", runtime::JsonValue("tilde")}})}}});
}

struct Fixture final {
    std::shared_ptr<const host::ConfigHostSnapshot> pinned{snapshot()};
    std::shared_ptr<AtomicConfigPort> port{std::make_shared<AtomicConfigPort>(
        pinned->identity, pinned->values, pinned->revision)};
    host::ConfigHostRuntime owner{host::make_config_host_runtime(pinned, port)};
};

runtime::SynchronousHostOptions options(const host::ConfigHostRuntime& owner)
{
    runtime::SynchronousHostOptions result;
    result.metadata = owner.metadata;
    result.bindings = owner.bindings;
    result.permissions.declared_modules.push_back({"baas/config", 1, 0});
    result.permissions.declared_capabilities = {"config.read", "config.write"};
    result.permissions.policy_capabilities = {"config.read", "config.write"};
    result.permissions.platform_capabilities = {"config.read", "config.write"};
    result.permissions.task_capabilities = {"config.read", "config.write"};
    return result;
}

template <class Function>
void expect_evaluation(
    const runtime::LanguageErrorCode code, Function&& function,
    const std::string_view message)
{
    try {
        std::forward<Function>(function)();
        check(false, message);
    } catch (const runtime::EvaluationError& error) {
        check(error.code() == code, message);
    } catch (...) {
        check(false, message);
    }
}

runtime::HostCallContext context(
    const runtime::SynchronousNativeBinding& binding,
    std::shared_ptr<const runtime::HostCancellationProbe> probe = {})
{
    return {"baas/config", "direct", binding.binding_id, {1, 0}, 1,
            std::move(probe)};
}

runtime::HostResult invoke(
    const host::ConfigHostRuntime& owner, const std::string_view id,
    runtime::HostArguments arguments,
    std::shared_ptr<const runtime::HostCancellationProbe> probe = {})
{
    const auto* binding = owner.bindings->find(id);
    if (!binding) return runtime::HostResult::boundary_failure(
        runtime::HostResult::BoundaryFailure::CallbackException);
    return runtime::invoke_host_callback(
        *binding, context(*binding, std::move(probe)), arguments,
        owner.bindings->limits());
}

const runtime::JsonObject& result_object(const runtime::HostResult& result)
{
    return std::get<runtime::JsonObject>(
        std::get<runtime::JsonValue>(result.value().storage()).value());
}

void test_catalog_and_identity()
{
    Fixture fixture;
    const auto resolution = fixture.owner.metadata->resolve({
        {{"baas/config", 1, 0}}, {"config.read", "config.write"},
        {{"baas/config", {"get", "snapshot", "transact"}}},
        {"config.read", "config.write"}, {"config.read", "config.write"},
        {"config.read", "config.write"}});
    check(resolution.modules.size() == 1 &&
              resolution.modules[0].bindings.size() == 3,
          "baas/config must publish exactly three exports");
    const auto* get = fixture.owner.bindings->find("host.config.get.v1");
    const auto* snap = fixture.owner.bindings->find("host.config.snapshot.v1");
    const auto* transact = fixture.owner.bindings->find("host.config.transact.v1");
    check(get && snap && transact &&
              get->contract.execution == runtime::HostExecutionMode::ThreadSafe &&
              transact->contract.cancellation ==
                  runtime::HostCancellationMode::Cooperative &&
              get->contract.parameters.size() == 2 &&
              !get->contract.parameters[1].required,
          "native config binding contracts must be exact");

    const auto result = invoke(fixture.owner, "host.config.snapshot.v1", {});
    check(result.ok() && result_object(result) == runtime::JsonObject{
              {"revision", runtime::JsonValue(std::int64_t{7})},
              {"snapshot_id", runtime::JsonValue("snapshot-7")}},
          "snapshot must expose the pinned revision and identity in stable order");

    auto mismatched = std::make_shared<AtomicConfigPort>(
        host::ConfigHostIdentity{"other", "snapshot-7"},
        fixture.pinned->values, 7);
    try {
        (void)host::make_config_host_runtime(fixture.pinned, mismatched);
        check(false, "mismatched config identity must be rejected");
    } catch (const std::invalid_argument&) {
    }
}

void test_four_layer_capability_narrowing()
{
    Fixture fixture;
    auto request = runtime::HostResolutionRequest{
        {{"baas/config", 1, 0}}, {"config.read", "config.write"},
        {{"baas/config", {"get", "snapshot", "transact"}}},
        {"config.read", "config.write"}, {"config.read", "config.write"},
        {"config.read", "config.write"}};
    const auto expect_layer = [&](const std::string_view layer, auto mutate) {
        auto denied = request;
        mutate(denied);
        try {
            (void)fixture.owner.metadata->resolve(denied);
            check(false, "missing capability layer must deny config access");
        } catch (const runtime::HostRegistryError& error) {
            check((error.code() == runtime::HostRegistryErrorCode::CapabilityDenied ||
                   error.code() == runtime::HostRegistryErrorCode::UndeclaredCapability) &&
                      error.layer() == layer,
                  "config capability denial must identify its narrowing layer");
        }
    };
    expect_layer("manifest", [](auto& value) {
        value.declared_capabilities.erase(value.declared_capabilities.begin());
    });
    expect_layer("policy", [](auto& value) {
        value.policy_capabilities.erase(value.policy_capabilities.begin());
    });
    expect_layer("platform", [](auto& value) {
        value.platform_capabilities.erase(value.platform_capabilities.begin());
    });
    expect_layer("task", [](auto& value) {
        value.task_capabilities.erase(value.task_capabilities.begin());
    });
}

void test_snapshot_reads_and_deep_copy()
{
    Fixture fixture;
    const auto coin = invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("/bounty_coin"), std::nullopt});
    check(coin.ok() && std::get<runtime::JsonValue>(coin.value().storage()) ==
              runtime::JsonValue(std::int64_t{3}),
          "get must return a typed JSON-safe scalar");
    const auto missing = invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("/missing"),
         runtime::HostValue(runtime::JsonValue("fallback"))});
    check(missing.ok() && std::get<runtime::JsonValue>(missing.value().storage()) ==
              runtime::JsonValue("fallback"),
          "get must copy the optional default");
    const auto escaped_slash = invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("/nested/a~1b"), std::nullopt});
    const auto escaped_tilde = invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("/nested/a~0b"), std::nullopt});
    check(escaped_slash.ok() && escaped_tilde.ok() &&
              std::get<runtime::JsonValue>(escaped_slash.value().storage()) ==
                  runtime::JsonValue("slash") &&
              std::get<runtime::JsonValue>(escaped_tilde.value().storage()) ==
                  runtime::JsonValue("tilde"),
          "get must implement strict RFC 6901 escaping");
    const auto invalid = invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("nested/value"), std::nullopt});
    check(invalid.has_error() &&
              invalid.error().code == runtime::HostErrorCode::InvalidArgument,
          "ambient or non-canonical paths must fail closed");

    auto first = std::get<runtime::JsonValue>(invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("/nested"), std::nullopt}).value().storage());
    auto& first_entries = std::get<runtime::JsonObject>(first.value());
    std::get<std::int64_t>(first_entries[0].second.value()) = 99;
    const auto second = invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("/nested/value"), std::nullopt});
    check(second.ok() && std::get<runtime::JsonValue>(second.value().storage()) ==
              runtime::JsonValue(std::int64_t{5}),
          "returned objects must be deep copies of the immutable snapshot");
}

void test_evaluator_optional_named_and_argument_regressions()
{
    Fixture fixture;
    runtime::SynchronousEvaluator evaluator(
        {{"main",
          "import \"baas/config\" as config;\n"
          "let one = config.get(\"/bounty_coin\");\n"
          "let named = config.get(path = \"/missing\", default = 17);\n"
          "let explicit_null = config.get(\"/missing\", null);\n"}},
        options(fixture.owner));
    (void)evaluator.execute("main");
    check(evaluator.module_export("main", "one").as_integer() == 3 &&
              evaluator.module_export("main", "named").as_integer() == 17 &&
              evaluator.module_export("main", "explicit_null") ==
                  runtime::Value::null(),
          "evaluator must support one-argument get, named default, and explicit null");

    Fixture unknown_fixture;
    runtime::SynchronousEvaluator unknown(
        {{"main",
          "import \"baas/config\" as config;\n"
          "config.get(path = \"/bounty_coin\", bogus = 1 / 0);\n"}},
        options(unknown_fixture.owner));
    expect_evaluation(runtime::LanguageErrorCode::CallArgumentUnknown, [&] {
        (void)unknown.execute("main");
    }, "unknown config arguments must fail before expression evaluation");
    check(unknown_fixture.owner.host->stats().value_reads == 0,
          "unknown config arguments must not enter ConfigHost");

    Fixture duplicate_fixture;
    runtime::SynchronousEvaluator duplicate(
        {{"main",
          "import \"baas/config\" as config;\n"
          "config.get(\"/bounty_coin\", path = 1 / 0);\n"}},
        options(duplicate_fixture.owner));
    expect_evaluation(runtime::LanguageErrorCode::CallArgumentDuplicate, [&] {
        (void)duplicate.execute("main");
    }, "duplicate config arguments must fail before expression evaluation");
    check(duplicate_fixture.owner.host->stats().value_reads == 0,
          "duplicate config arguments must not enter ConfigHost");
}

runtime::JsonValue patch(std::int64_t coin)
{
    return json_object({{"/bounty_coin", runtime::JsonValue(coin)}});
}

void test_atomic_transaction_and_pinned_read()
{
    Fixture fixture;
    const auto commit = invoke(
        fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(9))});
    check(commit.ok() && result_object(commit) == runtime::JsonObject{
              {"revision", runtime::JsonValue(std::int64_t{8})},
              {"snapshot_id", runtime::JsonValue("snapshot-8")}},
          "successful transact must return the committed CAS identity");
    check(fixture.port->revision() == 8 && fixture.port->values()[1].second ==
              runtime::JsonValue("keep"),
          "port transaction must preserve unknown configuration fields");
    const auto pinned = invoke(
        fixture.owner, "host.config.get.v1",
        {runtime::HostValue("/bounty_coin"), std::nullopt});
    check(pinned.ok() && std::get<runtime::JsonValue>(pinned.value().storage()) ==
              runtime::JsonValue(std::int64_t{3}),
          "transaction success must not mutate the task-pinned read snapshot");

    const auto conflict = invoke(
        fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(10))});
    check(conflict.has_error() &&
              conflict.error().code == runtime::HostErrorCode::ConfigConflict &&
              conflict.error().effect_state == runtime::HostEffectState::NotStarted,
          "stale expected revision must report a fail-closed config conflict");

    const auto calls = fixture.port->calls();
    const auto write_bytes = fixture.owner.host->stats().write_bytes;
    const auto overlap = invoke(
        fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{8}), runtime::HostValue(json_object({
            {"/nested", json_object({{"x", runtime::JsonValue(true)}})},
            {"/nested/value", runtime::JsonValue(std::int64_t{4})}}))});
    check(overlap.has_error() &&
              overlap.error().code == runtime::HostErrorCode::InvalidArgument &&
              fixture.port->calls() == calls &&
              fixture.owner.host->stats().write_bytes == write_bytes,
          "overlapping patch paths must be rejected before persistence");
}

void test_concurrent_cas_exactly_one_winner()
{
    Fixture fixture;
    std::atomic<bool> start{};
    auto call = [&](const std::int64_t value) {
        while (!start.load(std::memory_order_acquire)) {}
        return invoke(
            fixture.owner, "host.config.transact.v1",
            {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(value))});
    };
    auto first = std::async(std::launch::async, call, 10);
    auto second = std::async(std::launch::async, call, 11);
    start.store(true, std::memory_order_release);
    const auto left = first.get();
    const auto right = second.get();
    check(left.ok() != right.ok() &&
              (left.has_error() ? left.error().code : right.error().code) ==
                  runtime::HostErrorCode::ConfigConflict &&
              fixture.port->revision() == 8,
          "same-revision concurrent transactions must have one atomic winner");
}

struct Probe final : runtime::HostCancellationProbe {
    bool cancel{};
    bool deadline{};
    bool cancelled() const noexcept override { return cancel; }
    bool deadline_exceeded() const noexcept override { return deadline; }
};

struct FlippingProbe final : runtime::HostCancellationProbe {
    mutable std::atomic<std::size_t> cancel_polls{};
    mutable std::atomic<std::size_t> deadline_polls{};
    std::size_t cancel_on{std::numeric_limits<std::size_t>::max()};
    std::size_t deadline_on{std::numeric_limits<std::size_t>::max()};
    bool cancelled() const noexcept override
    {
        return cancel_polls.fetch_add(1, std::memory_order_relaxed) + 1 >=
            cancel_on;
    }
    bool deadline_exceeded() const noexcept override
    {
        return deadline_polls.fetch_add(1, std::memory_order_relaxed) + 1 >=
            deadline_on;
    }
};

runtime::JsonValue wide_object_with_late_invalid_key()
{
    runtime::JsonObject entries;
    entries.reserve(1'025);
    entries.emplace_back(
        std::string("bad\xFF", 4), runtime::JsonValue(std::int64_t{0}));
    for (std::size_t index{}; index < 1'024; ++index) {
        entries.emplace_back(
            "valid_" + std::to_string(index),
            runtime::JsonValue(static_cast<std::int64_t>(index)));
    }
    return runtime::JsonValue(std::move(entries));
}

std::string large_string_with_late_invalid_utf8()
{
    std::string result(1U << 20U, 'x');
    result.back() = static_cast<char>(0xFF);
    return result;
}

void test_limits_cancellation_and_adapter_failures()
{
    Fixture fixture;
    auto cancelled_probe = std::make_shared<Probe>();
    cancelled_probe->cancel = true;
    const auto cancelled = invoke(
        fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))},
        cancelled_probe);
    check(cancelled.has_error() &&
              cancelled.error().code == runtime::HostErrorCode::Cancelled &&
              fixture.port->calls() == 0,
          "pre-cancelled transaction must never reach persistence");

    Fixture simultaneous_fixture;
    auto simultaneous_probe = std::make_shared<Probe>();
    simultaneous_probe->cancel = true;
    simultaneous_probe->deadline = true;
    const auto simultaneous = invoke(
        simultaneous_fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))},
        simultaneous_probe);
    check(simultaneous.has_error() &&
              simultaneous.error().code == runtime::HostErrorCode::DeadlineExceeded &&
              simultaneous_fixture.port->calls() == 0,
          "deadline must deterministically outrank simultaneous cancellation");

    for (const bool deadline_case : {false, true}) {
        Fixture wide_fixture;
        auto probe = std::make_shared<FlippingProbe>();
        if (deadline_case) probe->deadline_on = 4;
        else probe->cancel_on = 4;
        host::ConfigHostLimits limits;
        limits.cooperative_check_interval = 8;
        auto owner = host::make_config_host_runtime(
            wide_fixture.pinned, wide_fixture.port, limits);
        const auto result = invoke(
            owner, "host.config.transact.v1",
            {runtime::HostValue(std::int64_t{7}),
             runtime::HostValue(json_object({
                 {"/wide", wide_object_with_late_invalid_key()}}))},
            probe);
        const auto expected = deadline_case
            ? runtime::HostErrorCode::DeadlineExceeded
            : runtime::HostErrorCode::Cancelled;
        const auto relevant_polls = deadline_case
            ? probe->deadline_polls.load(std::memory_order_relaxed)
            : probe->cancel_polls.load(std::memory_order_relaxed);
        check(result.has_error() && result.error().code == expected &&
                  relevant_polls >= 4 && wide_fixture.port->calls() == 0 &&
                  owner.host->stats().write_bytes == 0,
              deadline_case
                  ? "deadline flipping inside wide-object expansion must win before a late invalid key"
                  : "cancellation flipping inside wide-object expansion must win before a late invalid key");
    }

    for (const bool deadline_case : {false, true}) {
        Fixture value_fixture;
        auto probe = std::make_shared<FlippingProbe>();
        if (deadline_case) probe->deadline_on = 4;
        else probe->cancel_on = 4;
        host::ConfigHostLimits limits;
        limits.cooperative_check_interval = 256;
        auto owner = host::make_config_host_runtime(
            value_fixture.pinned, value_fixture.port, limits);
        const auto result = invoke(
            owner, "host.config.transact.v1",
            {runtime::HostValue(std::int64_t{7}),
             runtime::HostValue(json_object({
                 {"/large", runtime::JsonValue(
                     large_string_with_late_invalid_utf8())}}))},
            probe);
        const auto expected = deadline_case
            ? runtime::HostErrorCode::DeadlineExceeded
            : runtime::HostErrorCode::Cancelled;
        const auto relevant_polls = deadline_case
            ? probe->deadline_polls.load(std::memory_order_relaxed)
            : probe->cancel_polls.load(std::memory_order_relaxed);
        check(result.has_error() && result.error().code == expected &&
                  relevant_polls >= 4 && value_fixture.port->calls() == 0 &&
                  owner.host->stats().write_bytes == 0,
              deadline_case
                  ? "deadline flipping during large-value UTF-8 validation must win before a late invalid byte"
                  : "cancellation flipping during large-value UTF-8 validation must win before a late invalid byte");
    }

    for (const bool deadline_case : {false, true}) {
        Fixture key_fixture;
        auto probe = std::make_shared<FlippingProbe>();
        if (deadline_case) probe->deadline_on = 4;
        else probe->cancel_on = 4;
        host::ConfigHostLimits limits;
        limits.cooperative_check_interval = 256;
        auto owner = host::make_config_host_runtime(
            key_fixture.pinned, key_fixture.port, limits);
        runtime::JsonObject nested;
        nested.emplace_back(
            large_string_with_late_invalid_utf8(),
            runtime::JsonValue(std::int64_t{1}));
        const auto result = invoke(
            owner, "host.config.transact.v1",
            {runtime::HostValue(std::int64_t{7}),
             runtime::HostValue(json_object({
                 {"/large-key", runtime::JsonValue(std::move(nested))}}))},
            probe);
        const auto expected = deadline_case
            ? runtime::HostErrorCode::DeadlineExceeded
            : runtime::HostErrorCode::Cancelled;
        const auto relevant_polls = deadline_case
            ? probe->deadline_polls.load(std::memory_order_relaxed)
            : probe->cancel_polls.load(std::memory_order_relaxed);
        check(result.has_error() && result.error().code == expected &&
                  relevant_polls >= 4 && key_fixture.port->calls() == 0 &&
                  owner.host->stats().write_bytes == 0,
              deadline_case
                  ? "deadline flipping during large-key UTF-8 validation must win before a late invalid byte"
                  : "cancellation flipping during large-key UTF-8 validation must win before a late invalid byte");
    }

    for (const bool deadline_case : {false, true}) {
        Fixture duplicate_fixture;
        auto probe = std::make_shared<FlippingProbe>();
        if (deadline_case) probe->deadline_on = 3;
        else probe->cancel_on = 3;
        host::ConfigHostLimits limits;
        limits.cooperative_check_interval = 9'000;
        auto owner = host::make_config_host_runtime(
            duplicate_fixture.pinned, duplicate_fixture.port, limits);
        const std::string common_prefix(4'096, 'p');
        runtime::JsonObject nested{
            {common_prefix, runtime::JsonValue(std::int64_t{1})},
            {common_prefix, runtime::JsonValue(std::int64_t{2})}};
        const auto result = invoke(
            owner, "host.config.transact.v1",
            {runtime::HostValue(std::int64_t{7}),
             runtime::HostValue(json_object({
                 {"/duplicate", runtime::JsonValue(std::move(nested))}}))},
            probe);
        const auto expected = deadline_case
            ? runtime::HostErrorCode::DeadlineExceeded
            : runtime::HostErrorCode::Cancelled;
        const auto relevant_polls = deadline_case
            ? probe->deadline_polls.load(std::memory_order_relaxed)
            : probe->cancel_polls.load(std::memory_order_relaxed);
        check(result.has_error() && result.error().code == expected &&
                  relevant_polls == 3 && duplicate_fixture.port->calls() == 0 &&
                  owner.host->stats().write_bytes == 0,
              deadline_case
                  ? "deadline must interrupt exact duplicate-key comparison after fingerprinting"
                  : "cancellation must interrupt exact duplicate-key comparison after fingerprinting");
    }

    for (const bool deadline_case : {false, true}) {
        Fixture copy_fixture;
        auto probe = std::make_shared<FlippingProbe>();
        if (deadline_case) probe->deadline_on = 4;
        else probe->cancel_on = 4;
        host::ConfigHostLimits limits;
        limits.cooperative_check_interval = 256;
        auto owner = host::make_config_host_runtime(
            copy_fixture.pinned, copy_fixture.port, limits);
        const auto result = invoke(
            owner, "host.config.transact.v1",
            {runtime::HostValue(std::int64_t{7}),
             runtime::HostValue(json_object({
                 {"/large", runtime::JsonValue(std::string(1U << 20U, 'x'))}}))},
            probe);
        const auto expected = deadline_case
            ? runtime::HostErrorCode::DeadlineExceeded
            : runtime::HostErrorCode::Cancelled;
        const auto relevant_polls = deadline_case
            ? probe->deadline_polls.load(std::memory_order_relaxed)
            : probe->cancel_polls.load(std::memory_order_relaxed);
        check(result.has_error() && result.error().code == expected &&
                  relevant_polls >= 4 && copy_fixture.port->calls() == 0 &&
                  owner.host->stats().write_bytes == 0,
              deadline_case
                  ? "deadline flipping during a large JSON deep copy must roll back"
                  : "cancellation flipping during a large JSON deep copy must roll back");
    }

    Fixture unavailable_fixture;
    unavailable_fixture.port->force({
        host::ConfigPortErrorCode::Unavailable, true,
        runtime::HostEffectState::NotStarted});
    const auto unavailable = invoke(
        unavailable_fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))});
    check(unavailable.has_error() &&
              unavailable.error().code == runtime::HostErrorCode::Unavailable &&
              unavailable.error().retryable,
          "adapter unavailability must retain retry semantics");

    Fixture deadline_fixture;
    deadline_fixture.port->force({
        host::ConfigPortErrorCode::DeadlineExceeded, false,
        runtime::HostEffectState::Unknown});
    const auto deadline = invoke(
        deadline_fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))});
    check(deadline.has_error() &&
              deadline.error().code == runtime::HostErrorCode::DeadlineExceeded &&
              deadline.error().effect_state == runtime::HostEffectState::Unknown,
          "in-flight adapter deadlines must retain unknown effect state");

    Fixture throwing_fixture;
    throwing_fixture.port->throw_on_call();
    const auto failure = invoke(
        throwing_fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))});
    check(failure.has_error() &&
              failure.error().code == runtime::HostErrorCode::Internal &&
              failure.error().message.find("secret") == std::string::npos &&
              failure.error().effect_state == runtime::HostEffectState::Unknown,
          "persistence exceptions must be redacted and fail closed");

    Fixture unsafe_conflict_fixture;
    unsafe_conflict_fixture.port->force({
        host::ConfigPortErrorCode::Conflict, false,
        runtime::HostEffectState::Committed});
    const auto unsafe_conflict = invoke(
        unsafe_conflict_fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))});
    check(unsafe_conflict.has_error() &&
              unsafe_conflict.error().code == runtime::HostErrorCode::Internal &&
              unsafe_conflict.error().effect_state ==
                  runtime::HostEffectState::Committed,
          "effectful adapter conflicts must become redacted Internal failures");

    Fixture unsafe_patch_fixture;
    unsafe_patch_fixture.port->force({
        host::ConfigPortErrorCode::InvalidPatch, false,
        runtime::HostEffectState::Unknown});
    const auto unsafe_patch = invoke(
        unsafe_patch_fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))});
    check(unsafe_patch.has_error() &&
              unsafe_patch.error().code == runtime::HostErrorCode::Internal &&
              unsafe_patch.error().effect_state == runtime::HostEffectState::Unknown,
          "effect-unknown validation results must not claim InvalidArgument safety");

    Fixture oversized_commit_fixture;
    oversized_commit_fixture.port->force(
        host::ConfigCommit{8, std::string(1'020, 's')});
    const auto oversized_commit = invoke(
        oversized_commit_fixture.owner, "host.config.transact.v1",
        {runtime::HostValue(std::int64_t{7}), runtime::HostValue(patch(8))});
    check(oversized_commit.has_error() &&
              oversized_commit.error().code == runtime::HostErrorCode::Internal &&
              oversized_commit.error().effect_state ==
                  runtime::HostEffectState::Committed,
          "commit identity budget must cover config_id plus the new snapshot_id");

    host::ConfigHostLimits limits;
    limits.max_total_read_bytes = 1;
    auto limited = host::make_config_host_runtime(
        throwing_fixture.pinned,
        std::make_shared<AtomicConfigPort>(
            throwing_fixture.pinned->identity, throwing_fixture.pinned->values, 7),
        limits);
    const auto exhausted = invoke(
        limited, "host.config.get.v1",
        {runtime::HostValue("/unknown"), std::nullopt});
    check(exhausted.has_error() &&
              exhausted.error().code == runtime::HostErrorCode::BudgetExceeded,
          "read byte limits must fail before value publication");
    const auto repeated_exhausted = invoke(
        limited, "host.config.get.v1",
        {runtime::HostValue("/unknown"), std::nullopt});
    check(repeated_exhausted.has_error() &&
              limited.host->stats().read_bytes == 0,
          "failed aggregate reservation must not copy or consume retry budget bytes");

    limits = {};
    limits.max_read_operations = 1;
    auto operation_limited = host::make_config_host_runtime(
        throwing_fixture.pinned,
        std::make_shared<AtomicConfigPort>(
            throwing_fixture.pinned->identity, throwing_fixture.pinned->values, 7),
        limits);
    check(invoke(operation_limited, "host.config.snapshot.v1", {}).ok(),
          "the first read operation must fit its budget");
    const auto operation_exhausted = invoke(
        operation_limited, "host.config.get.v1",
        {runtime::HostValue("/unknown"), std::nullopt});
    check(operation_exhausted.has_error() &&
              operation_exhausted.error().code ==
                  runtime::HostErrorCode::BudgetExceeded,
          "snapshot and get must share one aggregate read-operation budget");
}

void test_whole_patch_aggregate_json_limits()
{
    const auto tiny = std::make_shared<const host::ConfigHostSnapshot>(
        host::ConfigHostSnapshot{
            {"u", "s"}, 1,
            {{"x", runtime::JsonValue(std::int64_t{1})}}});
    const auto rejected = [&](host::ConfigHostLimits limits,
                              runtime::JsonValue value,
                              const std::string_view message) {
        auto port = std::make_shared<AtomicConfigPort>(
            tiny->identity, tiny->values, tiny->revision);
        auto owner = host::make_config_host_runtime(tiny, port, limits);
        const auto result = invoke(
            owner, "host.config.transact.v1",
            {runtime::HostValue(std::int64_t{1}),
             runtime::HostValue(std::move(value))});
        check(result.has_error() &&
                  result.error().code == runtime::HostErrorCode::BudgetExceeded &&
                  port->calls() == 0 && owner.host->stats().write_bytes == 0,
              message);
    };

    host::ConfigHostLimits limits;
    limits.json.max_nodes = 8;
    rejected(limits, json_object({
        {"/a", runtime::JsonValue(runtime::JsonArray{
            runtime::JsonValue(std::int64_t{1}), runtime::JsonValue(std::int64_t{2}),
            runtime::JsonValue(std::int64_t{3}), runtime::JsonValue(std::int64_t{4})})},
        {"/b", runtime::JsonValue(runtime::JsonArray{
            runtime::JsonValue(std::int64_t{5}), runtime::JsonValue(std::int64_t{6}),
            runtime::JsonValue(std::int64_t{7}), runtime::JsonValue(std::int64_t{8})})}}),
        "patch node limits must aggregate across every entry");

    limits = {};
    limits.json.max_string_bytes = 16;
    rejected(limits, json_object({
        {"/a", runtime::JsonValue(std::string(8, 'a'))},
        {"/b", runtime::JsonValue(std::string(8, 'b'))}}),
        "patch string limits must aggregate keys and values across entries");

    limits = {};
    limits.json.max_string_bytes = 8;
    std::string oversized_invalid_value(32, 'v');
    oversized_invalid_value.front() = static_cast<char>(0xFF);
    rejected(limits, json_object({
        {"/a", runtime::JsonValue(std::move(oversized_invalid_value))}}),
        "oversized values must fail their aggregate budget before UTF-8 scanning");

    limits = {};
    limits.json.max_string_bytes = 8;
    std::string oversized_invalid_key(32, 'k');
    oversized_invalid_key.front() = static_cast<char>(0xFF);
    runtime::JsonObject oversized_key_object{{
        std::move(oversized_invalid_key), runtime::JsonValue(std::int64_t{1})}};
    rejected(limits, json_object({
        {"/a", runtime::JsonValue(std::move(oversized_key_object))}}),
        "oversized keys must fail their aggregate budget before UTF-8 scanning");

    limits = {};
    limits.json.max_total_bytes = 40;
    rejected(limits, json_object({
        {"/a", runtime::JsonValue(std::string(12, 'a'))},
        {"/b", runtime::JsonValue(std::string(12, 'b'))}}),
        "patch byte limits must aggregate the complete ordered map");

    limits = {};
    limits.json.max_work = 4;
    rejected(limits, json_object({
        {"/a", runtime::JsonValue(std::int64_t{1})},
        {"/b", runtime::JsonValue(std::int64_t{2})}}),
        "patch work limits must aggregate the complete ordered map");
}

void test_invalid_json_and_composition()
{
    auto bad = std::make_shared<host::ConfigHostSnapshot>(*snapshot());
    bad->values.emplace_back("unknown", runtime::JsonValue("duplicate"));
    auto port = std::make_shared<AtomicConfigPort>(
        bad->identity, bad->values, bad->revision);
    try {
        (void)host::make_config_host_runtime(bad, port);
        check(false, "duplicate JSON object keys must be rejected");
    } catch (const std::invalid_argument&) {
    }
    bad = std::make_shared<host::ConfigHostSnapshot>(*snapshot());
    bad->values[0].second = runtime::JsonValue(
        std::numeric_limits<double>::infinity());
    port = std::make_shared<AtomicConfigPort>(bad->identity, bad->values, bad->revision);
    try {
        (void)host::make_config_host_runtime(bad, port);
        check(false, "non-finite config values must be rejected");
    } catch (const std::invalid_argument&) {
    }
    bad = std::make_shared<host::ConfigHostSnapshot>(*snapshot());
    bad->identity.config_id = std::string("bad\xFF", 4);
    port = std::make_shared<AtomicConfigPort>(bad->identity, bad->values, bad->revision);
    try {
        (void)host::make_config_host_runtime(bad, port);
        check(false, "invalid UTF-8 config identity must be rejected");
    } catch (const std::invalid_argument&) {
    }

    Fixture fixture;
    auto composed = host::compose_host_runtime({host::make_host_runtime_contribution(
        fixture.owner.metadata, fixture.owner.bindings, {}, {fixture.owner.host})});
    check(composed.module_version_count() == 1 && composed.binding_count() == 3 &&
              composed.options().metadata && composed.options().bindings,
          "ConfigHost must compose through the production contribution seam");
}

}  // namespace

int main()
{
    test_catalog_and_identity();
    test_four_layer_capability_narrowing();
    test_snapshot_reads_and_deep_copy();
    test_evaluator_optional_named_and_argument_regressions();
    test_atomic_transaction_and_pinned_read();
    test_concurrent_cas_exactly_one_winner();
    test_limits_cancellation_and_adapter_failures();
    test_whole_patch_aggregate_json_limits();
    test_invalid_json_and_composition();
    if (failures != 0) {
        std::cerr << failures << " ConfigHost checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ConfigHost checks passed\n";
    return EXIT_SUCCESS;
}
