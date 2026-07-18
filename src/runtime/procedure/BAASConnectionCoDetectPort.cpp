#include "runtime/procedure/BAASConnectionCoDetectPort.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace baas::runtime::procedure {
namespace {

inline constexpr std::uint64_t maximum_wait_ms = 60'000U * 64U;
inline constexpr std::uint64_t maximum_wait_slice_ms = 50U;

[[nodiscard]] std::optional<CoDetectOperationError> control_error(
    const CoDetectControl& control) noexcept
{
    switch (control.poll()) {
    case CoDetectControlState::Proceed: return std::nullopt;
    case CoDetectControlState::ContextDeadlineExceeded:
        return CoDetectOperationError::ContextDeadlineExceeded;
    case CoDetectControlState::CallDeadlineExceeded:
        return CoDetectOperationError::CallDeadlineExceeded;
    case CoDetectControlState::Cancelled:
        return CoDetectOperationError::Cancelled;
    case CoDetectControlState::SessionChanged:
        return CoDetectOperationError::SessionChanged;
    }
    return CoDetectOperationError::Internal;
}

[[nodiscard]] bool valid_device_id(const std::string& value) noexcept
{
    return !value.empty() && value.size() <= 1'024U && value.find('\0') == std::string::npos;
}

[[nodiscard]] bool valid_profile(const CoDetectProfile profile) noexcept
{
    return !co_detect_profile_name(profile).empty();
}

[[nodiscard]] bool valid_frame(const BAASConnectionCoDetectRawFrame& frame) noexcept
{
    return frame.width == baas_connection_co_detect_width &&
        frame.height == baas_connection_co_detect_height &&
        frame.row_stride == baas_connection_co_detect_row_stride &&
        frame.pixels.size() == baas_connection_co_detect_frame_bytes;
}

[[nodiscard]] bool valid_published_frame(
    const CoDetectProductionBgrFrame& frame,
    const std::shared_ptr<const CoDetectProductionDeviceIdentity>& identity) noexcept
{
    return frame.identity && frame.identity.get() == identity.get() &&
        frame.width == baas_connection_co_detect_width &&
        frame.height == baas_connection_co_detect_height &&
        frame.row_stride == baas_connection_co_detect_row_stride && frame.pixels &&
        frame.pixels->size() == baas_connection_co_detect_frame_bytes;
}

}  // namespace

#if defined(BAAS_CONNECTION_CO_DETECT_PORT_TESTING)
namespace detail {
namespace {
std::atomic<std::int64_t> activation_allocation_countdown{-1};
}

void fail_activation_allocation_after(
    const std::size_t successful_checkpoints) noexcept
{
    activation_allocation_countdown.store(
        successful_checkpoints > static_cast<std::size_t>(
                                     std::numeric_limits<std::int64_t>::max())
            ? std::numeric_limits<std::int64_t>::max()
            : static_cast<std::int64_t>(successful_checkpoints),
        std::memory_order_release);
}

void clear_activation_allocation_failure() noexcept
{
    activation_allocation_countdown.store(-1, std::memory_order_release);
}
}  // namespace detail
#endif

class BAASConnectionCoDetectOwner::State final
    : public std::enable_shared_from_this<BAASConnectionCoDetectOwner::State> {
public:
    struct Session final {
        enum class Status : std::uint8_t { Candidate, Current, Tombstoned, Retired };

        explicit Session(std::shared_ptr<BAASConnectionCoDetectBackend> value) noexcept
            : backend(std::move(value)) {}

        std::shared_ptr<BAASConnectionCoDetectBackend> backend;
        std::shared_ptr<const CoDetectProductionDeviceIdentity> identity;
        mutable std::mutex operation_mutex;
        mutable std::mutex wait_mutex;
        std::condition_variable wait_condition;
        std::shared_ptr<const CoDetectProductionBgrFrame> latest;
        Status status{Status::Candidate};
    };

    class Port final : public CoDetectProductionDevicePort {
    public:
        Port(std::shared_ptr<State> state, std::shared_ptr<Session> session) noexcept
            : state_(std::move(state)), session_(std::move(session)) {}

        [[nodiscard]] std::shared_ptr<const CoDetectProductionDeviceIdentity>
        current_identity() const noexcept override
        {
            try {
                std::scoped_lock lock(session_->operation_mutex);
                if (session_error_locked()) return {};
                return session_->identity;
            } catch (...) {
                return {};
            }
        }

        [[nodiscard]] std::uint64_t monotonic_ms() const noexcept override
        {
            const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
            const auto milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            return milliseconds < 0 ? 0U : static_cast<std::uint64_t>(milliseconds);
        }

        [[nodiscard]] std::uint64_t screenshot_interval_ms() const noexcept override
        {
            try {
                std::scoped_lock lock(session_->operation_mutex);
                if (session_error_locked()) return 0;
                const auto value = session_->backend->screenshot_interval_ms();
                if (session_error_locked()) return 0;
                return value >= 1U && value <= 60'000U ? value : 0U;
            } catch (...) {
                return 0;
            }
        }

        [[nodiscard]] std::shared_ptr<const CoDetectProductionBgrFrame>
        latest_frame() const noexcept override
        {
            try {
                std::scoped_lock lock(session_->operation_mutex);
                if (session_error_locked() || !session_->latest ||
                    !valid_published_frame(*session_->latest, session_->identity))
                    return {};
                return session_->latest;
            } catch (...) {
                return {};
            }
        }

        [[nodiscard]] bool publish_latest_frame(
            std::shared_ptr<const CoDetectProductionBgrFrame> frame) noexcept override
        {
            try {
                std::scoped_lock lock(session_->operation_mutex);
                if (!frame || session_error_locked() ||
                    !valid_published_frame(*frame, session_->identity))
                    return false;
                session_->latest = std::move(frame);
                return true;
            } catch (...) {
                return false;
            }
        }

        [[nodiscard]] CoDetectResult<CoDetectProductionBgrFrame> capture(
            const CoDetectControl& control) override
        {
            try {
                std::scoped_lock lock(session_->operation_mutex);
                if (const auto error = guard(control)) return *error;
                const BAASConnectionCoDetectCheckpoint checkpoint = [this, &control] {
                    if (const auto error = guard(control))
                        return CoDetectResult<std::monostate>{*error};
                    return CoDetectResult<std::monostate>{std::monostate{}};
                };
                auto captured = session_->backend->capture(checkpoint);
                if (const auto error = guard(control)) return *error;
                if (const auto* error = std::get_if<CoDetectOperationError>(&captured))
                    return *error;
                auto frame = std::get<BAASConnectionCoDetectRawFrame>(std::move(captured));
                if (!valid_frame(frame)) return CoDetectOperationError::Unavailable;
                auto pixels = std::make_shared<const std::vector<std::byte>>(
                    std::move(frame.pixels));
                CoDetectProductionBgrFrame result{
                    session_->identity, baas_connection_co_detect_width,
                    baas_connection_co_detect_height, baas_connection_co_detect_row_stride,
                    std::move(pixels)};
                auto latest =
                    std::make_shared<const CoDetectProductionBgrFrame>(result);
                if (const auto error = guard(control)) return *error;
                session_->latest = std::move(latest);
                return result;
            } catch (const std::bad_alloc&) {
                return CoDetectOperationError::ResourceExhausted;
            } catch (...) {
                return CoDetectOperationError::Internal;
            }
        }

        [[nodiscard]] CoDetectResult<std::monostate> click(
            const CoDetectClick click, const CoDetectControl& control) override
        {
            if (click.match_only() || click.x > static_cast<std::int32_t>(
                    baas_connection_co_detect_width) ||
                click.y > static_cast<std::int32_t>(baas_connection_co_detect_height))
                return CoDetectOperationError::Unavailable;
            try {
                std::scoped_lock lock(session_->operation_mutex);
                if (const auto error = guard(control)) return *error;
                auto result = session_->backend->click(click);
                if (const auto error = guard(control)) return *error;
                return result;
            } catch (const std::bad_alloc&) {
                return CoDetectOperationError::ResourceExhausted;
            } catch (...) {
                return CoDetectOperationError::Internal;
            }
        }

        [[nodiscard]] CoDetectResult<std::monostate> wait(
            const std::uint64_t milliseconds, const CoDetectControl& control) override
        {
            if (milliseconds > maximum_wait_ms) return CoDetectOperationError::Unavailable;
            if (const auto error = probe(control)) return *error;
            const auto start = std::chrono::steady_clock::now();
            const auto deadline = start + std::chrono::milliseconds(milliseconds);
            std::unique_lock wait_lock(session_->wait_mutex);
            while (std::chrono::steady_clock::now() < deadline) {
                if (const auto error = probe(control)) return *error;
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                const auto slice = std::min(
                    remaining, std::chrono::milliseconds(maximum_wait_slice_ms));
                if (slice.count() > 0) session_->wait_condition.wait_for(wait_lock, slice);
            }
            if (const auto error = probe(control)) return *error;
            return std::monostate{};
        }

        [[nodiscard]] CoDetectResult<bool> foreground_matches(
            const CoDetectControl& control) override
        {
            try {
                std::scoped_lock lock(session_->operation_mutex);
                if (const auto error = guard(control)) return *error;
                auto result = session_->backend->foreground_matches();
                if (const auto error = guard(control)) return *error;
                return result;
            } catch (const std::bad_alloc&) {
                return CoDetectOperationError::ResourceExhausted;
            } catch (...) {
                return CoDetectOperationError::Internal;
            }
        }

    private:
        [[nodiscard]] std::optional<CoDetectOperationError> session_error_locked()
            const noexcept
        {
            if (session_->status != Session::Status::Current ||
                !state_->is_current(session_))
                return CoDetectOperationError::SessionChanged;
            if (!session_->backend->identity_valid()) {
                session_->status = Session::Status::Tombstoned;
                session_->latest.reset();
                session_->wait_condition.notify_all();
                return CoDetectOperationError::SessionChanged;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<CoDetectOperationError> guard(
            const CoDetectControl& control) const noexcept
        {
            if (const auto error = control_error(control)) return error;
            return session_error_locked();
        }

        [[nodiscard]] std::optional<CoDetectOperationError> probe(
            const CoDetectControl& control) const noexcept
        {
            std::scoped_lock lock(session_->operation_mutex);
            return guard(control);
        }

        std::shared_ptr<State> state_;
        std::shared_ptr<Session> session_;
    };

    [[nodiscard]] BAASConnectionCoDetectBinding activate(
        std::shared_ptr<BAASConnectionCoDetectBackend> backend,
        const CoDetectProfile profile,
        const std::uint64_t session_epoch,
        const bool android)
    {
        if (!backend || !android || session_epoch == 0 || !valid_profile(profile))
            throw std::invalid_argument("invalid BAAS co-detect device binding");

        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        if (session_epoch <= last_epoch_)
            throw std::invalid_argument("BAAS co-detect epoch must strictly increase");

        activation_allocation_checkpoint();
        auto next = std::make_shared<Session>(std::move(backend));
        std::string device_id;
        {
            std::scoped_lock next_lock(next->operation_mutex);
            device_id = candidate_device_id_locked(*next, profile);
        }
        activation_allocation_checkpoint();
        auto identity = std::make_shared<const CoDetectProductionDeviceIdentity>(
            CoDetectProductionDeviceIdentity{
                std::move(device_id), profile, session_epoch, true});
        next->identity = identity;
        activation_allocation_checkpoint();
        auto port = std::make_shared<Port>(shared_from_this(), next);

        std::shared_ptr<Session> previous;
        {
            std::scoped_lock current_lock(current_mutex_);
            previous = current_;
        }
        std::unique_lock previous_lock(
            previous ? previous->operation_mutex : unused_operation_mutex_);
        std::scoped_lock next_lock(next->operation_mutex);
        validate_candidate_locked(*next, *identity);
        {
            std::scoped_lock current_lock(current_mutex_);
            if (previous) {
                previous->status = Session::Status::Retired;
                previous->latest.reset();
            }
            next->status = Session::Status::Current;
            current_ = next;
            last_epoch_ = session_epoch;
        }
        if (previous) previous->wait_condition.notify_all();
        return {std::move(port), std::move(identity)};
    }

    void invalidate() noexcept
    {
        try {
            std::scoped_lock lifecycle_lock(lifecycle_mutex_);
            std::shared_ptr<Session> previous;
            {
                std::scoped_lock current_lock(current_mutex_);
                previous = current_;
            }
            if (!previous) return;
            std::scoped_lock operation_lock(previous->operation_mutex);
            {
                std::scoped_lock current_lock(current_mutex_);
                if (current_.get() == previous.get()) {
                    previous->status = Session::Status::Retired;
                    previous->latest.reset();
                    current_.reset();
                }
            }
            previous->wait_condition.notify_all();
        } catch (...) {
        }
    }

    [[nodiscard]] bool is_current(const std::shared_ptr<Session>& session) const noexcept
    {
        try {
            std::scoped_lock lock(current_mutex_);
            return current_.get() == session.get() && current_ &&
                current_->identity.get() == session->identity.get();
        } catch (...) {
            return false;
        }
    }

private:
    static void activation_allocation_checkpoint()
    {
#if defined(BAAS_CONNECTION_CO_DETECT_PORT_TESTING)
        const auto remaining = detail::activation_allocation_countdown.load(
            std::memory_order_acquire);
        if (remaining == 0) throw std::bad_alloc{};
        if (remaining > 0)
            detail::activation_allocation_countdown.fetch_sub(
                1, std::memory_order_acq_rel);
#endif
    }

    [[nodiscard]] static std::string candidate_device_id_locked(
        Session& session, const CoDetectProfile profile)
    {
        if (session.status != Session::Status::Candidate ||
            !session.backend->identity_valid()) {
            session.status = Session::Status::Tombstoned;
            throw std::invalid_argument("invalid BAAS co-detect device binding");
        }
        const auto device_id = session.backend->device_id();
        const auto profile_name = session.backend->profile_name();
        const auto interval = session.backend->screenshot_interval_ms();
        if (!session.backend->identity_valid() || !valid_device_id(device_id) ||
            profile_name != co_detect_profile_name(profile) || interval < 1U ||
            interval > 60'000U) {
            session.status = Session::Status::Tombstoned;
            throw std::invalid_argument("invalid BAAS co-detect device binding");
        }
        return device_id;
    }

    static void validate_candidate_locked(
        Session& session, const CoDetectProductionDeviceIdentity& identity)
    {
        if (session.status != Session::Status::Candidate ||
            !session.backend->identity_valid() ||
            session.backend->device_id() != identity.device_id ||
            session.backend->profile_name() != co_detect_profile_name(identity.profile)) {
            session.status = Session::Status::Tombstoned;
            throw std::invalid_argument(
                "BAAS co-detect binding changed before publication");
        }
        const auto interval = session.backend->screenshot_interval_ms();
        if (interval < 1U || interval > 60'000U ||
            !session.backend->identity_valid()) {
            session.status = Session::Status::Tombstoned;
            throw std::invalid_argument(
                "BAAS co-detect binding changed before publication");
        }
    }

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex current_mutex_;
    std::mutex unused_operation_mutex_;
    std::shared_ptr<Session> current_;
    std::uint64_t last_epoch_{};
};

BAASConnectionCoDetectOwner::BAASConnectionCoDetectOwner()
    : state_(std::make_shared<State>()) {}

BAASConnectionCoDetectOwner::~BAASConnectionCoDetectOwner()
{
    invalidate();
}

BAASConnectionCoDetectBinding BAASConnectionCoDetectOwner::activate(
    std::shared_ptr<BAASConnectionCoDetectBackend> backend,
    const CoDetectProfile profile,
    const std::uint64_t session_epoch,
    const bool android)
{
    return state_->activate(std::move(backend), profile, session_epoch, android);
}

void BAASConnectionCoDetectOwner::invalidate() noexcept
{
    state_->invalidate();
}

}  // namespace baas::runtime::procedure
