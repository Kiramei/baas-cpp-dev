#include "service/channels/RemoteHandler.h"

#include "service/auth/CanonicalJson.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baas::service::channels {
namespace {

using Json = nlohmann::json;
namespace ws = websocket;

struct RemoteConfig {
    std::optional<std::string> config_id;
    bool encrypt_device_to_client{true};
};

bool bounded_tree(const Json& value, const std::size_t maximum_depth,
                  const std::size_t maximum_nodes, const std::size_t depth,
                  std::size_t& nodes)
{
    if (++nodes > maximum_nodes || depth > maximum_depth) return false;
    if (value.is_array()) {
        for (const auto& child : value) {
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes))
                return false;
        }
    }
    else if (value.is_object()) {
        for (const auto& [key, child] : value.items()) {
            (void)key;
            if (!bounded_tree(child, maximum_depth, maximum_nodes, depth + 1, nodes))
                return false;
        }
    }
    return true;
}

std::optional<RemoteConfig> parse_config(
    const std::string_view text, const RemoteHandlerLimits& limits)
{
    if (text.size() > limits.max_config_json_bytes || !auth::is_valid_utf8(text))
        return std::nullopt;
    try {
        bool duplicate{};
        std::vector<std::unordered_set<std::string>> object_keys;
        const auto callback = [&duplicate, &object_keys](
            int, const Json::parse_event_t event, Json& parsed) {
            if (event == Json::parse_event_t::object_start) object_keys.emplace_back();
            else if (event == Json::parse_event_t::key && !object_keys.empty()) {
                if (!object_keys.back().insert(parsed.get<std::string>()).second)
                    duplicate = true;
            }
            else if (event == Json::parse_event_t::object_end && !object_keys.empty()) {
                object_keys.pop_back();
            }
            return !duplicate;
        };
        const auto value = Json::parse(text, callback, false);
        if (duplicate || value.is_discarded() || !value.is_object())
            return std::nullopt;
        std::size_t nodes{};
        if (!bounded_tree(value, limits.max_json_depth, limits.max_json_nodes, 0, nodes))
            return std::nullopt;

        const auto id = value.find("config_id");
        if (id == value.end() || (!id->is_null() && !id->is_string()))
            return std::nullopt;
        RemoteConfig result;
        if (id->is_string()) {
            auto parsed_id = id->get<std::string>();
            if (parsed_id.size() > limits.max_config_id_bytes
                || !auth::is_valid_utf8(parsed_id)) return std::nullopt;
            result.config_id = std::move(parsed_id);
        }
        const auto decrypt = value.find("decrypt");
        if (decrypt != value.end()) {
            if (!decrypt->is_boolean()) return std::nullopt;
            result.encrypt_device_to_client = decrypt->get<bool>();
        }
        return result;
    }
    catch (...) { return std::nullopt; }
}

ws::BusinessHandlerResult status_result(const ws::BusinessHandlerStatus status)
{
    return {{}, status};
}

ws::BusinessHandlerStatus backend_status(const RemoteBackendError error) noexcept
{
    switch (error) {
        case RemoteBackendError::not_found:
        case RemoteBackendError::invalid_config:
            return ws::BusinessHandlerStatus::protocol_failed;
        case RemoteBackendError::capacity:
            return ws::BusinessHandlerStatus::capacity;
        case RemoteBackendError::none:
        case RemoteBackendError::internal_error:
            return ws::BusinessHandlerStatus::internal_error;
    }
    return ws::BusinessHandlerStatus::internal_error;
}

class RemoteCore final : public std::enable_shared_from_this<RemoteCore> {
public:
    RemoteCore(std::weak_ptr<ws::BusinessPlaintextSink> output,
               const RemoteHandlerLimits limits)
        : output_(std::move(output)), limits_(limits)
    {}

    [[nodiscard]] RemoteIoStatus device_bytes(std::string payload) noexcept
    {
        const auto bytes = payload.size();
        {
            std::lock_guard lock{mutex_};
            if (!active_) return RemoteIoStatus::closed;
            if (terminal_ != ws::BusinessHandlerStatus::ok)
                return io_status(terminal_);
            if (bytes > limits_.max_device_frame_bytes
                || in_flight_frames_ >= limits_.max_in_flight_frames
                || bytes > limits_.max_in_flight_bytes - in_flight_bytes_) {
                terminal_ = ws::BusinessHandlerStatus::capacity;
                return RemoteIoStatus::capacity;
            }
            ++in_flight_frames_;
            in_flight_bytes_ += bytes;
        }

        std::shared_ptr<WriteCompletion> completion;
        try { completion = std::make_shared<WriteCompletion>(weak_from_this(), bytes); }
        catch (...) {
            settle(bytes, false, false);
            fail(ws::BusinessHandlerStatus::internal_error);
            return RemoteIoStatus::internal_error;
        }
        const auto output = output_.lock();
        if (!output) {
            completion->reject();
            request_clean_end();
            return RemoteIoStatus::closed;
        }
        const auto admitted = output->emit(
            {std::move(payload), false}, completion);
        if (admitted == ws::BusinessEmitResult::accepted) {
            completion->accept();
            const auto current = status();
            return current == ws::BusinessHandlerStatus::ok
                ? RemoteIoStatus::accepted : io_status(current);
        }
        completion->reject();
        const auto mapped = emit_status(admitted);
        if (mapped == RemoteIoStatus::closed) request_clean_end();
        else fail(mapped == RemoteIoStatus::capacity
                      ? ws::BusinessHandlerStatus::capacity
                      : ws::BusinessHandlerStatus::internal_error);
        return mapped;
    }

    void ended(const RemoteSessionEnd reason) noexcept
    {
        switch (reason) {
            case RemoteSessionEnd::clean:
            case RemoteSessionEnd::device_closed:
                request_clean_end();
                return;
            case RemoteSessionEnd::capacity:
                fail(ws::BusinessHandlerStatus::capacity);
                return;
            case RemoteSessionEnd::internal_error:
                fail(ws::BusinessHandlerStatus::internal_error);
                return;
        }
    }

    void report_io(const RemoteIoStatus status) noexcept
    {
        switch (status) {
            case RemoteIoStatus::accepted: return;
            case RemoteIoStatus::closed:
                request_clean_end();
                return;
            case RemoteIoStatus::capacity:
                fail(ws::BusinessHandlerStatus::capacity);
                return;
            case RemoteIoStatus::internal_error:
                fail(ws::BusinessHandlerStatus::internal_error);
                return;
        }
    }

    [[nodiscard]] ws::BusinessHandlerStatus status() const noexcept
    {
        std::lock_guard lock{mutex_};
        return terminal_;
    }

    void deactivate() noexcept
    {
        std::lock_guard lock{mutex_};
        active_ = false;
    }

private:
    class WriteCompletion final : public ws::BusinessBatchCompletion {
    public:
        WriteCompletion(std::weak_ptr<RemoteCore> owner, const std::size_t bytes)
            : owner_(std::move(owner)), bytes_(bytes)
        {}

        void complete(const ws::BusinessBatchWriteResult result) noexcept override
        {
            bool deliver{};
            {
                std::lock_guard lock{mutex_};
                if (completed_) return;
                completed_ = true;
                result_ = result;
                if (decision_ != Decision::pending) {
                    delivered_ = true;
                    deliver = true;
                }
            }
            if (deliver) deliver_result();
        }

        void accept() noexcept { decide(Decision::accepted); }
        void reject() noexcept { decide(Decision::rejected); }

    private:
        enum class Decision : std::uint8_t { pending, accepted, rejected };

        void decide(const Decision decision) noexcept
        {
            bool deliver{};
            {
                std::lock_guard lock{mutex_};
                if (decision_ != Decision::pending) return;
                decision_ = decision;
                if (completed_ && !delivered_) {
                    delivered_ = true;
                    deliver = true;
                }
                else if (decision == Decision::rejected && !completed_) {
                    // Rejected admission is already final even if a broken
                    // legacy sink forgets to complete its observer.
                    completed_ = true;
                    delivered_ = true;
                    result_ = ws::BusinessBatchWriteResult::failed;
                    deliver = true;
                }
            }
            if (deliver) deliver_result();
        }

        void deliver_result() noexcept
        {
            if (const auto owner = owner_.lock()) {
                owner->settle(
                    bytes_, decision_ == Decision::accepted,
                    result_ == ws::BusinessBatchWriteResult::written);
            }
        }

        std::mutex mutex_;
        std::weak_ptr<RemoteCore> owner_;
        std::size_t bytes_{};
        Decision decision_{Decision::pending};
        ws::BusinessBatchWriteResult result_{ws::BusinessBatchWriteResult::failed};
        bool completed_{};
        bool delivered_{};
    };

    static RemoteIoStatus io_status(const ws::BusinessHandlerStatus status) noexcept
    {
        switch (status) {
            case ws::BusinessHandlerStatus::ok: return RemoteIoStatus::accepted;
            case ws::BusinessHandlerStatus::capacity: return RemoteIoStatus::capacity;
            case ws::BusinessHandlerStatus::complete: return RemoteIoStatus::closed;
            case ws::BusinessHandlerStatus::protocol_failed:
            case ws::BusinessHandlerStatus::internal_error:
                return RemoteIoStatus::internal_error;
        }
        return RemoteIoStatus::internal_error;
    }

    static RemoteIoStatus emit_status(const ws::BusinessEmitResult status) noexcept
    {
        switch (status) {
            case ws::BusinessEmitResult::accepted: return RemoteIoStatus::accepted;
            case ws::BusinessEmitResult::closed: return RemoteIoStatus::closed;
            case ws::BusinessEmitResult::message_too_large:
            case ws::BusinessEmitResult::queue_full:
            case ws::BusinessEmitResult::queued_bytes_exceeded:
                return RemoteIoStatus::capacity;
            case ws::BusinessEmitResult::resource_exhausted:
            case ws::BusinessEmitResult::completion_unsupported:
                return RemoteIoStatus::internal_error;
        }
        return RemoteIoStatus::internal_error;
    }

    void settle(const std::size_t bytes, const bool admitted,
                const bool written) noexcept
    {
        std::lock_guard lock{mutex_};
        if (in_flight_frames_ != 0) --in_flight_frames_;
        if (bytes <= in_flight_bytes_) in_flight_bytes_ -= bytes;
        else in_flight_bytes_ = 0;
        if (active_ && admitted && !written
            && terminal_ == ws::BusinessHandlerStatus::ok) {
            terminal_ = ws::BusinessHandlerStatus::internal_error;
            clean_end_pending_ = false;
        }
        else if (active_ && admitted && written && clean_end_pending_
                 && in_flight_frames_ == 0
                 && terminal_ == ws::BusinessHandlerStatus::ok) {
            clean_end_pending_ = false;
            terminal_ = ws::BusinessHandlerStatus::complete;
        }
    }

    void fail(const ws::BusinessHandlerStatus status) noexcept
    {
        std::lock_guard lock{mutex_};
        if (active_ && terminal_ == ws::BusinessHandlerStatus::ok) {
            terminal_ = status;
            clean_end_pending_ = false;
        }
    }

    void request_clean_end() noexcept
    {
        std::lock_guard lock{mutex_};
        if (!active_ || terminal_ != ws::BusinessHandlerStatus::ok) return;
        if (in_flight_frames_ == 0) {
            terminal_ = ws::BusinessHandlerStatus::complete;
            clean_end_pending_ = false;
        }
        else {
            clean_end_pending_ = true;
        }
    }

    mutable std::mutex mutex_;
    std::weak_ptr<ws::BusinessPlaintextSink> output_;
    RemoteHandlerLimits limits_;
    ws::BusinessHandlerStatus terminal_{ws::BusinessHandlerStatus::ok};
    std::size_t in_flight_frames_{};
    std::size_t in_flight_bytes_{};
    bool clean_end_pending_{};
    bool active_{true};
};

class RemoteHandler final : public ws::BusinessChannelHandler {
public:
    RemoteHandler(std::shared_ptr<RemoteBackend> backend,
                  std::shared_ptr<ws::BusinessPlaintextSink> output,
                  const RemoteHandlerLimits limits)
        : backend_(std::move(backend)), output_(std::move(output)),
          limits_(limits), core_(std::make_shared<RemoteCore>(output_, limits_))
    {}

    ~RemoteHandler() override { shutdown(); }

    ws::BusinessHandlerResult ready(std::stop_token stop) override
    {
        return stop.stop_requested()
            ? status_result(ws::BusinessHandlerStatus::complete)
            : ws::BusinessHandlerResult{};
    }

    ws::BusinessHandlerResult input(auth::SecretBuffer plaintext,
                                    const bool peer_final,
                                    std::stop_token stop) override
    {
        if (closed_.load(std::memory_order_acquire) || stop.stop_requested())
            return status_result(ws::BusinessHandlerStatus::complete);
        if (!configured_.exchange(true, std::memory_order_acq_rel)) {
            if (peer_final)
                return status_result(ws::BusinessHandlerStatus::protocol_failed);
            return configure(std::move(plaintext), stop);
        }

        const auto current = core_->status();
        if (current != ws::BusinessHandlerStatus::ok) return status_result(current);
        if (peer_final && plaintext.empty()) {
            shutdown_session();
            const auto status = core_->status();
            return status == ws::BusinessHandlerStatus::ok
                    || status == ws::BusinessHandlerStatus::complete
                ? status_result(ws::BusinessHandlerStatus::complete)
                : status_result(status);
        }
        RemoteIoStatus sent{RemoteIoStatus::closed};
        std::shared_ptr<RemoteSession> session;
        {
            std::lock_guard lock{session_mutex_};
            session = session_;
        }
        if (session) {
            try { sent = session->send_to_device(std::move(plaintext), stop); }
            catch (...) { sent = RemoteIoStatus::internal_error; }
        }
        core_->report_io(sent);
        if (sent != RemoteIoStatus::accepted || peer_final) shutdown_session();
        if (closed_.load(std::memory_order_acquire))
            return status_result(ws::BusinessHandlerStatus::complete);
        const auto status = core_->status();
        if (peer_final && status == ws::BusinessHandlerStatus::ok)
            return status_result(ws::BusinessHandlerStatus::complete);
        return status == ws::BusinessHandlerStatus::ok
            ? ws::BusinessHandlerResult{} : status_result(status);
    }

    ws::BusinessHandlerResult heartbeat(std::stop_token stop) override
    {
        if (closed_.load(std::memory_order_acquire) || stop.stop_requested())
            return status_result(ws::BusinessHandlerStatus::complete);
        const auto status = core_->status();
        if (status != ws::BusinessHandlerStatus::ok) shutdown_session();
        return status == ws::BusinessHandlerStatus::ok
            ? ws::BusinessHandlerResult{} : status_result(status);
    }

    void closed(ws::BusinessCloseReason) noexcept override { shutdown(); }

private:
    ws::BusinessHandlerResult configure(auth::SecretBuffer plaintext,
                                        std::stop_token stop)
    {
        const std::string_view text{
            reinterpret_cast<const char*>(plaintext.bytes().data()), plaintext.size()};
        auto config = parse_config(text, limits_);
        if (!config) return status_result(ws::BusinessHandlerStatus::protocol_failed);
        if (!config->encrypt_device_to_client
            && !output_->enable_remote_raw_output()) {
            return status_result(ws::BusinessHandlerStatus::internal_error);
        }

        RemoteOpenResult opened;
        try {
            opened = backend_->open(
                std::move(config->config_id),
                { [core = core_](RemoteDeviceMessageKind, std::string payload) {
                      return core->device_bytes(std::move(payload));
                  },
                  [core = core_](const RemoteSessionEnd reason) {
                      core->ended(reason);
                  } },
                stop);
        }
        catch (...) {
            return status_result(ws::BusinessHandlerStatus::internal_error);
        }
        if (!opened) {
            if (opened.session) opened.session->close();
            return status_result(backend_status(opened.error));
        }
        if (closed_.load(std::memory_order_acquire)
            || core_->status() != ws::BusinessHandlerStatus::ok) {
            opened.session->close();
            const auto status = core_->status();
            return status == ws::BusinessHandlerStatus::ok
                ? status_result(ws::BusinessHandlerStatus::complete)
                : status_result(status);
        }
        std::shared_ptr<RemoteSession> session;
        try { session = std::shared_ptr<RemoteSession>{std::move(opened.session)}; }
        catch (...) {
            if (opened.session) opened.session->close();
            return status_result(ws::BusinessHandlerStatus::internal_error);
        }
        {
            std::lock_guard lock{session_mutex_};
            if (!closed_.load(std::memory_order_acquire)) session_ = session;
        }
        if (closed_.load(std::memory_order_acquire)) {
            // Close outside the ownership lock so an in-progress callback or
            // send can complete without lock inversion.
            session->close();
            return status_result(ws::BusinessHandlerStatus::complete);
        }
        return {};
    }

    void shutdown_session() noexcept
    {
        std::shared_ptr<RemoteSession> session;
        {
            std::lock_guard lock{session_mutex_};
            session = std::move(session_);
        }
        if (session) session->close();
    }

    void shutdown() noexcept
    {
        if (closed_.exchange(true, std::memory_order_acq_rel)) return;
        core_->deactivate();
        shutdown_session();
    }

    std::shared_ptr<RemoteBackend> backend_;
    std::shared_ptr<ws::BusinessPlaintextSink> output_;
    RemoteHandlerLimits limits_;
    std::shared_ptr<RemoteCore> core_;
    std::mutex session_mutex_;
    std::shared_ptr<RemoteSession> session_;
    std::atomic_bool configured_{};
    std::atomic_bool closed_{};
};

}  // namespace

RemoteHandlerFactory::RemoteHandlerFactory(
    std::shared_ptr<RemoteBackend> backend, const RemoteHandlerLimits limits)
    : backend_(std::move(backend)), limits_(limits)
{
    if (!backend_ || limits_.max_config_json_bytes == 0
        || limits_.max_config_id_bytes == 0 || limits_.max_json_depth == 0
        || limits_.max_json_nodes == 0 || limits_.max_device_frame_bytes == 0
        || limits_.max_in_flight_frames == 0 || limits_.max_in_flight_bytes == 0
        || limits_.max_config_id_bytes > limits_.max_config_json_bytes
        || limits_.max_device_frame_bytes > limits_.max_in_flight_bytes) {
        throw std::invalid_argument("invalid remote handler configuration");
    }
}

ws::BusinessHandlerCreateResult RemoteHandlerFactory::create(
    ws::BusinessSessionContext context,
    std::shared_ptr<ws::BusinessPlaintextSink> output,
    std::stop_token stop)
{
    if (!output || stop.stop_requested()
        || context.channel != auth::BusinessChannel::remote) {
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    }
    try {
        return {std::make_unique<RemoteHandler>(backend_, std::move(output), limits_),
                ws::BusinessHandlerCreateError::none};
    }
    catch (...) {
        return {nullptr, ws::BusinessHandlerCreateError::internal_error};
    }
}

}  // namespace baas::service::channels
