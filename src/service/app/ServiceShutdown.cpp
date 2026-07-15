#include "service/app/ServiceShutdown.h"

#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <pthread.h>
#include <signal.h>
#endif

namespace baas::service::app {

std::string_view service_shutdown_reason_name(const ServiceShutdownReason reason) noexcept
{
    using enum ServiceShutdownReason;
    switch (reason) {
        case none: return "none";
        case http_request: return "http_request";
        case interrupt: return "interrupt";
        case terminate: return "terminate";
        case signal_failure: return "signal_failure";
    }
    return "unknown";
}

router::ShutdownDecision ServiceShutdownCoordinator::request_shutdown() noexcept
{
    return request(ServiceShutdownReason::http_request);
}

router::ShutdownDecision ServiceShutdownCoordinator::request(
    const ServiceShutdownReason reason) noexcept
{
    if (reason == ServiceShutdownReason::none) return router::ShutdownDecision::rejected;
    auto expected = ServiceShutdownReason::none;
    if (!reason_.compare_exchange_strong(
            expected, reason, std::memory_order_release, std::memory_order_relaxed)) {
        return router::ShutdownDecision::rejected;
    }
    // Synchronize with the predicate-check-to-sleep transition. The reason is
    // already immutable; taking this mutex only closes the condition-variable
    // lost-wakeup window and never serializes host shutdown work.
    {
        std::lock_guard lock{wait_mutex_};
    }
    wait_cv_.notify_all();
    return router::ShutdownDecision::accepted;
}

ServiceShutdownReason ServiceShutdownCoordinator::wait()
{
    std::unique_lock lock{wait_mutex_};
    wait_cv_.wait(lock, [this] {
        return reason_.load(std::memory_order_acquire) != ServiceShutdownReason::none;
    });
    return reason_.load(std::memory_order_acquire);
}

std::optional<ServiceShutdownReason> ServiceShutdownCoordinator::wait_for(
    const std::chrono::milliseconds timeout)
{
    std::unique_lock lock{wait_mutex_};
    if (!wait_cv_.wait_for(lock, timeout, [this] {
            return reason_.load(std::memory_order_acquire)
                != ServiceShutdownReason::none;
        })) {
        return std::nullopt;
    }
    return reason_.load(std::memory_order_acquire);
}

ServiceShutdownReason ServiceShutdownCoordinator::reason() const noexcept
{
    return reason_.load(std::memory_order_acquire);
}

std::string_view service_signal_error_name(const ServiceSignalError error) noexcept
{
    using enum ServiceSignalError;
    switch (error) {
        case none: return "none";
        case signal_block_failed: return "signal_block_failed";
        case invalid_coordinator: return "invalid_coordinator";
        case invalid_signal_block: return "invalid_signal_block";
        case already_active: return "already_active";
        case event_creation_failed: return "event_creation_failed";
        case handler_registration_failed: return "handler_registration_failed";
        case thread_start_failed: return "thread_start_failed";
    }
    return "unknown";
}

namespace {
std::atomic<bool> process_signal_owner_active{};
}

#if defined(_WIN32)

class ServiceSignalBlock::Impl final {};

namespace {

struct ConsoleEvents {
    HANDLE interrupt_event{};
    HANDLE terminate_event{};
};

std::atomic<ConsoleEvents*> process_console_events{};
std::mutex process_console_events_mutex;

[[nodiscard]] ConsoleEvents* ensure_console_events() noexcept
{
    if (auto* events = process_console_events.load(std::memory_order_acquire)) return events;
    try {
        std::lock_guard lock{process_console_events_mutex};
        if (auto* events = process_console_events.load(std::memory_order_relaxed)) return events;
        static ConsoleEvents events;
        const HANDLE interrupt_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const HANDLE terminate_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (interrupt_event == nullptr || terminate_event == nullptr) {
            if (interrupt_event != nullptr) CloseHandle(interrupt_event);
            if (terminate_event != nullptr) CloseHandle(terminate_event);
            return nullptr;
        }
        // Console handlers may still be finishing while an owner unregisters.
        // These two process-lifetime handles are therefore intentionally left
        // to the operating system at process exit instead of being closed.
        events = {interrupt_event, terminate_event};
        process_console_events.store(&events, std::memory_order_release);
        return &events;
    } catch (...) {
        return nullptr;
    }
}

BOOL WINAPI service_console_handler(const DWORD control_type)
{
    auto* events = process_console_events.load(std::memory_order_acquire);
    if (events == nullptr) return FALSE;
    switch (control_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            return SetEvent(events->interrupt_event);
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            return SetEvent(events->terminate_event);
        default:
            return FALSE;
    }
}

}  // namespace

class ServiceSignalOwner::Impl final {
public:
    Impl(
        std::shared_ptr<ServiceShutdownCoordinator> coordinator,
        std::unique_ptr<ServiceSignalBlock> signal_block,
        ConsoleEvents* events,
        HANDLE stop_event) noexcept
        : coordinator_(std::move(coordinator)), signal_block_(std::move(signal_block)),
          events_(events), stop_event_(stop_event)
    {}

    ~Impl() { stop(); }

    [[nodiscard]] ServiceSignalError start() noexcept
    {
        if (!SetConsoleCtrlHandler(service_console_handler, TRUE)) {
            return ServiceSignalError::handler_registration_failed;
        }
        handler_registered_ = true;
        try {
            worker_ = std::thread([this] { run(); });
        } catch (...) {
            SetConsoleCtrlHandler(service_console_handler, FALSE);
            handler_registered_ = false;
            return ServiceSignalError::thread_start_failed;
        }
        return ServiceSignalError::none;
    }

    void stop() noexcept
    {
        try {
            std::lock_guard lock{stop_mutex_};
            if (stopped_) return;
            stopped_ = true;
            if (stop_event_ != nullptr) SetEvent(stop_event_);
            if (worker_.joinable()) worker_.join();
            if (handler_registered_) {
                SetConsoleCtrlHandler(service_console_handler, FALSE);
                handler_registered_ = false;
            }
            signal_block_.reset();
            if (stop_event_ != nullptr) {
                CloseHandle(stop_event_);
                stop_event_ = nullptr;
            }
            process_signal_owner_active.store(false, std::memory_order_release);
        } catch (...) {
            // A joinable std::thread cannot safely be abandoned. No standard
            // implementation throws here after the joinable/self checks above.
            std::terminate();
        }
    }

#if defined(BAAS_SERVICE_SHUTDOWN_TEST_HOOKS)
    [[nodiscard]] bool notify_for_test(const ServiceShutdownReason reason) noexcept
    {
        try {
            std::lock_guard lock{stop_mutex_};
            if (stopped_) return false;
        } catch (...) {
            return false;
        }
        if (events_ == nullptr) return false;
        if (reason == ServiceShutdownReason::interrupt) {
            return SetEvent(events_->interrupt_event) != 0;
        }
        if (reason == ServiceShutdownReason::terminate) {
            return SetEvent(events_->terminate_event) != 0;
        }
        return false;
    }
#endif

private:
    void run() noexcept
    {
        const HANDLE events[]{stop_event_, events_->interrupt_event, events_->terminate_event};
        for (;;) {
            const DWORD result = WaitForMultipleObjects(3, events, FALSE, INFINITE);
            if (result == WAIT_OBJECT_0) return;
            if (result == WAIT_OBJECT_0 + 1) {
                ResetEvent(events_->interrupt_event);
                static_cast<void>(coordinator_->request(ServiceShutdownReason::interrupt));
                continue;
            }
            if (result == WAIT_OBJECT_0 + 2) {
                ResetEvent(events_->terminate_event);
                static_cast<void>(coordinator_->request(ServiceShutdownReason::terminate));
                continue;
            }
            static_cast<void>(coordinator_->request(ServiceShutdownReason::signal_failure));
            return;
        }
    }

    std::shared_ptr<ServiceShutdownCoordinator> coordinator_;
    std::unique_ptr<ServiceSignalBlock> signal_block_;
    ConsoleEvents* events_{};
    HANDLE stop_event_{};
    std::thread worker_;
    std::mutex stop_mutex_;
    bool handler_registered_{};
    bool stopped_{};
};

#else

class ServiceSignalBlock::Impl final {
public:
    Impl(const sigset_t previous_mask, const pthread_t owner_thread) noexcept
        : previous_mask_(previous_mask), owner_thread_(owner_thread)
    {}

    ~Impl()
    {
        if (pthread_equal(pthread_self(), owner_thread_) != 0) {
            static_cast<void>(pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr));
        }
    }

private:
    sigset_t previous_mask_{};
    pthread_t owner_thread_{};
};

class ServiceSignalOwner::Impl final {
public:
    Impl(
        std::shared_ptr<ServiceShutdownCoordinator> coordinator,
        std::unique_ptr<ServiceSignalBlock> signal_block) noexcept
        : coordinator_(std::move(coordinator)), signal_block_(std::move(signal_block))
    {
        sigemptyset(&signal_set_);
        sigaddset(&signal_set_, SIGINT);
        sigaddset(&signal_set_, SIGTERM);
    }

    ~Impl() { stop(); }

    [[nodiscard]] ServiceSignalError start() noexcept
    {
        try {
            worker_ = std::thread([this] { run(); });
            return ServiceSignalError::none;
        } catch (...) {
            return ServiceSignalError::thread_start_failed;
        }
    }

    void stop() noexcept
    {
        try {
            std::lock_guard lock{stop_mutex_};
            if (stopped_) return;
            stopped_ = true;
            stopping_.store(true, std::memory_order_release);
            if (worker_.joinable()) {
                static_cast<void>(pthread_kill(worker_.native_handle(), SIGTERM));
                worker_.join();
            }
            signal_block_.reset();
            process_signal_owner_active.store(false, std::memory_order_release);
        } catch (...) {
            std::terminate();
        }
    }

#if defined(BAAS_SERVICE_SHUTDOWN_TEST_HOOKS)
    [[nodiscard]] bool notify_for_test(const ServiceShutdownReason reason) noexcept
    {
        try {
            std::lock_guard lock{stop_mutex_};
            if (stopped_ || !worker_.joinable()) return false;
            const int signal = reason == ServiceShutdownReason::interrupt ? SIGINT
                : reason == ServiceShutdownReason::terminate ? SIGTERM : 0;
            return signal != 0 && pthread_kill(worker_.native_handle(), signal) == 0;
        } catch (...) {
            return false;
        }
    }
#endif

private:
    void run() noexcept
    {
        for (;;) {
            int received = 0;
            const int status = sigwait(&signal_set_, &received);
            if (status != 0) {
                static_cast<void>(coordinator_->request(ServiceShutdownReason::signal_failure));
                return;
            }
            if (stopping_.load(std::memory_order_acquire)) return;
            const auto reason = received == SIGINT ? ServiceShutdownReason::interrupt
                : received == SIGTERM ? ServiceShutdownReason::terminate
                                      : ServiceShutdownReason::signal_failure;
            static_cast<void>(coordinator_->request(reason));
        }
    }

    std::shared_ptr<ServiceShutdownCoordinator> coordinator_;
    std::unique_ptr<ServiceSignalBlock> signal_block_;
    sigset_t signal_set_{};
    std::thread worker_;
    std::mutex stop_mutex_;
    std::atomic<bool> stopping_{};
    bool stopped_{};
};

#endif

ServiceSignalBlock::ServiceSignalBlock(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{}

ServiceSignalBlock::~ServiceSignalBlock() = default;

ServiceSignalBlockResult block_service_shutdown_signals() noexcept
{
    try {
#if defined(_WIN32)
        return {
            std::unique_ptr<ServiceSignalBlock>{
                new ServiceSignalBlock{std::make_unique<ServiceSignalBlock::Impl>()}},
            ServiceSignalError::none,
        };
#else
        sigset_t signals{};
        sigemptyset(&signals);
        sigaddset(&signals, SIGINT);
        sigaddset(&signals, SIGTERM);
        sigset_t previous{};
        if (pthread_sigmask(SIG_BLOCK, &signals, &previous) != 0) {
            return {nullptr, ServiceSignalError::signal_block_failed};
        }
        try {
            auto impl = std::make_unique<ServiceSignalBlock::Impl>(previous, pthread_self());
            return {
                std::unique_ptr<ServiceSignalBlock>{
                    new ServiceSignalBlock{std::move(impl)}},
                ServiceSignalError::none,
            };
        } catch (...) {
            static_cast<void>(pthread_sigmask(SIG_SETMASK, &previous, nullptr));
            throw;
        }
#endif
    } catch (...) {
        return {nullptr, ServiceSignalError::signal_block_failed};
    }
}

ServiceSignalOwner::ServiceSignalOwner(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{}

ServiceSignalOwner::~ServiceSignalOwner() = default;

void ServiceSignalOwner::stop() noexcept
{
    if (impl_) impl_->stop();
}

#if defined(BAAS_SERVICE_SHUTDOWN_TEST_HOOKS)
bool ServiceSignalOwner::notify_for_test(const ServiceShutdownReason reason) noexcept
{
    return impl_ && impl_->notify_for_test(reason);
}
#endif

ServiceSignalOwnerOpenResult open_service_signal_owner(
    std::shared_ptr<ServiceShutdownCoordinator> coordinator,
    std::unique_ptr<ServiceSignalBlock> signal_block) noexcept
{
    if (!coordinator) return {nullptr, ServiceSignalError::invalid_coordinator};
    if (!signal_block || !signal_block->impl_) {
        return {nullptr, ServiceSignalError::invalid_signal_block};
    }
    bool expected = false;
    if (!process_signal_owner_active.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_relaxed)) {
        return {nullptr, ServiceSignalError::already_active};
    }
    try {
#if defined(_WIN32)
        auto* events = ensure_console_events();
        if (events == nullptr) {
            process_signal_owner_active.store(false, std::memory_order_release);
            return {nullptr, ServiceSignalError::event_creation_failed};
        }
        ResetEvent(events->interrupt_event);
        ResetEvent(events->terminate_event);
        HANDLE stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (stop_event == nullptr) {
            process_signal_owner_active.store(false, std::memory_order_release);
            return {nullptr, ServiceSignalError::event_creation_failed};
        }
        std::unique_ptr<ServiceSignalOwner::Impl> impl;
        try {
            impl = std::make_unique<ServiceSignalOwner::Impl>(
                std::move(coordinator), std::move(signal_block), events, stop_event);
        } catch (...) {
            CloseHandle(stop_event);
            process_signal_owner_active.store(false, std::memory_order_release);
            throw;
        }
        const auto error = impl->start();
        if (error != ServiceSignalError::none) {
            impl->stop();
            return {nullptr, error};
        }
#else
        auto impl = std::make_unique<ServiceSignalOwner::Impl>(
            std::move(coordinator), std::move(signal_block));
        const auto error = impl->start();
        if (error != ServiceSignalError::none) return {nullptr, error};
#endif
        return {
            std::unique_ptr<ServiceSignalOwner>{
                new ServiceSignalOwner{std::move(impl)}},
            ServiceSignalError::none,
        };
    } catch (...) {
        process_signal_owner_active.store(false, std::memory_order_release);
        return {nullptr, ServiceSignalError::thread_start_failed};
    }
}

}  // namespace baas::service::app
