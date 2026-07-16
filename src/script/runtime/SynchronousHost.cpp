#include "script/runtime/SynchronousHost.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace baas::script::runtime {

class HostHandleCapability final {
public:
    explicit HostHandleCapability(std::function<void()> on_revoke)
        : on_revoke_(std::move(on_revoke)) {}
    ~HostHandleCapability() { revoke(); }

    [[nodiscard]] bool active() const noexcept
    {
        const std::scoped_lock lock(mutex_);
        return active_;
    }

    void revoke() noexcept
    {
        std::function<void()> callback;
        {
            const std::scoped_lock lock(mutex_);
            if (!active_) return;
            active_ = false;
            callback = std::move(on_revoke_);
        }
        try { if (callback) callback(); } catch (...) {}
    }

    void consume() noexcept
    {
        const std::scoped_lock lock(mutex_);
        active_ = false;
        on_revoke_ = {};
    }

private:
    mutable std::mutex mutex_;
    bool active_{true};
    std::function<void()> on_revoke_;
};

bool HostHandleValue::usable() const noexcept
{
    return !capability_ || capability_->active();
}

void HostHandleValue::require_usable() const
{
    if (!usable()) throw std::logic_error("Host handle capability is no longer valid");
}

HostHandleTypeId HostHandleValue::type_id() const
{
    require_usable();
    return metadata_.type_id;
}

std::uint64_t HostHandleValue::handle_id() const
{
    require_usable();
    return metadata_.handle_id;
}

std::uint64_t HostHandleValue::adapter_id() const
{
    require_usable();
    return metadata_.adapter_id;
}

std::uint64_t HostHandleValue::generation() const
{
    require_usable();
    return metadata_.generation;
}

std::uint64_t HostHandleValue::context_id() const
{
    require_usable();
    return metadata_.context_id;
}

std::uint64_t HostHandleValue::snapshot_id() const
{
    require_usable();
    return metadata_.snapshot_id;
}

std::size_t HostHandleValue::external_bytes() const
{
    require_usable();
    return metadata_.external_bytes;
}

void HostHandleValue::revoke_callback_borrow() const noexcept
{
    if (transfer_kind_ == HostHandleTransferKind::BorrowedReference && capability_)
        capability_->revoke();
}

void HostHandleValue::consume() const noexcept
{
    if (capability_) capability_->consume();
}

namespace {

std::atomic<std::uint64_t> next_handle_authentication_secret{1};

[[nodiscard]] std::uint64_t mix64(std::uint64_t value) noexcept
{
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

[[nodiscard]] std::uint64_t authenticate_handle(
    const std::uint64_t secret, const HostHandleMetadata& metadata) noexcept
{
    auto value = mix64(secret ^ metadata.handle_id);
    value = mix64(value ^ metadata.adapter_id);
    value = mix64(value ^ static_cast<std::uint64_t>(metadata.type_id));
    value = mix64(value ^ metadata.generation);
    value = mix64(value ^ metadata.context_id);
    value = mix64(value ^ metadata.snapshot_id);
    value = mix64(value ^ static_cast<std::uint64_t>(metadata.external_bytes));
    return value == 0 ? UINT64_C(0xa5a5a5a5a5a5a5a5) : value;
}

[[nodiscard]] bool valid_enum(const HostErrorCode code) noexcept
{
    const auto raw = static_cast<unsigned>(code);
    return raw >= 1 && raw <= 16;
}

[[nodiscard]] bool valid_effect(const HostEffectState state) noexcept
{
    return state == HostEffectState::NotStarted || state == HostEffectState::Committed ||
        state == HostEffectState::Unknown;
}

[[nodiscard]] bool valid_value_type(const HostValueType type) noexcept
{
    return type >= HostValueType::Null && type <= HostValueType::Bytes;
}

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept
{
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        std::size_t width{};
        std::uint32_t scalar{};
        if (first <= 0x7F) { width = 1; scalar = first; }
        else if (first >= 0xC2 && first <= 0xDF) { width = 2; scalar = first & 0x1FU; }
        else if (first >= 0xE0 && first <= 0xEF) { width = 3; scalar = first & 0x0FU; }
        else if (first >= 0xF0 && first <= 0xF4) { width = 4; scalar = first & 0x07U; }
        else return false;
        if (width > value.size() - offset) return false;
        for (std::size_t index = 1; index < width; ++index) {
            const auto next = static_cast<unsigned char>(value[offset + index]);
            if ((next & 0xC0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (next & 0x3FU);
        }
        if ((width == 2 && scalar < 0x80U) || (width == 3 && scalar < 0x800U) ||
            (width == 4 && scalar < 0x10000U) || scalar > 0x10FFFFU ||
            (scalar >= 0xD800U && scalar <= 0xDFFFU)) return false;
        offset += width;
    }
    return true;
}

[[nodiscard]] bool valid_identifier(const std::string_view value) noexcept
{
    if (value.empty()) return false;
    for (const unsigned char character : value) {
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' ||
              character == '_' || character == '-')) return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string_view> detail_string(
    const HostError& error, const std::string_view key) noexcept
{
    if (!error.details || error.details->kind() != JsonKind::Object) return std::nullopt;
    const auto& entries = std::get<JsonObject>(error.details->value());
    for (const auto& [name, value] : entries) {
        if (name == key && value.kind() == JsonKind::String)
            return std::get<std::string>(value.value());
    }
    return std::nullopt;
}

[[nodiscard]] bool validate_json(const JsonValue& value, const JsonBridgeLimits limits)
{
    try {
        Heap heap;
        (void)json_to_heap_value(heap, value, limits);
        return true;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool validate_host_value(
    const HostValue& value, const HostValueType expected,
    const JsonBridgeLimits limits)
{
    if (expected == HostValueType::OrderedStringJsonMap) {
        return value.type() == HostValueType::Json &&
            std::get<JsonValue>(value.storage()).kind() == JsonKind::Object &&
            validate_json(std::get<JsonValue>(value.storage()), limits);
    }
    if (value.type() != expected) return false;
    if (is_host_handle_type(expected)) {
        const auto& handle = std::get<HostHandleValue>(value.storage());
        return handle.usable() && handle.type_id() == host_handle_type(expected) &&
            handle.transfer_kind() == HostHandleTransferKind::ProducedGrant;
    }
    if (expected == HostValueType::Float)
        return std::isfinite(std::get<double>(value.storage()));
    if (expected == HostValueType::String)
        return std::get<std::string>(value.storage()).size() <= limits.max_string_bytes &&
            valid_utf8(std::get<std::string>(value.storage()));
    if (expected == HostValueType::Json)
        return validate_json(std::get<JsonValue>(value.storage()), limits);
    return true;
}

}  // namespace

struct HostReleaseDispatcher::Impl {
    struct AtomicStats {
        std::atomic<std::size_t> issued{};
        std::atomic<std::size_t> borrowed{};
        std::atomic<std::size_t> released{};
        std::atomic<std::size_t> retried{};
        std::atomic<std::size_t> rejected_records{};
    };
    struct LiveHandle {
        enum class ReleaseState : std::uint8_t {
            Live,
            ReleaseQueued,
            ReleaseInProgress,
            NativeReleasedAwaitingAck,
        };
        HostHandleMetadata metadata;
        std::size_t borrows{};
        bool published{};
        ReleaseState release_state{ReleaseState::Live};
        std::uint64_t callback_scope{};
        std::weak_ptr<HostHandleCapability> capability;
        std::optional<Heap::ExternalReservation> reservation;
    };

    struct Slot {
        std::uint64_t generation{1};
        std::optional<LiveHandle> live;
        bool reserved{};
        bool retired{};
        bool unpublished_enqueued{};
        std::size_t next_free{std::numeric_limits<std::size_t>::max()};
        std::size_t next_unpublished{std::numeric_limits<std::size_t>::max()};
    };

    struct SharedState {
        static constexpr auto no_index = std::numeric_limits<std::size_t>::max();

        std::mutex mutex;
        std::vector<Slot> slots;
        std::vector<std::size_t> active_scope_slots;
        std::size_t free_head{no_index};
        std::size_t unpublished_head{no_index};
        std::size_t unpublished_tail{no_index};
        std::size_t unpublished_count{};
        std::size_t published_count{};
        std::size_t live_count{};
        std::size_t reserved_count{};

        void ensure_slot_capacity()
        {
            const auto required = slots.size() + 1;
            if (required <= slots.capacity() &&
                required <= active_scope_slots.capacity())
                return;
            const auto current = std::max(slots.capacity(),
                                          active_scope_slots.capacity());
            const auto target = current > (no_index - 1) / 2
                ? required : std::max(required, std::max<std::size_t>(1, current * 2));
            slots.reserve(target);
            active_scope_slots.reserve(target);
        }

        void ensure_scope_append_capacity()
        {
            const auto required = active_scope_slots.size() + 1;
            if (required <= active_scope_slots.capacity()) return;
            const auto current = active_scope_slots.capacity();
            const auto target = current > (no_index - 1) / 2
                ? required : std::max(required, std::max<std::size_t>(1, current * 2));
            active_scope_slots.reserve(target);
        }

        [[nodiscard]] std::size_t acquire_slot()
        {
            if (free_head != no_index) {
                const auto result = free_head;
                auto& slot = slots[result];
                free_head = slot.next_free;
                slot.next_free = no_index;
                return result;
            }
            ensure_slot_capacity();
            slots.push_back({});
            return slots.size() - 1;
        }

        void recycle_slot(const std::size_t index) noexcept
        {
            auto& slot = slots[index];
            if (slot.retired) return;
            slot.next_free = free_head;
            free_head = index;
        }

        void enqueue_unpublished(const std::size_t index) noexcept
        {
            auto& slot = slots[index];
            if (slot.unpublished_enqueued) return;
            slot.unpublished_enqueued = true;
            slot.next_unpublished = no_index;
            if (unpublished_tail == no_index)
                unpublished_head = index;
            else
                slots[unpublished_tail].next_unpublished = index;
            unpublished_tail = index;
            ++unpublished_count;
        }

        [[nodiscard]] std::size_t dequeue_unpublished() noexcept
        {
            if (unpublished_head == no_index) return no_index;
            const auto result = unpublished_head;
            auto& slot = slots[result];
            unpublished_head = slot.next_unpublished;
            if (unpublished_head == no_index) unpublished_tail = no_index;
            slot.next_unpublished = no_index;
            slot.unpublished_enqueued = false;
            --unpublished_count;
            return result;
        }

        bool transition_unpublished_to_queued(const std::size_t index) noexcept
        {
            if (index >= slots.size()) return false;
            auto& live = slots[index].live;
            if (!live || live->published ||
                live->release_state != LiveHandle::ReleaseState::Live)
                return false;
            live->release_state = LiveHandle::ReleaseState::ReleaseQueued;
            enqueue_unpublished(index);
            return true;
        }

        void retry_unpublished(const std::size_t index) noexcept
        {
            if (index >= slots.size()) return;
            auto& live = slots[index].live;
            if (!live || live->published) return;
            live->release_state = LiveHandle::ReleaseState::ReleaseQueued;
            enqueue_unpublished(index);
        }

        void retire_live(const std::size_t index) noexcept
        {
            auto& slot = slots[index];
            if (!slot.live) return;
            if (slot.unpublished_enqueued) {
                // Retirement only occurs after dequeue. Keeping this guard
                // makes a violated invariant fail closed instead of linking a
                // recycled slot into the pending queue.
                return;
            }
            if (slot.live->published) --published_count;
            slot.live.reset();
            --live_count;
            if (slot.generation != std::numeric_limits<std::uint64_t>::max()) {
                ++slot.generation;
                recycle_slot(index);
            } else {
                slot.retired = true;
            }
        }
    };

    struct PendingReservation {
        std::shared_ptr<SharedState> state;
        std::optional<Heap::ExternalReservation> external;
        std::size_t slot{};
        std::uint64_t generation{};
        std::uint64_t dispatcher_secret{};
        HostHandleTypeId type_id{HostHandleTypeId::Invalid};
        std::uint64_t adapter_id{};
        std::shared_ptr<HostHandleCapability> capability;
        bool owns_slot{};
        bool adopted{};

        ~PendingReservation()
        {
            if (!owns_slot || adopted) return;
            const std::scoped_lock lock(state->mutex);
            if (slot < state->slots.size() &&
                state->slots[slot].generation == generation &&
                !state->slots[slot].live && state->slots[slot].reserved) {
                state->slots[slot].reserved = false;
                --state->reserved_count;
                state->recycle_slot(slot);
            }
        }
    };

    explicit Impl(
        const std::uint64_t requested_snapshot,
        std::vector<HostReleaseAdapter> requested_adapters)
        : snapshot_id(requested_snapshot), adapters(std::move(requested_adapters)),
          state(std::make_shared<SharedState>())
    {
        if (snapshot_id == 0)
            throw std::invalid_argument("Host handle snapshot id must be non-zero");
        std::sort(adapters.begin(), adapters.end(), [](const auto& left, const auto& right) {
            return left.adapter_id < right.adapter_id;
        });
        for (std::size_t index = 0; index < adapters.size(); ++index) {
            if (adapters[index].adapter_id == 0 || !adapters[index].release)
                throw std::invalid_argument("Host release adapter is invalid");
            if (index != 0 && adapters[index - 1].adapter_id == adapters[index].adapter_id)
                throw std::invalid_argument("Host release adapter id is duplicated");
        }
        secret = mix64(next_handle_authentication_secret.fetch_add(
            1, std::memory_order_relaxed));
        if (secret == 0) secret = 1;
    }

    [[nodiscard]] const HostReleaseAdapter* adapter(
        const std::uint64_t adapter_id) const noexcept
    {
        const auto found = std::lower_bound(
            adapters.begin(), adapters.end(), adapter_id,
            [](const HostReleaseAdapter& candidate, const std::uint64_t id) {
                return candidate.adapter_id < id;
            });
        return found != adapters.end() && found->adapter_id == adapter_id
            ? &*found : nullptr;
    }

    [[nodiscard]] static bool same_native_key(
        const HostHandleMetadata& left, const HostHandleMetadata& right) noexcept
    {
        return left.handle_id == right.handle_id &&
            left.adapter_id == right.adapter_id &&
            left.external_bytes == right.external_bytes &&
            left.type_id == right.type_id &&
            left.generation == right.generation &&
            left.context_id == right.context_id &&
            left.snapshot_id == right.snapshot_id &&
            left.authentication == right.authentication;
    }

    [[nodiscard]] static HostHandleMetadata metadata_for(
        const HostReleaseRecord& record) noexcept
    {
        return {record.handle_id, record.adapter_id, record.external_bytes, true,
                record.type_id, record.generation, record.context_id,
                record.snapshot_id, record.authentication};
    }

    [[nodiscard]] LiveHandle* find_live_locked(
        const HostHandleMetadata& metadata) noexcept
    {
        if (metadata.handle_id == 0 || metadata.handle_id > state->slots.size())
            return nullptr;
        auto& slot = state->slots[static_cast<std::size_t>(metadata.handle_id - 1)];
        if (!slot.live || slot.generation != metadata.generation ||
            !same_native_key(slot.live->metadata, metadata))
            return nullptr;
        return &*slot.live;
    }

    std::uint64_t snapshot_id{};
    std::uint64_t context_id{};
    std::uint64_t secret{};
    std::vector<HostReleaseAdapter> adapters;
    std::shared_ptr<SharedState> state;
    AtomicStats stats;
    mutable std::mutex control_mutex;
    std::thread::id owner_thread{};
    Heap* heap{};
    bool attached{};
    bool accepting{true};
    std::uint64_t next_callback_scope{1};
    std::uint64_t active_callback_scope{};
    bool teardown_complete{};
    std::optional<Heap::DetachedHostReleases> detached_releases;
    bool context_detached{};
};

HostReleaseDispatcher::HostReleaseDispatcher(
    const std::uint64_t snapshot_id, std::vector<HostReleaseAdapter> adapters)
    : impl_(new Impl(snapshot_id, std::move(adapters)))
{
}

HostReleaseDispatcher::~HostReleaseDispatcher()
{
    if (!destruction_safe()) std::terminate();
    delete impl_;
}

void HostReleaseDispatcher::attach_context(Heap& heap)
{
    if (impl_->attached)
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle dispatcher already owns a context");
    impl_->context_id = heap.identity();
    impl_->heap = &heap;
    impl_->owner_thread = std::this_thread::get_id();
    impl_->attached = true;
}

HostHandleReservation HostReleaseDispatcher::reserve(
    const HostHandleTypeId type_id, const std::uint64_t adapter_id,
    const std::size_t external_bytes)
{
    if (!impl_->attached || !impl_->accepting ||
        std::this_thread::get_id() != impl_->owner_thread)
        throw RuntimeError(RuntimeErrorCode::HeapTornDown,
                           "Host handle context is unavailable");
    if (type_id < HostHandleTypeId::Resource || type_id > HostHandleTypeId::Device ||
        !impl_->adapter(adapter_id))
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle type or adapter is invalid");

    std::shared_ptr<Impl::PendingReservation> pending;
    try {
        pending = std::make_shared<Impl::PendingReservation>();
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                           "Host handle reservation allocation failed");
    }
    pending->state = impl_->state;
    pending->dispatcher_secret = impl_->secret;
    pending->type_id = type_id;
    pending->adapter_id = adapter_id;
    {
        const std::scoped_lock lock(impl_->state->mutex);
        std::size_t slot_index{};
        try { slot_index = impl_->state->acquire_slot(); }
        catch (const std::bad_alloc&) {
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                               "Host handle table allocation failed");
        }
        auto& slot = impl_->state->slots[slot_index];
        slot.reserved = true;
        ++impl_->state->reserved_count;
        pending->slot = slot_index;
        pending->generation = slot.generation;
        pending->owns_slot = true;
    }
    pending->external.emplace(impl_->heap->reserve_host_external(external_bytes));
    const auto state = impl_->state;
    const auto slot = pending->slot;
    const auto generation = pending->generation;
    try {
        pending->capability = std::make_shared<HostHandleCapability>(
            [state, slot, generation] {
                const std::scoped_lock lock(state->mutex);
                if (slot >= state->slots.size()) return;
                auto& live = state->slots[slot].live;
                if (live && live->metadata.generation == generation &&
                    !live->published && live->release_state ==
                        Impl::LiveHandle::ReleaseState::Live)
                    (void)state->transition_unpublished_to_queued(slot);
            });
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                           "Host handle capability allocation failed");
    }
    return HostHandleReservation(std::move(pending));
}

HostHandleValue HostReleaseDispatcher::adopt(HostHandleReservation&& reservation)
{
    if (!impl_->attached || !impl_->accepting ||
        std::this_thread::get_id() != impl_->owner_thread)
        throw RuntimeError(RuntimeErrorCode::HeapTornDown,
                           "Host handle context is unavailable");
    if (!reservation.control_)
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle reservation is absent");
    auto pending = std::static_pointer_cast<Impl::PendingReservation>(
        reservation.control_);
    if (pending->dispatcher_secret != impl_->secret || pending->adopted ||
        !pending->external || !pending->external->active())
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle reservation is foreign or consumed");
    HostHandleMetadata metadata;
    metadata.handle_id = static_cast<std::uint64_t>(pending->slot + 1);
    metadata.adapter_id = pending->adapter_id;
    metadata.external_bytes = pending->external->bytes();
    metadata.type_id = pending->type_id;
    metadata.generation = pending->generation;
    metadata.context_id = impl_->context_id;
    metadata.snapshot_id = impl_->snapshot_id;
    metadata.authentication = authenticate_handle(impl_->secret, metadata);
    {
        const std::scoped_lock lock(impl_->state->mutex);
        if (pending->slot >= impl_->state->slots.size())
            throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                               "Host handle reservation slot is stale");
        auto& slot = impl_->state->slots[pending->slot];
        if (!slot.reserved || slot.live || slot.generation != pending->generation)
            throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                               "Host handle reservation was invalidated");
        if (impl_->active_callback_scope != 0) {
            try { impl_->state->ensure_scope_append_capacity(); }
            catch (const std::bad_alloc&) {
                throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                                   "Host callback scope index allocation failed");
            }
        }
        slot.live.emplace(Impl::LiveHandle{
            metadata, 0, false, Impl::LiveHandle::ReleaseState::Live,
            impl_->active_callback_scope, pending->capability,
            std::move(pending->external)});
        slot.reserved = false;
        --impl_->state->reserved_count;
        ++impl_->state->live_count;
        if (impl_->active_callback_scope != 0)
            impl_->state->active_scope_slots.push_back(pending->slot);
    }
    pending->adopted = true;
    reservation.control_.reset();
    ++impl_->stats.issued;
    return HostHandleValue(
        metadata, HostHandleTransferKind::ProducedGrant,
        std::move(pending->capability));
}

HostHandleValue HostReleaseDispatcher::borrow(
    const Heap& heap, const Value value, const HostHandleTypeId expected_type)
{
    if (!impl_->attached || std::this_thread::get_id() != impl_->owner_thread ||
        heap.identity() != impl_->context_id)
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle belongs to another execution context");
    if (heap.kind(value) != ValueKind::HostHandle ||
        expected_type < HostHandleTypeId::Resource ||
        expected_type > HostHandleTypeId::Device)
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host argument is not the required typed handle");
    const auto metadata = heap.host_handle_metadata(value.as_heap_ref());
    if (metadata.closed)
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle is closed");
    if (metadata.type_id != expected_type || metadata.context_id != impl_->context_id ||
        metadata.snapshot_id != impl_->snapshot_id ||
        metadata.authentication != authenticate_handle(impl_->secret, metadata))
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle identity is invalid");

    const auto state = impl_->state;
    const auto slot_index = static_cast<std::size_t>(metadata.handle_id - 1);
    std::shared_ptr<HostHandleCapability> capability;
    try {
        capability = std::make_shared<HostHandleCapability>(
            [state, slot_index, generation = metadata.generation] {
                const std::scoped_lock lock(state->mutex);
                if (slot_index >= state->slots.size()) return;
                auto& live = state->slots[slot_index].live;
                if (live && live->metadata.generation == generation && live->borrows != 0)
                    --live->borrows;
            });
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                           "Host handle borrow allocation failed");
    }
    {
        const std::scoped_lock lock(impl_->state->mutex);
        auto* live = impl_->find_live_locked(metadata);
        if (!live || !live->published)
            throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                               "Host handle is stale or forged");
        ++live->borrows;
    }
    ++impl_->stats.borrowed;
    return HostHandleValue(
        metadata, HostHandleTransferKind::BorrowedReference,
        std::move(capability));
}

Value HostReleaseDispatcher::publish(
    Heap& heap, const HostHandleValue& value, const HostHandleTypeId expected_type)
{
    if (!impl_->attached || !impl_->accepting ||
        std::this_thread::get_id() != impl_->owner_thread ||
        heap.identity() != impl_->context_id)
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle result belongs to another execution context");
    const auto metadata = value.metadata_;
    if (value.transfer_kind_ != HostHandleTransferKind::ProducedGrant ||
        !value.usable() || metadata.closed || metadata.type_id != expected_type ||
        metadata.context_id != impl_->context_id ||
        metadata.snapshot_id != impl_->snapshot_id ||
        metadata.authentication != authenticate_handle(impl_->secret, metadata))
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host handle result identity is invalid");
    std::size_t required_release_capacity{};
    {
        const std::scoped_lock lock(impl_->state->mutex);
        if (impl_->state->published_count >=
                heap.limits().max_pending_release_records)
            throw RuntimeError(RuntimeErrorCode::ReleaseQueueLimitExceeded,
                               "typed Host handle release capacity is exhausted");
        required_release_capacity = impl_->state->published_count + 1;
        auto* live = impl_->find_live_locked(metadata);
        if (!live || live->published)
            throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                               "Host handle result is stale, forged, or already published");
    }
    heap.ensure_host_release_capacity(required_release_capacity);
    std::optional<Heap::ExternalReservation> external;
    {
        const std::scoped_lock lock(impl_->state->mutex);
        auto* live = impl_->find_live_locked(metadata);
        if (!live || live->published || !live->reservation)
            throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                               "Host handle result lost its external reservation");
        external.emplace(std::move(*live->reservation));
        live->reservation.reset();
    }
    Value published;
    try {
        published = heap.allocate_host_handle(metadata, std::move(*external));
    } catch (...) {
        const std::scoped_lock lock(impl_->state->mutex);
        auto* live = impl_->find_live_locked(metadata);
        if (live && !live->reservation && external->active())
            live->reservation.emplace(std::move(*external));
        throw;
    }
    {
        const std::scoped_lock lock(impl_->state->mutex);
        auto* live = impl_->find_live_locked(metadata);
        if (!live || live->published)
            throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                               "Host handle publication lost ownership");
        live->published = true;
        ++impl_->state->published_count;
    }
    value.consume();
    return published;
}

void HostReleaseDispatcher::abandon(const HostHandleValue& value) noexcept
{
    try {
        if (value.transfer_kind_ != HostHandleTransferKind::ProducedGrant) return;
        const auto metadata = value.metadata_;
        {
            const std::scoped_lock lock(impl_->state->mutex);
            auto* live = impl_->find_live_locked(metadata);
            if (live && !live->published && live->release_state ==
                    Impl::LiveHandle::ReleaseState::Live) {
                const auto index = static_cast<std::size_t>(metadata.handle_id - 1);
                (void)impl_->state->transition_unpublished_to_queued(index);
            }
        }
        if (value.capability_) value.capability_->revoke();
    } catch (...) {
    }
}

std::uint64_t HostReleaseDispatcher::begin_callback_scope()
{
    if (!impl_->attached || !impl_->accepting || impl_->active_callback_scope != 0 ||
        std::this_thread::get_id() != impl_->owner_thread)
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "Host callback scope is unavailable");
    auto result = impl_->next_callback_scope++;
    if (result == 0) result = impl_->next_callback_scope++;
    {
        const std::scoped_lock lock(impl_->state->mutex);
        impl_->state->active_scope_slots.clear();
    }
    impl_->active_callback_scope = result;
    return result;
}

void HostReleaseDispatcher::finish_callback_scope(
    const std::uint64_t scope_id,
    const HostHandleValue* const retained_grant) noexcept
{
    try {
        HostHandleMetadata retained_metadata;
        const bool has_retained = retained_grant && retained_grant->usable() &&
            retained_grant->transfer_kind_ ==
                HostHandleTransferKind::ProducedGrant;
        if (has_retained) retained_metadata = retained_grant->metadata_;
        {
            const std::scoped_lock lock(impl_->state->mutex);
            if (scope_id == 0 || impl_->active_callback_scope != scope_id) return;
            impl_->active_callback_scope = 0;
        }
        for (;;) {
            std::shared_ptr<HostHandleCapability> capability;
            {
                const std::scoped_lock lock(impl_->state->mutex);
                if (impl_->state->active_scope_slots.empty()) break;
                const auto index = impl_->state->active_scope_slots.back();
                impl_->state->active_scope_slots.pop_back();
                if (index >= impl_->state->slots.size()) continue;
                auto& slot = impl_->state->slots[index];
                if (!slot.live || slot.live->published ||
                    slot.live->callback_scope != scope_id)
                    continue;
                if (has_retained && Impl::same_native_key(
                        slot.live->metadata, retained_metadata))
                    continue;
                if (slot.live->release_state ==
                        Impl::LiveHandle::ReleaseState::Live)
                    (void)impl_->state->transition_unpublished_to_queued(index);
                capability = slot.live->capability.lock();
            }
            if (capability) capability->revoke();
        }
    } catch (...) {
        impl_->active_callback_scope = 0;
    }
}

bool HostReleaseDispatcher::dispatch_one(Heap& heap) noexcept
{
    if (!impl_->attached || std::this_thread::get_id() != impl_->owner_thread ||
        heap.identity() != impl_->context_id)
        return false;
    try {
        const auto dispatch_unpublished = [&]() noexcept -> bool {
            try {
                HostHandleMetadata metadata;
                const HostReleaseAdapter* adapter{};
                std::size_t selected_slot{};
                {
                    const std::scoped_lock lock(impl_->state->mutex);
                    selected_slot = impl_->state->dequeue_unpublished();
                    if (selected_slot != Impl::SharedState::no_index) {
                        auto& live = impl_->state->slots[selected_slot].live;
                        if (!live || live->published || live->release_state !=
                                Impl::LiveHandle::ReleaseState::ReleaseQueued) {
                            selected_slot = Impl::SharedState::no_index;
                        } else if (live->borrows != 0) {
                            impl_->state->enqueue_unpublished(selected_slot);
                            selected_slot = Impl::SharedState::no_index;
                        } else {
                            metadata = live->metadata;
                            adapter = impl_->adapter(metadata.adapter_id);
                            live->release_state =
                                Impl::LiveHandle::ReleaseState::ReleaseInProgress;
                        }
                    }
                }
                if (selected_slot == Impl::SharedState::no_index) return false;
                if (!adapter) {
                    ++impl_->stats.rejected_records;
                    const std::scoped_lock lock(impl_->state->mutex);
                    impl_->state->retry_unpublished(selected_slot);
                    return false;
                }
                bool accepted{};
                try {
                    accepted = adapter->release(HostHandleValue(
                        metadata, HostHandleTransferKind::BorrowedReference));
                } catch (...) {
                    accepted = false;
                }
                if (!accepted) {
                    ++impl_->stats.retried;
                    const std::scoped_lock lock(impl_->state->mutex);
                    impl_->state->retry_unpublished(selected_slot);
                    return false;
                }
                {
                    const std::scoped_lock lock(impl_->state->mutex);
                    auto* live = impl_->find_live_locked(metadata);
                    if (!live || live->published || live->release_state !=
                            Impl::LiveHandle::ReleaseState::ReleaseInProgress)
                        return false;
                    impl_->state->retire_live(selected_slot);
                    ++impl_->stats.released;
                }
                return true;
            } catch (...) {
                return false;
            }
        };

        const auto lease = heap.lease_host_release();
        if (!lease) return dispatch_unpublished();
        const auto metadata = Impl::metadata_for(lease->record);
        HostHandleValue handle(metadata, HostHandleTransferKind::BorrowedReference);
        const HostReleaseAdapter* adapter{};
        bool native_released{};
        bool release_in_progress{};
        bool rejected{};
        bool pinned{};
        {
            const std::scoped_lock lock(impl_->state->mutex);
            auto* live = impl_->find_live_locked(metadata);
            rejected = !live || !live->published;
            if (!rejected) {
                native_released = live->release_state ==
                    Impl::LiveHandle::ReleaseState::NativeReleasedAwaitingAck;
                release_in_progress = live->release_state ==
                    Impl::LiveHandle::ReleaseState::ReleaseInProgress;
                pinned = !native_released &&
                    (release_in_progress || live->borrows != 0);
                adapter = impl_->adapter(metadata.adapter_id);
                rejected = !adapter;
            }
        }
        if (rejected) {
            ++impl_->stats.rejected_records;
            (void)heap.defer_host_release(lease->lease_id);
            return dispatch_unpublished();
        }
        if (pinned) {
            ++impl_->stats.retried;
            (void)heap.defer_host_release(lease->lease_id);
            return dispatch_unpublished();
        }

        if (!native_released) {
            {
                const std::scoped_lock lock(impl_->state->mutex);
                if (auto* live = impl_->find_live_locked(metadata))
                    live->release_state =
                        Impl::LiveHandle::ReleaseState::ReleaseInProgress;
            }
            bool accepted{};
            try { accepted = adapter->release(handle); } catch (...) { accepted = false; }
            if (!accepted) {
                ++impl_->stats.retried;
                {
                    const std::scoped_lock lock(impl_->state->mutex);
                    if (auto* live = impl_->find_live_locked(metadata))
                        live->release_state =
                            Impl::LiveHandle::ReleaseState::ReleaseQueued;
                }
                (void)heap.defer_host_release(lease->lease_id);
                return dispatch_unpublished();
            }
            bool state_valid{};
            {
                const std::scoped_lock lock(impl_->state->mutex);
                auto* live = impl_->find_live_locked(metadata);
                state_valid = live && live->published;
                if (state_valid)
                    live->release_state =
                        Impl::LiveHandle::ReleaseState::NativeReleasedAwaitingAck;
            }
            if (!state_valid) {
                ++impl_->stats.rejected_records;
                (void)heap.defer_host_release(lease->lease_id);
                return dispatch_unpublished();
            }
        }

        if (!heap.acknowledge_host_release(lease->lease_id)) {
            ++impl_->stats.retried;
            (void)heap.defer_host_release(lease->lease_id);
            return dispatch_unpublished();
        }
        {
            const std::scoped_lock lock(impl_->state->mutex);
            const auto slot_index = static_cast<std::size_t>(metadata.handle_id - 1);
            auto* live = impl_->find_live_locked(metadata);
            if (!live || live->release_state !=
                    Impl::LiveHandle::ReleaseState::NativeReleasedAwaitingAck)
                return false;
            impl_->state->retire_live(slot_index);
            ++impl_->stats.released;
        }
        return true;
    } catch (...) {
        return false;
    }
}

void HostReleaseDispatcher::dispatch_all(Heap& heap) noexcept
{
    std::size_t attempts{};
    {
        const std::scoped_lock lock(impl_->state->mutex);
        const auto maximum = std::numeric_limits<std::size_t>::max();
        const auto published_queued = heap.pending_host_release_count();
        attempts = published_queued >
                maximum - impl_->state->unpublished_count
            ? maximum
            : published_queued + impl_->state->unpublished_count;
    }
    const auto maximum = std::numeric_limits<std::size_t>::max();
    attempts = attempts > (maximum - 2) / 2 ? maximum : attempts * 2 + 2;
    while (attempts-- != 0) (void)dispatch_one(heap);
}

void HostReleaseDispatcher::retry_detached_releases() noexcept
{
    if (!impl_->attached || std::this_thread::get_id() != impl_->owner_thread)
        return;
    try {
        std::size_t attempts{};
        {
            const std::scoped_lock lock(impl_->state->mutex);
            const auto maximum = std::numeric_limits<std::size_t>::max();
            const auto queued = impl_->state->unpublished_count;
            attempts = queued > (maximum - 2) / 2 ? maximum : queued * 2 + 2;
        }
        {
            const std::scoped_lock lock(impl_->control_mutex);
            const auto maximum = std::numeric_limits<std::size_t>::max();
            const auto detached = impl_->detached_releases
                ? impl_->detached_releases->size() : 0;
            attempts = detached > maximum - attempts ? maximum : attempts + detached;
        }
        while (attempts-- != 0) {
            bool progressed{};
            std::optional<HostReleaseLease> detached_lease;
            {
                const std::scoped_lock lock(impl_->control_mutex);
                if (impl_->detached_releases)
                    detached_lease = impl_->detached_releases->lease();
            }
            if (detached_lease) {
                const auto lease = detached_lease;
                if (lease) {
                    const auto metadata = Impl::metadata_for(lease->record);
                    const HostReleaseAdapter* adapter{};
                    bool native_released{};
                    bool release_in_progress{};
                    bool pinned{};
                    bool rejected{};
                    {
                        const std::scoped_lock lock(impl_->state->mutex);
                        auto* live = impl_->find_live_locked(metadata);
                        rejected = !live || !live->published;
                        if (!rejected) {
                            native_released = live->release_state ==
                                Impl::LiveHandle::ReleaseState::NativeReleasedAwaitingAck;
                            release_in_progress = live->release_state ==
                                Impl::LiveHandle::ReleaseState::ReleaseInProgress;
                            pinned = !native_released &&
                                (release_in_progress || live->borrows != 0);
                            adapter = impl_->adapter(metadata.adapter_id);
                            rejected = !adapter;
                        }
                    }
                    if (rejected || pinned) {
                        if (rejected) ++impl_->stats.rejected_records;
                        else ++impl_->stats.retried;
                        const std::scoped_lock lock(impl_->control_mutex);
                        (void)impl_->detached_releases->defer(lease->lease_id);
                    } else {
                        bool accepted{true};
                        if (!native_released) {
                            {
                                const std::scoped_lock lock(impl_->state->mutex);
                                if (auto* live = impl_->find_live_locked(metadata))
                                    live->release_state = Impl::LiveHandle::ReleaseState::
                                        ReleaseInProgress;
                            }
                            try {
                                accepted = adapter->release(HostHandleValue(
                                    metadata,
                                    HostHandleTransferKind::BorrowedReference));
                            } catch (...) {
                                accepted = false;
                            }
                        }
                        if (!accepted) {
                            ++impl_->stats.retried;
                            {
                                const std::scoped_lock lock(impl_->state->mutex);
                                if (auto* live = impl_->find_live_locked(metadata))
                                    live->release_state = Impl::LiveHandle::ReleaseState::
                                        ReleaseQueued;
                            }
                            const std::scoped_lock lock(impl_->control_mutex);
                            (void)impl_->detached_releases->defer(lease->lease_id);
                        } else {
                            if (!native_released) {
                                const std::scoped_lock lock(impl_->state->mutex);
                                if (auto* live = impl_->find_live_locked(metadata))
                                    live->release_state = Impl::LiveHandle::ReleaseState::
                                        NativeReleasedAwaitingAck;
                            }
                            bool acknowledged{};
                            {
                                const std::scoped_lock lock(impl_->control_mutex);
                                acknowledged = impl_->detached_releases->acknowledge(
                                    lease->lease_id);
                            }
                            if (!acknowledged) {
                                ++impl_->stats.retried;
                                const std::scoped_lock lock(impl_->control_mutex);
                                (void)impl_->detached_releases->defer(lease->lease_id);
                            } else {
                                const std::scoped_lock lock(impl_->state->mutex);
                                const auto slot_index = static_cast<std::size_t>(
                                    metadata.handle_id - 1);
                                auto* live = impl_->find_live_locked(metadata);
                                if (live && live->release_state == Impl::LiveHandle::
                                        ReleaseState::NativeReleasedAwaitingAck) {
                                    impl_->state->retire_live(slot_index);
                                    ++impl_->stats.released;
                                    progressed = true;
                                }
                            }
                        }
                    }
                }
            }

            HostHandleMetadata unpublished;
            const HostReleaseAdapter* adapter{};
            std::size_t selected{};
            {
                const std::scoped_lock lock(impl_->state->mutex);
                selected = impl_->state->dequeue_unpublished();
                if (selected != Impl::SharedState::no_index) {
                    auto& live = impl_->state->slots[selected].live;
                    if (!live || live->published || live->release_state !=
                            Impl::LiveHandle::ReleaseState::ReleaseQueued) {
                        selected = Impl::SharedState::no_index;
                    } else if (live->borrows != 0) {
                        impl_->state->enqueue_unpublished(selected);
                        selected = Impl::SharedState::no_index;
                    } else {
                        unpublished = live->metadata;
                        adapter = impl_->adapter(unpublished.adapter_id);
                        live->release_state =
                            Impl::LiveHandle::ReleaseState::ReleaseInProgress;
                    }
                }
            }
            if (selected != Impl::SharedState::no_index) {
                bool accepted{};
                try {
                    accepted = adapter && adapter->release(HostHandleValue(
                        unpublished, HostHandleTransferKind::BorrowedReference));
                } catch (...) {
                    accepted = false;
                }
                if (!accepted) {
                    ++impl_->stats.retried;
                    const std::scoped_lock lock(impl_->state->mutex);
                    impl_->state->retry_unpublished(selected);
                } else {
                    const std::scoped_lock lock(impl_->state->mutex);
                    auto* live = impl_->find_live_locked(unpublished);
                    if (live && !live->published && live->release_state ==
                            Impl::LiveHandle::ReleaseState::ReleaseInProgress) {
                        impl_->state->retire_live(selected);
                        ++impl_->stats.released;
                        progressed = true;
                    }
                }
            }
            (void)progressed;
        }
        bool slots_empty{};
        {
            const std::scoped_lock lock(impl_->state->mutex);
            slots_empty = impl_->state->live_count == 0 &&
                impl_->state->reserved_count == 0;
        }
        {
            const std::scoped_lock lock(impl_->control_mutex);
            impl_->teardown_complete = impl_->detached_releases &&
                impl_->detached_releases->empty() && slots_empty;
        }
    } catch (...) {
    }
}

bool HostReleaseDispatcher::detach_context_for_destruction(Heap& heap) noexcept
{
    if (!impl_->attached || heap.identity() != impl_->context_id)
        return false;
    {
        const std::scoped_lock lock(impl_->control_mutex);
        if (impl_->context_detached) return true;
    }
    impl_->accepting = false;
    try {
        heap.teardown_for_dispatcher();
    } catch (...) {
        return false;
    }
    for (std::size_t index = 0;; ++index) {
        std::shared_ptr<HostHandleCapability> capability;
        {
            const std::scoped_lock lock(impl_->state->mutex);
            if (index >= impl_->state->slots.size()) break;
            auto& live = impl_->state->slots[index].live;
            if (!live || live->published) continue;
            if (live->release_state == Impl::LiveHandle::ReleaseState::Live)
                (void)impl_->state->transition_unpublished_to_queued(index);
            capability = live->capability.lock();
            live->capability.reset();
        }
        if (capability) capability->revoke();
    }
    try {
        auto detached = heap.detach_host_releases();
        const std::scoped_lock lock(impl_->control_mutex);
        impl_->detached_releases.emplace(std::move(detached));
        impl_->context_detached = true;
    } catch (...) {
        return false;
    }
    impl_->heap = nullptr;
    return true;
}

bool HostReleaseDispatcher::teardown(Heap& heap) noexcept
{
    if (!impl_->attached || std::this_thread::get_id() != impl_->owner_thread ||
        heap.identity() != impl_->context_id)
        return false;
    if (!detach_context_for_destruction(heap)) return false;
    retry_detached_releases();
    const std::scoped_lock lock(impl_->control_mutex);
    return impl_->teardown_complete;
}

std::uint64_t HostReleaseDispatcher::context_id() const noexcept
{
    return impl_->context_id;
}

std::uint64_t HostReleaseDispatcher::snapshot_id() const noexcept
{
    return impl_->snapshot_id;
}

HostReleaseDispatcherStats HostReleaseDispatcher::stats() const noexcept
{
    HostReleaseDispatcherStats result;
    result.issued = impl_->stats.issued.load(std::memory_order_relaxed);
    result.borrowed = impl_->stats.borrowed.load(std::memory_order_relaxed);
    result.released = impl_->stats.released.load(std::memory_order_relaxed);
    result.retried = impl_->stats.retried.load(std::memory_order_relaxed);
    result.rejected_records = impl_->stats.rejected_records.load(
        std::memory_order_relaxed);
    bool detached{};
    {
        const std::scoped_lock lock(impl_->control_mutex);
        detached = impl_->detached_releases.has_value();
        if (detached) {
            result.pending_releases = impl_->detached_releases->size();
            result.pending_external_bytes =
                impl_->detached_releases->external_bytes();
        }
        result.teardown_complete = impl_->teardown_complete;
    }
    {
        const std::scoped_lock lock(impl_->state->mutex);
        for (const auto& slot : impl_->state->slots) {
            if (!slot.live || (detached && slot.live->published)) continue;
            if (result.pending_releases != std::numeric_limits<std::size_t>::max())
                ++result.pending_releases;
            if (slot.live->metadata.external_bytes >
                std::numeric_limits<std::size_t>::max() - result.pending_external_bytes)
                result.pending_external_bytes = std::numeric_limits<std::size_t>::max();
            else
                result.pending_external_bytes += slot.live->metadata.external_bytes;
        }
    }
    return result;
}

bool HostReleaseDispatcher::destruction_safe() const noexcept
{
    {
        const std::scoped_lock lock(impl_->control_mutex);
        if (impl_->detached_releases && !impl_->detached_releases->empty())
            return false;
    }
    const std::scoped_lock lock(impl_->state->mutex);
    return impl_->state->live_count == 0 && impl_->state->reserved_count == 0;
}

std::string_view host_error_code_name(const HostErrorCode code) noexcept
{
    using enum HostErrorCode;
    switch (code) {
        case CapabilityDenied: return "HOST001_CAPABILITY_DENIED";
        case InvalidArgument: return "HOST002_INVALID_ARGUMENT";
        case Cancelled: return "HOST003_CANCELLED";
        case DeadlineExceeded: return "HOST004_DEADLINE_EXCEEDED";
        case BudgetExceeded: return "HOST005_BUDGET_EXCEEDED";
        case Unavailable: return "HOST006_UNAVAILABLE";
        case IoError: return "HOST007_IO_ERROR";
        case DeviceDisconnected: return "HOST008_DEVICE_DISCONNECTED";
        case ConfigConflict: return "HOST009_CONFIG_CONFLICT";
        case ResourceNotFound: return "HOST010_RESOURCE_NOT_FOUND";
        case ModelUnavailable: return "HOST011_MODEL_UNAVAILABLE";
        case PolicyDenied: return "HOST012_POLICY_DENIED";
        case ProtocolError: return "HOST013_PROTOCOL_ERROR";
        case Internal: return "HOST014_INTERNAL";
        case HandleClosed: return "HOST015_HANDLE_CLOSED";
        case Backpressure: return "HOST016_BACKPRESSURE";
    }
    return "HOST014_INTERNAL";
}

HostValueType HostValue::type() const noexcept
{
    switch (storage_.index()) {
        case 0: return HostValueType::Null;
        case 1: return HostValueType::Boolean;
        case 2: return HostValueType::Integer;
        case 3: return HostValueType::Float;
        case 4: return HostValueType::String;
        case 5: return HostValueType::Json;
        case 6: {
            switch (std::get<HostHandleValue>(storage_).metadata_.type_id) {
                case HostHandleTypeId::Resource: return HostValueType::HostResource;
                case HostHandleTypeId::Image: return HostValueType::HostImage;
                case HostHandleTypeId::OcrModel: return HostValueType::HostOcrModel;
                case HostHandleTypeId::Device: return HostValueType::HostDevice;
                case HostHandleTypeId::Invalid: break;
            }
            return HostValueType::HostResource;
        }
        case 7: return HostValueType::Bytes;
        default: return HostValueType::Null;
    }
}

bool is_host_handle_type(const HostValueType type) noexcept
{
    return type >= HostValueType::HostResource && type <= HostValueType::HostDevice;
}

HostValueType host_value_type(const HostHandleTypeId type)
{
    switch (type) {
        case HostHandleTypeId::Resource: return HostValueType::HostResource;
        case HostHandleTypeId::Image: return HostValueType::HostImage;
        case HostHandleTypeId::OcrModel: return HostValueType::HostOcrModel;
        case HostHandleTypeId::Device: return HostValueType::HostDevice;
        case HostHandleTypeId::Invalid: break;
    }
    throw RuntimeError(RuntimeErrorCode::TypeMismatch, "invalid Host handle type id");
}

HostHandleTypeId host_handle_type(const HostValueType type)
{
    switch (type) {
        case HostValueType::HostResource: return HostHandleTypeId::Resource;
        case HostValueType::HostImage: return HostHandleTypeId::Image;
        case HostValueType::HostOcrModel: return HostHandleTypeId::OcrModel;
        case HostValueType::HostDevice: return HostHandleTypeId::Device;
        default: break;
    }
    throw RuntimeError(RuntimeErrorCode::TypeMismatch, "Host value type is not host<T>");
}

HostResult HostResult::success(HostValue value)
{
    return HostResult(std::variant<HostValue, HostError, BoundaryFailure>(
        std::in_place_index<0>, std::move(value)));
}

HostResult HostResult::failure(HostError error) noexcept
{
    return HostResult(std::variant<HostValue, HostError, BoundaryFailure>(
        std::in_place_index<1>, std::move(error)));
}

HostResult HostResult::boundary_failure(const BoundaryFailure failure) noexcept
{
    return HostResult(std::variant<HostValue, HostError, BoundaryFailure>(
        std::in_place_index<2>, failure));
}

bool HostResult::ok() const noexcept { return state_.index() == 0; }
bool HostResult::has_error() const noexcept { return state_.index() == 1; }
const HostValue& HostResult::value() const { return std::get<HostValue>(state_); }
const HostError& HostResult::error() const { return std::get<HostError>(state_); }
HostResult::BoundaryFailure HostResult::boundary_failure() const noexcept
{
    return state_.index() == 2 ? std::get<BoundaryFailure>(state_) : BoundaryFailure::None;
}

SynchronousNativeBindingSet::SynchronousNativeBindingSet(
    std::vector<SynchronousNativeBinding> bindings, const SynchronousHostLimits limits)
    : limits_(limits)
{
    if (limits.max_bindings == 0 || limits.max_parameters_per_binding == 0 ||
        limits.max_total_parameters == 0 || limits.max_string_bytes == 0 ||
        limits.max_total_string_bytes == 0 || limits.max_validation_work == 0 ||
        limits.max_safe_message_bytes == 0)
        throw HostBindingError(HostBindingErrorCode::InvalidLimits, "synchronous Host limits must be non-zero");
    if (bindings.size() > limits.max_bindings)
        throw HostBindingError(HostBindingErrorCode::BindingLimitExceeded, "synchronous Host binding limit exceeded");

    std::size_t total_parameters{};
    std::size_t total_strings{};
    std::size_t work{};
    for (const auto& binding : bindings) {
        if (++work > limits.max_validation_work)
            throw HostBindingError(HostBindingErrorCode::WorkLimitExceeded, "synchronous Host validation work exceeded");
        if (!valid_identifier(binding.binding_id))
            throw HostBindingError(HostBindingErrorCode::InvalidBindingId, "invalid synchronous Host binding id");
        if (!binding.callback)
            throw HostBindingError(HostBindingErrorCode::MissingCallback, "synchronous Host callback is absent");
        if (binding.contract.execution != HostExecutionMode::ThreadSafe)
            throw HostBindingError(HostBindingErrorCode::UnsupportedExecutionMode, "synchronous Host supports thread_safe bindings only");
        if (binding.contract.cancellation != HostCancellationMode::Preflight &&
            binding.contract.cancellation != HostCancellationMode::Cooperative)
            throw HostBindingError(HostBindingErrorCode::UnsupportedCancellationMode,
                                   "synchronous Host cancellation mode is invalid");
        if (!valid_value_type(binding.contract.result))
            throw HostBindingError(HostBindingErrorCode::InvalidParameter, "invalid synchronous Host result type");
        if (binding.contract.budget_scope.empty())
            throw HostBindingError(HostBindingErrorCode::MissingBudgetScope, "synchronous Host budget scope is absent");
        if (binding.contract.parameters.size() > limits.max_parameters_per_binding ||
            binding.contract.parameters.size() > limits.max_total_parameters -
                std::min(total_parameters, limits.max_total_parameters))
            throw HostBindingError(HostBindingErrorCode::ParameterLimitExceeded, "synchronous Host parameter limit exceeded");
        total_parameters += binding.contract.parameters.size();

        std::unordered_set<std::string_view> names;
        bool saw_optional = false;
        auto add_string = [&](const std::string_view value) {
            if (value.size() > limits.max_string_bytes ||
                value.size() > limits.max_total_string_bytes -
                    std::min(total_strings, limits.max_total_string_bytes))
                throw HostBindingError(HostBindingErrorCode::StringLimitExceeded, "synchronous Host string limit exceeded");
            total_strings += value.size();
        };
        add_string(binding.binding_id);
        add_string(binding.contract.budget_scope);
        for (const auto& parameter : binding.contract.parameters) {
            if (++work > limits.max_validation_work)
                throw HostBindingError(HostBindingErrorCode::WorkLimitExceeded, "synchronous Host validation work exceeded");
            if (!valid_identifier(parameter.name) || !valid_value_type(parameter.type))
                throw HostBindingError(HostBindingErrorCode::InvalidParameter, "invalid synchronous Host parameter");
            if (saw_optional && parameter.required)
                throw HostBindingError(HostBindingErrorCode::InvalidParameter, "required Host parameter follows an optional parameter");
            saw_optional = saw_optional || !parameter.required;
            if (!names.insert(parameter.name).second)
                throw HostBindingError(HostBindingErrorCode::DuplicateParameter, "duplicate synchronous Host parameter");
            add_string(parameter.name);
        }
    }

    std::sort(bindings.begin(), bindings.end(), [](const auto& left, const auto& right) {
        return left.binding_id < right.binding_id;
    });
    for (std::size_t index = 1; index < bindings.size(); ++index) {
        if (bindings[index - 1].binding_id == bindings[index].binding_id)
            throw HostBindingError(HostBindingErrorCode::DuplicateBinding, "duplicate synchronous Host binding");
    }
    bindings_ = std::move(bindings);
}

const SynchronousNativeBinding* SynchronousNativeBindingSet::find(
    const std::string_view binding_id) const noexcept
{
    const auto found = std::lower_bound(bindings_.begin(), bindings_.end(), binding_id,
        [](const SynchronousNativeBinding& binding, const std::string_view id) {
            return binding.binding_id < id;
        });
    return found != bindings_.end() && found->binding_id == binding_id ? &*found : nullptr;
}

HostErrorTranslation translate_host_error(const HostError& error) noexcept
{
    if (!valid_enum(error.code) || !valid_effect(error.effect_state)) return {};
    using enum HostErrorCode;
    switch (error.code) {
        case CapabilityDenied:
        case PolicyDenied: return {LanguageErrorCode::CapabilityDenied, true};
        case InvalidArgument:
        case ConfigConflict:
        case HandleClosed: return {LanguageErrorCode::HostValidationFailed, true};
        case Cancelled: return {LanguageErrorCode::Cancelled, true};
        case DeadlineExceeded: {
            const auto scope = detail_string(error, "deadline_scope");
            if (!scope) return {};
            if (*scope == "context") return {LanguageErrorCode::DeadlineExceeded, true};
            if (*scope == "call") return {LanguageErrorCode::Timeout, true};
            return {};
        }
        case BudgetExceeded: {
            const auto scope = detail_string(error, "budget_scope");
            if (!scope) return {};
            if (*scope == "external_memory") return {LanguageErrorCode::MemoryLimitExceeded, true};
            if (*scope == "host_operation") return {LanguageErrorCode::TaskLimitExceeded, true};
            return {};
        }
        case Unavailable:
        case IoError:
        case ProtocolError:
        case Backpressure: return {LanguageErrorCode::HostUnavailable, true};
        case DeviceDisconnected: return {LanguageErrorCode::DeviceDisconnected, true};
        case ResourceNotFound: return {LanguageErrorCode::ResourceMissing, true};
        case ModelUnavailable: return {LanguageErrorCode::OcrModelUnavailable, true};
        case Internal: return {LanguageErrorCode::HostInternal, true};
    }
    return {};
}

LanguageErrorCode translate_host_boundary_failure(
    const HostResult::BoundaryFailure failure) noexcept
{
    return failure == HostResult::BoundaryFailure::Allocation
        ? LanguageErrorCode::MemoryLimitExceeded
        : LanguageErrorCode::HostInternal;
}

LanguageErrorCode translate_host_result_runtime_error(
    const RuntimeErrorCode code) noexcept
{
    using enum RuntimeErrorCode;
    switch (code) {
        case TypeMismatch:
        case InvalidUtf8:
        case JsonCycle:
        case JsonNonFinite:
        case JsonUnsupported:
        case JsonDepthLimitExceeded:
        case JsonNodeLimitExceeded:
        case JsonStringLimitExceeded:
        case JsonByteLimitExceeded:
        case JsonWorkLimitExceeded:
        case JsonDuplicateKey:
            return LanguageErrorCode::HostInternal;
        case MemoryLimitExceeded:
        case CellLimitExceeded:
        case SingleAllocationExceeded:
        case StringLimitExceeded:
        case ExternalMemoryLimitExceeded:
        case CollectionWorkLimitExceeded:
            return LanguageErrorCode::MemoryLimitExceeded;
        default:
            return LanguageErrorCode::InternalInvariant;
    }
}

JsonBridgeLimits effective_host_json_limits(const SynchronousHostLimits& limits) noexcept
{
    auto result = limits.json_limits;
    result.max_string_bytes = std::min(result.max_string_bytes, limits.max_string_bytes);
    return result;
}

HostValueMetrics measure_host_value(
    const HostValue& value, const JsonBridgeLimits limits)
{
    HostValueMetrics metrics;
    auto add = [](std::size_t& target, const std::size_t amount,
                  const std::size_t maximum, const RuntimeErrorCode code) {
        if (amount > maximum || target > maximum - amount)
            throw RuntimeError(code, "Host conversion aggregate limit exceeded");
        target += amount;
    };
    auto visit_payload = [&](const std::size_t bytes) {
        add(metrics.total_bytes, bytes, limits.max_total_bytes,
            RuntimeErrorCode::JsonByteLimitExceeded);
    };

    if (value.type() != HostValueType::Json) {
        metrics.nodes = 1;
        metrics.work = 1;
        metrics.total_bytes = 1;
        switch (value.type()) {
            case HostValueType::Boolean: visit_payload(1); break;
            case HostValueType::Integer: visit_payload(sizeof(std::int64_t)); break;
            case HostValueType::Float: visit_payload(sizeof(double)); break;
            case HostValueType::String: {
                const auto bytes = std::get<std::string>(value.storage()).size();
                add(metrics.string_bytes, bytes, limits.max_string_bytes,
                    RuntimeErrorCode::JsonStringLimitExceeded);
                visit_payload(bytes);
                break;
            }
            case HostValueType::Bytes:
                visit_payload(std::get<std::vector<std::byte>>(
                    value.storage()).size());
                break;
            case HostValueType::HostResource:
            case HostValueType::HostImage:
            case HostValueType::HostOcrModel:
            case HostValueType::HostDevice: {
                const auto& handle = std::get<HostHandleValue>(value.storage());
                visit_payload(sizeof(std::uint64_t) * 7 + sizeof(std::size_t));
                if (handle.type_id() != host_handle_type(value.type()))
                    throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                                       "Host handle value type is inconsistent");
                break;
            }
            default: break;
        }
        return metrics;
    }

    struct Pending { const JsonValue* value; std::size_t depth; };
    std::vector<Pending> pending{{&std::get<JsonValue>(value.storage()), 1}};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (current.depth > std::min<std::size_t>(limits.max_depth, 1024))
            throw RuntimeError(RuntimeErrorCode::JsonDepthLimitExceeded,
                               "Host conversion depth limit exceeded");
        add(metrics.nodes, 1, limits.max_nodes, RuntimeErrorCode::JsonNodeLimitExceeded);
        add(metrics.work, 1, limits.max_work, RuntimeErrorCode::JsonWorkLimitExceeded);
        visit_payload(1);
        switch (current.value->kind()) {
            case JsonKind::Null: break;
            case JsonKind::Boolean: visit_payload(1); break;
            case JsonKind::Integer: visit_payload(sizeof(std::int64_t)); break;
            case JsonKind::Float: visit_payload(sizeof(double)); break;
            case JsonKind::String: {
                const auto bytes = std::get<std::string>(current.value->value()).size();
                add(metrics.string_bytes, bytes, limits.max_string_bytes,
                    RuntimeErrorCode::JsonStringLimitExceeded);
                visit_payload(bytes);
                break;
            }
            case JsonKind::Array: {
                const auto& values = std::get<JsonArray>(current.value->value());
                add(metrics.work, values.size(), limits.max_work,
                    RuntimeErrorCode::JsonWorkLimitExceeded);
                if (values.size() > limits.max_nodes - std::min(metrics.nodes, limits.max_nodes))
                    throw RuntimeError(RuntimeErrorCode::JsonNodeLimitExceeded,
                                       "Host conversion node limit exceeded");
                for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator)
                    pending.push_back({&*iterator, current.depth + 1});
                break;
            }
            case JsonKind::Object: {
                const auto& entries = std::get<JsonObject>(current.value->value());
                add(metrics.work, entries.size(), limits.max_work,
                    RuntimeErrorCode::JsonWorkLimitExceeded);
                if (entries.size() > limits.max_nodes - std::min(metrics.nodes, limits.max_nodes))
                    throw RuntimeError(RuntimeErrorCode::JsonNodeLimitExceeded,
                                       "Host conversion node limit exceeded");
                for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
                    const auto key_bytes = iterator->first.size();
                    add(metrics.string_bytes, key_bytes, limits.max_string_bytes,
                        RuntimeErrorCode::JsonStringLimitExceeded);
                    visit_payload(key_bytes);
                    pending.push_back({&iterator->second, current.depth + 1});
                }
                break;
            }
        }
    }
    return metrics;
}

HostValue heap_to_host_value(
    const Heap& heap, const Value value, const HostValueType expected,
    const JsonBridgeLimits limits, HostReleaseDispatcher* const handles)
{
    const auto kind = heap.kind(value);
    switch (expected) {
        case HostValueType::Null:
            if (kind == ValueKind::Null) return HostValue();
            break;
        case HostValueType::Boolean:
            if (kind == ValueKind::Boolean) return HostValue(value.as_boolean());
            break;
        case HostValueType::Integer:
            if (kind == ValueKind::Integer) return HostValue(value.as_integer());
            break;
        case HostValueType::Float:
            if (kind == ValueKind::Float && std::isfinite(value.as_float())) return HostValue(value.as_float());
            break;
        case HostValueType::String:
            if (kind == ValueKind::String) {
                auto string = heap.string_copy(value.as_heap_ref());
                if (string.size() > limits.max_string_bytes)
                    throw RuntimeError(RuntimeErrorCode::JsonStringLimitExceeded, "Host string limit exceeded");
                return HostValue(std::move(string));
            }
            break;
        case HostValueType::Bytes:
            if (kind == ValueKind::Bytes) {
                HostValue result(heap.bytes_copy(value.as_heap_ref()));
                (void)measure_host_value(result, limits);
                return result;
            }
            break;
        case HostValueType::Json:
            return HostValue(heap_value_to_json(heap, value, limits));
        case HostValueType::OrderedStringJsonMap: {
            auto converted = heap_value_to_json(heap, value, limits);
            if (converted.kind() != JsonKind::Object)
                throw RuntimeError(RuntimeErrorCode::TypeMismatch, "Host argument must be an ordered map");
            return HostValue(std::move(converted));
        }
        case HostValueType::HostResource:
        case HostValueType::HostImage:
        case HostValueType::HostOcrModel:
        case HostValueType::HostDevice:
            if (!handles)
                throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                                   "typed Host handle dispatcher is absent");
            return HostValue(handles->borrow(heap, value, host_handle_type(expected)));
    }
    throw RuntimeError(RuntimeErrorCode::TypeMismatch, "Host argument type does not match the binding contract");
}

Value host_to_heap_value(
    Heap& heap, const HostValue& value, const HostValueType expected,
    const JsonBridgeLimits limits, HostReleaseDispatcher* const handles)
{
    if (!validate_host_value(value, expected, limits))
        throw RuntimeError(RuntimeErrorCode::TypeMismatch, "Host result does not match the binding contract");
    switch (expected) {
        case HostValueType::Null: return Value::null();
        case HostValueType::Boolean: return Value(std::get<bool>(value.storage()));
        case HostValueType::Integer: return Value(std::get<std::int64_t>(value.storage()));
        case HostValueType::Float: return Value(std::get<double>(value.storage()));
        case HostValueType::String: return heap.allocate_string(std::get<std::string>(value.storage()));
        case HostValueType::Bytes:
            (void)measure_host_value(value, limits);
            return heap.allocate_bytes(
                std::get<std::vector<std::byte>>(value.storage()));
        case HostValueType::Json: return json_to_heap_value(heap, std::get<JsonValue>(value.storage()), limits);
        case HostValueType::OrderedStringJsonMap:
            return json_to_heap_value(heap, std::get<JsonValue>(value.storage()), limits);
        case HostValueType::HostResource:
        case HostValueType::HostImage:
        case HostValueType::HostOcrModel:
        case HostValueType::HostDevice:
            if (!handles)
                throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                                   "typed Host handle dispatcher is absent");
            return handles->publish(
                heap, std::get<HostHandleValue>(value.storage()),
                host_handle_type(expected));
    }
    throw RuntimeError(RuntimeErrorCode::TypeMismatch, "invalid Host result contract");
}

HostResult invoke_host_callback(
    const SynchronousNativeBinding& binding, const HostCallContext& context,
    const HostArguments& arguments, const SynchronousHostLimits& limits,
    HostReleaseDispatcher* const handles) noexcept
{
    struct CallbackBoundary final {
        const HostArguments& arguments;
        HostReleaseDispatcher* handles{};
        std::uint64_t scope{};

        ~CallbackBoundary() { finish(nullptr); }
        void finish(const HostHandleValue* retained) noexcept
        {
            for (const auto& argument : arguments) {
                if (!argument || !is_host_handle_type(argument->type())) continue;
                std::get<HostHandleValue>(argument->storage()).revoke_callback_borrow();
            }
            if (handles && scope != 0)
                handles->finish_callback_scope(scope, retained);
            scope = 0;
        }
    } boundary{arguments, handles, 0};
    try {
        if (binding.contract.execution != HostExecutionMode::ThreadSafe ||
            (binding.contract.cancellation != HostCancellationMode::Preflight &&
             binding.contract.cancellation != HostCancellationMode::Cooperative))
            return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
        if (context.deadline_exceeded())
            return HostResult::failure({
                HostErrorCode::DeadlineExceeded, "Host call deadline exceeded", true,
                HostEffectState::NotStarted,
                JsonValue(JsonObject{{"deadline_scope", JsonValue("call")}})});
        if (context.cancelled())
            return HostResult::failure({
                HostErrorCode::Cancelled, "Host call cancelled", false,
                HostEffectState::NotStarted, std::nullopt});
        if (handles) boundary.scope = handles->begin_callback_scope();
        auto result = binding.callback(context, arguments);
        if (result.ok()) {
            if (!validate_host_value(
                    result.value(), binding.contract.result, effective_host_json_limits(limits))) {
                boundary.finish(nullptr);
                return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
            }
            if (result.value().type() == HostValueType::Bytes) {
                (void)measure_host_value(
                    result.value(), effective_host_json_limits(limits));
            }
            const HostHandleValue* retained{};
            if (is_host_handle_type(binding.contract.result))
                retained = &std::get<HostHandleValue>(result.value().storage());
            boundary.finish(retained);
            return result;
        }
        if (!result.has_error()) {
            boundary.finish(nullptr);
            return result;
        }
        const auto& error = result.error();
        if (!valid_enum(error.code) || !valid_effect(error.effect_state) ||
            error.message.size() > limits.max_safe_message_bytes ||
            !valid_utf8(error.message) ||
            (error.details && !validate_json(
                *error.details, effective_host_json_limits(limits)))) {
            boundary.finish(nullptr);
            return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
        }
        boundary.finish(nullptr);
        return result;
    } catch (const std::bad_alloc&) {
        return HostResult::boundary_failure(HostResult::BoundaryFailure::Allocation);
    } catch (const std::exception&) {
        return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
    } catch (...) {
        return HostResult::boundary_failure(HostResult::BoundaryFailure::CallbackException);
    }
}

std::vector<InMemoryLogEvent> InMemoryLogHost::events() const
{
    const std::scoped_lock lock(mutex_);
    return events_;
}

HostResult InMemoryLogHost::emit(const HostCallContext&, const HostArguments& arguments)
{
    if (arguments.size() != 3 || !arguments[0] || !arguments[1] ||
        arguments[0]->type() != HostValueType::String ||
        arguments[1]->type() != HostValueType::String ||
        (arguments[2] && (arguments[2]->type() != HostValueType::Json ||
            std::get<JsonValue>(arguments[2]->storage()).kind() != JsonKind::Object))) {
        return HostResult::failure({
            HostErrorCode::InvalidArgument, "invalid in-memory log arguments", false,
            HostEffectState::NotStarted, std::nullopt});
    }
    InMemoryLogEvent event;
    event.level = std::get<std::string>(arguments[0]->storage());
    event.message = std::get<std::string>(arguments[1]->storage());
    if (arguments[2])
        event.fields = std::get<JsonObject>(
            std::get<JsonValue>(arguments[2]->storage()).value());
    {
        const std::scoped_lock lock(mutex_);
        events_.push_back(std::move(event));
    }
    return HostResult::success();
}

SynchronousNativeBinding make_in_memory_log_binding(
    std::shared_ptr<InMemoryLogHost> host)
{
    if (!host)
        throw HostBindingError(
            HostBindingErrorCode::MissingCallback, "in-memory log host is absent");
    return {
        "host.log.emit.v1",
        {{{"level", HostValueType::String, true},
          {"message", HostValueType::String, true},
          {"fields", HostValueType::OrderedStringJsonMap, false}},
         HostValueType::Null,
         "log_events",
         HostExecutionMode::ThreadSafe,
         HostCancellationMode::Preflight},
        [host = std::move(host)](
            const HostCallContext& context, const HostArguments& arguments) {
            return host->emit(context, arguments);
        }};
}

}  // namespace baas::script::runtime
