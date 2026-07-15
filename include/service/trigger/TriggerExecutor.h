#pragma once

#include "service/trigger/TriggerDispatch.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace baas::service::trigger {

struct TriggerExecutorLimits {
    std::size_t worker_threads{2};
    std::size_t max_tasks{256};
    std::size_t max_queued_tasks{256};
    std::size_t max_tasks_per_connection{64};
    std::size_t max_connections{256};
    std::size_t max_input_bytes{256U * 1'024U * 1'024U};
    std::size_t max_input_bytes_per_connection{128U * 1'024U * 1'024U};
    std::size_t max_pending_response_bytes{256U * 1'024U * 1'024U};
    std::size_t max_pending_response_bytes_per_connection{
        128U * 1'024U * 1'024U};
};

enum class TriggerSubmitError : std::uint8_t {
    none,
    unregistered_command,
    executor_stopped,
    connection_stopped,
    global_task_limit,
    connection_task_limit,
    queue_full,
    global_input_limit,
    connection_input_limit,
    admission_rejected,
    transaction_failed,
};

[[nodiscard]] std::string_view trigger_submit_error_name(
    TriggerSubmitError error) noexcept;

struct TriggerSubmitResult {
    TriggerSubmitError error{TriggerSubmitError::none};
    trigger_protocol::AdmissionError admission_error{
        trigger_protocol::AdmissionError::none};
    trigger_protocol::Timestamp timestamp{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TriggerSubmitError::none;
    }
};

struct TriggerExecutorStats {
    std::size_t reserved{};
    std::size_t queued{};
    std::size_t running{};
    // Completed tasks are retained only while their terminal response is
    // waiting for egress capacity.
    std::size_t completed{};
    std::size_t active_tasks{};
    std::size_t accepted{};
    std::size_t rejected{};
    std::size_t handlers_finished{};
    std::size_t pending_retries{};
    std::size_t retained_input_bytes{};
    std::size_t pending_response_bytes{};
    bool stopping{};
};

struct TriggerConnectionStats {
    std::size_t active_tasks{};
    std::size_t queued{};
    std::size_t running{};
    std::size_t completed{};
    std::size_t retained_input_bytes{};
    std::size_t pending_response_bytes{};
    bool accepting{};
    bool close_required{};
};

struct TriggerCancelResult {
    trigger_protocol::CancelDecision session_decision{
        trigger_protocol::CancelDecision::unknown_timestamp};
    bool task_found{};
    bool stop_requested{};
};

struct TriggerRetryResult {
    std::size_t attempted{};
    std::size_t published{};
    std::size_t still_pending{};
    bool close_required{};
};

struct TriggerCompleteSendResult {
    trigger_protocol::CompleteSendResult send{};
    TriggerRetryResult retry{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return static_cast<bool>(send) && !retry.close_required;
    }
};

struct TriggerFailSendResult {
    trigger_protocol::SendTransitionError error{
        trigger_protocol::SendTransitionError::none};
    std::size_t cancellations_consumed{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == trigger_protocol::SendTransitionError::none;
    }
};

struct TriggerCloseResult {
    std::size_t cancellations_consumed{};
};

class TriggerConnectionOwner;
class TriggerExecutor;
struct TriggerExecutorTestAccess;

#if defined(BAAS_SERVICE_TRIGGER_EXECUTOR_TEST_HOOKS)
struct TriggerExecutorTestAccess final {
    using AfterDispatchHook = void (*)(void*) noexcept;
    static void set_after_dispatch_hook(
        TriggerExecutor& executor, AfterDispatchHook hook,
        void* context) noexcept;
};
#endif

// Fixed-capacity worker pool. It owns no BAAS globals, transport, device, or
// application object. External shutdown() requests stop, closes every owned
// connection, and waits for registry close plus worker drain before returning.
// Calls made from any executor worker context are deliberately non-blocking.
class TriggerExecutor final {
public:
    TriggerExecutor(
        std::shared_ptr<const TriggerDispatcher> dispatcher,
        TriggerExecutorLimits limits = {});
    ~TriggerExecutor() noexcept;

    TriggerExecutor(const TriggerExecutor&) = delete;
    TriggerExecutor& operator=(const TriggerExecutor&) = delete;
    TriggerExecutor(TriggerExecutor&&) = delete;
    TriggerExecutor& operator=(TriggerExecutor&&) = delete;

    [[nodiscard]] TriggerConnectionOwner connect(
        std::shared_ptr<trigger_protocol::TriggerSession> session,
        std::size_t max_tasks = 0);
    [[nodiscard]] TriggerExecutorStats stats() const noexcept;
    void shutdown() noexcept;

private:
    struct Impl;
    [[nodiscard]] static const TriggerHandler* resolve_handler(
        const TriggerDispatcher& dispatcher,
        const TriggerCommandDescriptor& descriptor) noexcept;
    [[nodiscard]] static AdmittedTriggerRequest make_request(
        trigger_protocol::TriggerIngressItem item,
        trigger_protocol::TriggerSession& session,
        trigger_protocol::AdmissionReceipt receipt) noexcept;
    [[nodiscard]] static TriggerDispatchResult execute(
        const TriggerDispatcher& dispatcher,
        const TriggerHandler& handler,
        const AdmittedTriggerRequest& request,
        std::stop_token stop_token);
    std::shared_ptr<Impl> impl_;

    friend class TriggerConnectionOwner;
    friend struct TriggerExecutorTestAccess;
};

// Exclusive execution owner for one shared TriggerSession. State and every
// active slot retain that shared lifetime through deferred worker drain.
class TriggerConnectionOwner final {
public:
    ~TriggerConnectionOwner() noexcept;

    TriggerConnectionOwner(const TriggerConnectionOwner&) = delete;
    TriggerConnectionOwner& operator=(const TriggerConnectionOwner&) = delete;
    TriggerConnectionOwner(TriggerConnectionOwner&&) noexcept;
    TriggerConnectionOwner& operator=(TriggerConnectionOwner&&) noexcept;

    [[nodiscard]] TriggerSubmitResult submit(
        trigger_protocol::TriggerIngressItem item);
    [[nodiscard]] TriggerCancelResult request_cancel(
        trigger_protocol::Timestamp timestamp);
    [[nodiscard]] TriggerRetryResult retry_pending();
    [[nodiscard]] trigger_protocol::OutputReadySubscription observe_output_ready(
        std::weak_ptr<trigger_protocol::OutputReadyObserver> observer);
    [[nodiscard]] TriggerCompleteSendResult complete_send(
        const trigger_protocol::SendLease& lease);
    [[nodiscard]] TriggerFailSendResult fail_send(
        const trigger_protocol::SendLease& lease);
    [[nodiscard]] TriggerCloseResult close();
    [[nodiscard]] TriggerConnectionStats stats() const noexcept;
    void shutdown() noexcept;

private:
    struct State;
    explicit TriggerConnectionOwner(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class TriggerExecutor;
};

}  // namespace baas::service::trigger
