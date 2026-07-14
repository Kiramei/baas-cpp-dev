#include "service/http/HttpHost.h"

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace baas::service::http {
namespace {

thread_local const void* active_http_host = nullptr;

class ActiveHostScope final {
public:
    explicit ActiveHostScope(const void* host) noexcept
        : previous_(active_http_host)
    {
        active_http_host = host;
    }
    ~ActiveHostScope() { active_http_host = previous_; }

private:
    const void* previous_;
};

struct ListenerLaunch final {
    std::mutex mutex;
    std::condition_variable changed;
    bool launch = false;
    bool cancel = false;
};

[[nodiscard]] router::Router make_router(
    HttpHostRouterConfig config,
    const std::shared_ptr<router::ShutdownIntent>& shutdown_intent
)
{
    if (config.health_snapshot.has_value() && config.health_provider) {
        throw std::invalid_argument(
            "HTTP host router config must select either a health snapshot or provider"
        );
    }
    if (config.health_snapshot.has_value()) {
        return router::Router::with_health_snapshot(
            std::move(config.service),
            std::move(*config.health_snapshot),
            config.budget,
            shutdown_intent.get()
        );
    }
    if (config.health_provider) {
        return router::Router::with_health_provider(
            std::move(config.service),
            std::move(config.health_provider),
            config.budget,
            shutdown_intent.get()
        );
    }
    return router::Router{
        std::move(config.service), config.budget, shutdown_intent.get()
    };
}

void validate_host_config(const HttpHostConfig& config)
{
    if (config.worker_count == 0 || config.worker_count > http_host_max_worker_count) {
        throw std::invalid_argument("HTTP host worker count must be in 1..256");
    }
    if (config.max_queued_requests == 0
        || config.max_queued_requests > http_host_max_queued_requests) {
        throw std::invalid_argument("HTTP host request queue must be in 1..65536");
    }
    if (config.ready_timeout <= std::chrono::milliseconds::zero()
        || config.read_timeout <= std::chrono::milliseconds::zero()
        || config.write_timeout <= std::chrono::milliseconds::zero()
        || config.idle_interval <= std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("HTTP host timeouts and idle interval must be positive");
    }
}

class TrackingTaskQueue final : public httplib::TaskQueue {
public:
    TrackingTaskQueue(
        const std::size_t workers,
        const std::size_t queued_requests,
        std::atomic<std::size_t>& rejections,
        const void* owner
    )
        : pool_(workers, queued_requests), rejections_(rejections), owner_(owner)
    {}

    bool enqueue(std::function<void()> task) override
    {
        auto wrapped = [task = std::move(task), owner = owner_]() mutable {
            ActiveHostScope active{owner};
            task();
        };
        if (pool_.enqueue(std::move(wrapped))) return true;
        rejections_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    void shutdown() override { pool_.shutdown(); }
    void on_idle() override { pool_.on_idle(); }

private:
    httplib::ThreadPool pool_;
    std::atomic<std::size_t>& rejections_;
    const void* owner_;
};

}  // namespace

class HttpHost::Impl final {
public:
    Impl(
        HttpHostRouterConfig router_config,
        const InputBudget input_budget,
        const HttpHostConfig host_config
    )
        : shutdown_intent_(std::move(router_config.shutdown_intent)),
          router_(make_router(
              std::move(router_config), shutdown_intent_
          )),
          adapter_(router_, input_budget, host_config.cors_policy),
          config_(host_config)
    {
        validate_host_config(config_);
    }

    ~Impl() = default;

    [[nodiscard]] HttpHostStartResult start()
    {
        std::lock_guard<std::mutex> lifecycle_lock{lifecycle_mutex_};
        {
            std::lock_guard<std::mutex> lock{state_mutex_};
            if (server_stop_failed_) {
                return {false, HttpHostStartError::listener_start_failed, 0};
            }
            if (state_ == HttpHostState::starting || state_ == HttpHostState::running
                || state_ == HttpHostState::stopping) {
                return {false, HttpHostStartError::already_active, port_};
            }
        }
        if (!join_stale_listener()) {
            return {false, HttpHostStartError::listener_start_failed, 0};
        }
        {
            std::lock_guard<std::mutex> lock{state_mutex_};
            state_ = HttpHostState::starting;
            last_error_ = HttpHostStartError::none;
            last_error_message_.clear();
            port_ = 0;
            listen_returned_ = false;
            server_stop_requested_ = false;
            server_stop_failed_ = false;
        }
        queue_rejections_.store(0, std::memory_order_relaxed);

        auto server = std::make_unique<httplib::Server>();
        server->set_address_family(AF_INET);
        server->set_socket_options([](const socket_t socket) {
            int enabled = 1;
#ifdef _WIN32
            setsockopt(
                socket,
                SOL_SOCKET,
                SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&enabled),
                sizeof(enabled)
            );
#else
            setsockopt(
                socket,
                SOL_SOCKET,
                SO_REUSEADDR,
                reinterpret_cast<const void*>(&enabled),
                sizeof(enabled)
            );
#endif
        });
        server->set_read_timeout(config_.read_timeout);
        server->set_write_timeout(config_.write_timeout);
        server->set_idle_interval(config_.idle_interval);
        server->new_task_queue = [this] {
            return new TrackingTaskQueue(
                config_.worker_count,
                config_.max_queued_requests,
                queue_rejections_,
                this
            );
        };
        adapter_.install(*server);
        // Allocate the bind address before a listener thread exists. This
        // keeps native allocation failure from stranding a launch waiter.
        const std::string loopback_address{http_host_loopback_address};

        auto launch = std::make_shared<ListenerLaunch>();
        auto* const listening_server = server.get();
        std::function<void()> listener_task = [this, listening_server, launch] {
            {
                std::unique_lock<std::mutex> lock{launch->mutex};
                launch->changed.wait(lock, [&] { return launch->launch || launch->cancel; });
                if (launch->cancel) return;
            }
            bool result = false;
            bool threw = false;
            try {
                result = listening_server->listen_after_bind();
            } catch (...) {
                threw = true;
            }
            try {
                std::lock_guard<std::mutex> lock{state_mutex_};
                listen_returned_ = true;
                // listen_after_bind owns/consumes the listening socket. Once it
                // returns, calling Server::stop() again can target an invalid
                // socket while cpp-httplib is still publishing is_running_=false.
                server_stop_requested_ = true;
                if (state_ != HttpHostState::stopping) {
                    state_ = HttpHostState::failed;
                    port_ = 0;
                    last_error_ = HttpHostStartError::listen_failed;
                    last_error_message_ = threw
                        ? "cpp-httplib listener threw an exception"
                        : (result
                            ? "HTTP listener returned without a stop request"
                            : "HTTP listener failed after binding");
                } else if (threw) {
                    last_error_message_ = "cpp-httplib listener threw during stop";
                }
                state_changed_.notify_all();
            } catch (...) {
                record_start_failure_noexcept(
                    HttpHostStartError::listen_failed,
                    "failed to record HTTP listener completion"
                );
            }
        };
        try {
            listener_ = config_.listener_thread_factory
                ? config_.listener_thread_factory(std::move(listener_task))
                : std::thread(std::move(listener_task));
            if (!listener_.joinable()) {
                throw std::runtime_error("listener thread factory returned no thread");
            }
        } catch (const std::exception& error) {
            static_cast<void>(error);
            static_cast<void>(cancel_listener_launch(launch));
            record_start_failure_noexcept(
                HttpHostStartError::listener_start_failed,
                "failed to create HTTP listener thread"
            );
            return {false, HttpHostStartError::listener_start_failed, 0};
        } catch (...) {
            static_cast<void>(cancel_listener_launch(launch));
            record_start_failure_noexcept(
                HttpHostStartError::listener_start_failed,
                "failed to create HTTP listener thread"
            );
            return {false, HttpHostStartError::listener_start_failed, 0};
        }

        int bound_port = -1;
        try {
            bound_port = config_.port == 0
                ? server->bind_to_any_port(loopback_address)
                : (server->bind_to_port(loopback_address, config_.port)
                       ? static_cast<int>(config_.port)
                       : -1);
        } catch (...) {
            if (!cancel_listener_launch(launch)) {
                record_start_failure_noexcept(
                    HttpHostStartError::listener_start_failed,
                    "failed to join listener after HTTP bind exception"
                );
                return {false, HttpHostStartError::listener_start_failed, 0};
            }
            record_start_failure_noexcept(
                HttpHostStartError::bind_failed,
                "cpp-httplib threw while binding the loopback HTTP port"
            );
            return {false, HttpHostStartError::bind_failed, 0};
        }
        if (bound_port <= 0
            || bound_port > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
            if (!cancel_listener_launch(launch)) {
                record_start_failure_noexcept(
                    HttpHostStartError::listener_start_failed,
                    "failed to join cancelled HTTP listener"
                );
                return {false, HttpHostStartError::listener_start_failed, 0};
            }
            fail_start(
                HttpHostStartError::bind_failed,
                "failed to bind the configured loopback HTTP port"
            );
            return {false, HttpHostStartError::bind_failed, 0};
        }

        server_ = std::move(server);
        const auto public_port = static_cast<std::uint16_t>(bound_port);
        {
            std::lock_guard<std::mutex> lock{state_mutex_};
            port_ = public_port;
        }
        {
            std::lock_guard<std::mutex> lock{launch->mutex};
            launch->launch = true;
        }
        launch->changed.notify_one();

        const auto deadline = std::chrono::steady_clock::now() + config_.ready_timeout;
        std::unique_lock<std::mutex> lock{state_mutex_};
        while (!listen_returned_ && !listening_server->is_running()) {
            if (state_changed_.wait_until(
                    lock,
                    std::min(deadline, std::chrono::steady_clock::now()
                                           + std::chrono::milliseconds{1})
                ) == std::cv_status::timeout
                && std::chrono::steady_clock::now() >= deadline) {
                state_ = HttpHostState::stopping;
                lock.unlock();
                try {
                    listening_server->stop();
                    if (listener_.joinable()) listener_.join();
                } catch (...) {
                    record_start_failure_noexcept(
                        HttpHostStartError::listener_start_failed,
                        "failed to clean up timed-out HTTP listener"
                    );
                    return {false, HttpHostStartError::listener_start_failed, 0};
                }
                server_.reset();
                fail_start(
                    HttpHostStartError::ready_timeout,
                    "HTTP listener did not become ready within the configured timeout"
                );
                return {false, HttpHostStartError::ready_timeout, 0};
            }
        }
        if (listen_returned_) {
            const auto error = last_error_ == HttpHostStartError::none
                ? HttpHostStartError::listen_failed : last_error_;
            lock.unlock();
            try {
                if (listener_.joinable()) listener_.join();
            } catch (...) {
                record_start_failure_noexcept(
                    HttpHostStartError::listener_start_failed,
                    "failed to join returned HTTP listener"
                );
                return {false, HttpHostStartError::listener_start_failed, 0};
            }
            server_.reset();
            return {false, error, 0};
        }
        state_ = HttpHostState::running;
        return {true, HttpHostStartError::none, public_port};
    }

    [[nodiscard]] bool stop() noexcept
    {
        try {
            std::lock_guard<std::mutex> lifecycle_lock{lifecycle_mutex_};
            httplib::Server* server = nullptr;
            bool request_server_stop = false;
            {
                std::lock_guard<std::mutex> lock{state_mutex_};
                if (server_ == nullptr && !listener_.joinable()) {
                    state_ = HttpHostState::stopped;
                    port_ = 0;
                    return true;
                }
                if (server_stop_failed_) return false;
                state_ = HttpHostState::stopping;
                server = server_.get();
                if (server != nullptr && !server_stop_requested_) {
                    server_stop_requested_ = true;
                    request_server_stop = true;
                }
            }
            if (request_server_stop) {
                try {
                    server->stop();
                } catch (const std::exception&) {
                    record_server_stop_failure_noexcept();
                    return false;
                } catch (...) {
                    record_server_stop_failure_noexcept();
                    return false;
                }
            }
            if (active_http_host == this) {
                record_stop_failure_noexcept(
                    "stop requested from a request worker; join deferred to the owner",
                    true
                );
                return false;
            }
            if (listener_.joinable()) {
                try {
                    listener_.join();
                } catch (const std::exception&) {
                    record_stop_failure_noexcept(
                        "HTTP listener join failed", false
                    );
                    return false;
                } catch (...) {
                    record_stop_failure_noexcept("HTTP listener join failed", false);
                    return false;
                }
            }
            server_.reset();
            {
                std::lock_guard<std::mutex> lock{state_mutex_};
                state_ = HttpHostState::stopped;
                port_ = 0;
                listen_returned_ = false;
                server_stop_requested_ = false;
                server_stop_failed_ = false;
            }
            return true;
        } catch (const std::exception&) {
            record_stop_failure_noexcept(
                "HTTP host stop failed", false
            );
            return false;
        } catch (...) {
            record_stop_failure_noexcept("HTTP host stop failed", false);
            return false;
        }
    }

    [[nodiscard]] HttpHostState state() const noexcept
    {
        std::lock_guard<std::mutex> lock{state_mutex_};
        return state_;
    }

    [[nodiscard]] std::uint16_t port() const noexcept
    {
        std::lock_guard<std::mutex> lock{state_mutex_};
        return port_;
    }

    [[nodiscard]] HttpHostStartError last_start_error() const noexcept
    {
        std::lock_guard<std::mutex> lock{state_mutex_};
        return last_error_;
    }

    [[nodiscard]] std::string last_error_message() const
    {
        std::lock_guard<std::mutex> lock{state_mutex_};
        return last_error_message_;
    }

    [[nodiscard]] std::size_t queue_rejections() const noexcept
    {
        return queue_rejections_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const HttpHostConfig& config() const noexcept { return config_; }

private:
    void fail_start(const HttpHostStartError error, std::string message)
    {
        std::lock_guard<std::mutex> lock{state_mutex_};
        state_ = HttpHostState::failed;
        port_ = 0;
        last_error_ = error;
        last_error_message_ = std::move(message);
    }

    [[nodiscard]] bool cancel_listener_launch(
        const std::shared_ptr<ListenerLaunch>& launch
    ) noexcept
    {
        try {
            {
                std::lock_guard<std::mutex> lock{launch->mutex};
                launch->cancel = true;
            }
            launch->changed.notify_one();
            if (listener_.joinable()) listener_.join();
            return true;
        } catch (...) {
            // The caller records listener_start_failed. If join itself failed,
            // the retained Impl prevents dependency destruction.
            return false;
        }
    }

    void record_start_failure_noexcept(
        const HttpHostStartError error,
        const std::string_view message
    ) noexcept
    {
        try {
            std::lock_guard<std::mutex> lock{state_mutex_};
            state_ = HttpHostState::failed;
            port_ = 0;
            last_error_ = error;
            last_error_message_ = message;
        } catch (...) {}
    }

    void record_stop_failure_noexcept(
        const std::string_view message,
        const bool stopping
    ) noexcept
    {
        try {
            std::lock_guard<std::mutex> lock{state_mutex_};
            state_ = stopping ? HttpHostState::stopping : HttpHostState::failed;
            last_error_message_ = message;
        } catch (...) {}
    }

    void record_server_stop_failure_noexcept() noexcept
    {
        try {
            std::lock_guard<std::mutex> lock{state_mutex_};
            state_ = HttpHostState::failed;
            last_error_message_ = "cpp-httplib stop failed; ownership retained";
            server_stop_failed_ = true;
        } catch (...) {}
    }

    [[nodiscard]] bool join_stale_listener() noexcept
    {
        try {
            if (listener_.joinable()) listener_.join();
            server_.reset();
            return true;
        } catch (const std::exception&) {
            record_start_failure_noexcept(
                HttpHostStartError::listener_start_failed,
                "failed to join previous HTTP listener"
            );
        } catch (...) {
            record_start_failure_noexcept(
                HttpHostStartError::listener_start_failed,
                "failed to join previous HTTP listener"
            );
        }
        return false;
    }

    // This shared owner precedes Router so it outlives every Router request.
    // Router directly retains the health provider shared owner.
    std::shared_ptr<router::ShutdownIntent> shutdown_intent_;
    router::Router router_;
    HttplibAdapter adapter_;
    HttpHostConfig config_;

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex state_mutex_;
    std::condition_variable state_changed_;
    std::unique_ptr<httplib::Server> server_;
    std::thread listener_;
    std::atomic<std::size_t> queue_rejections_{0};
    HttpHostState state_ = HttpHostState::stopped;
    HttpHostStartError last_error_ = HttpHostStartError::none;
    std::string last_error_message_;
    std::uint16_t port_ = 0;
    bool listen_returned_ = false;
    bool server_stop_requested_ = false;
    bool server_stop_failed_ = false;
};

HttpHost::HttpHost(
    HttpHostRouterConfig router_config,
    const InputBudget input_budget,
    const HttpHostConfig host_config
)
    : impl_(std::make_unique<Impl>(
          std::move(router_config), input_budget, host_config
      ))
{}

HttpHost::~HttpHost()
{
    if (impl_ && !impl_->stop()) {
        // A join/transport failure leaves threads potentially referring to the
        // implementation. Retaining the entire ownership graph is safer than
        // destroying it and risking a dangling handler or std::terminate.
        static_cast<void>(impl_.release());
    }
}

HttpHostStartResult HttpHost::start()
{
    return impl_->start();
}

void HttpHost::stop() noexcept
{
    static_cast<void>(impl_->stop());
}

HttpHostState HttpHost::state() const noexcept
{
    return impl_->state();
}

std::uint16_t HttpHost::port() const noexcept
{
    return impl_->port();
}

std::string HttpHost::address() const
{
    return std::string{http_host_loopback_address};
}

HttpHostStartError HttpHost::last_start_error() const noexcept
{
    return impl_->last_start_error();
}

std::string HttpHost::last_error_message() const
{
    return impl_->last_error_message();
}

std::size_t HttpHost::queue_rejections() const noexcept
{
    return impl_->queue_rejections();
}

const HttpHostConfig& HttpHost::config() const noexcept
{
    return impl_->config();
}

}  // namespace baas::service::http
