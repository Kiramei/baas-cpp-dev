#include "script/host/ResourceHost.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace baas::script::host {
namespace {

using runtime::HostArguments;
using runtime::HostCallContext;
using runtime::HostEffectState;
using runtime::HostErrorCode;
using runtime::HostHandleTransferKind;
using runtime::HostHandleTypeId;
using runtime::HostHandleValue;
using runtime::HostResult;
using runtime::HostValue;
using runtime::HostValueType;

struct HandleKey {
    std::uint64_t id{};
    std::uint64_t generation{};
    friend bool operator==(const HandleKey&, const HandleKey&) = default;
};

struct HandleKeyHash {
    [[nodiscard]] std::size_t operator()(const HandleKey& value) const noexcept
    {
        auto hash = static_cast<std::size_t>(value.id);
        hash ^= static_cast<std::size_t>(value.generation) +
            static_cast<std::size_t>(0x9e3779b9U) + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

[[nodiscard]] runtime::JsonValue budget_detail(const char* scope)
{
    return runtime::JsonValue(runtime::JsonObject{
        {"budget_scope", runtime::JsonValue(scope)}});
}

[[nodiscard]] runtime::JsonValue deadline_detail()
{
    return runtime::JsonValue(runtime::JsonObject{
        {"deadline_scope", runtime::JsonValue("call")}});
}

[[nodiscard]] HostResult invalid(std::string message)
{
    return HostResult::failure({HostErrorCode::InvalidArgument, std::move(message), false,
                                HostEffectState::NotStarted, std::nullopt});
}

[[nodiscard]] HostResult cancelled()
{
    return HostResult::failure({HostErrorCode::Cancelled, "resource read cancelled", false,
                                HostEffectState::NotStarted, std::nullopt});
}

[[nodiscard]] HostResult deadline()
{
    return HostResult::failure({HostErrorCode::DeadlineExceeded,
                                "resource read deadline exceeded", false,
                                HostEffectState::NotStarted, deadline_detail()});
}

}  // namespace

struct ResourceHost::Impl {
    Impl(std::shared_ptr<const resources::ResourceSnapshot> value,
         const ResourceHostLimits configured)
        : snapshot(std::move(value)), limits(configured) {}

    [[nodiscard]] HostResult resolve(
        const std::shared_ptr<runtime::HostReleaseDispatcher>& dispatcher,
        const HostArguments& arguments)
    {
        if (arguments.size() != 2 || !arguments[0] ||
            arguments[0]->type() != HostValueType::String ||
            (arguments[1] && arguments[1]->type() != HostValueType::String))
            return invalid("invalid resource resolve arguments");
        const auto& resource_id = std::get<std::string>(arguments[0]->storage());
        if (!snapshot->accepts_resource_id(resource_id))
            return invalid("resource id is not canonical");
        std::optional<std::string_view> locale;
        if (arguments[1]) {
            const auto& value = std::get<std::string>(arguments[1]->storage());
            if (!snapshot->accepts_locale(value))
                return invalid("resource locale is not canonical");
            locale = value;
        }
        auto entry = snapshot->resolve(resource_id, locale);
        if (!entry)
            return HostResult::failure({HostErrorCode::ResourceNotFound,
                "resource is absent from the immutable snapshot", false,
                HostEffectState::NotStarted, std::nullopt});
        {
            const std::scoped_lock lock(mutex);
            if (slots.size() >= limits.max_open_handles)
                return HostResult::failure({HostErrorCode::BudgetExceeded,
                    "resource handle budget exceeded", true,
                    HostEffectState::NotStarted, budget_detail("host_operation")});
        }
        if (entry->retained_bytes() >
            std::numeric_limits<std::size_t>::max() - sizeof(HandleKey))
            return HostResult::failure({HostErrorCode::BudgetExceeded,
                "resource external-memory charge overflow", false,
                HostEffectState::NotStarted, budget_detail("external_memory")});
        auto reservation = dispatcher->reserve(
            HostHandleTypeId::Resource, resource_release_adapter_id,
            entry->retained_bytes() + sizeof(HandleKey));
        auto handle = dispatcher->adopt(std::move(reservation));
        try {
            const std::scoped_lock lock(mutex);
            if (slots.size() >= limits.max_open_handles) {
                dispatcher->abandon(handle);
                return HostResult::failure({HostErrorCode::BudgetExceeded,
                    "resource handle budget exceeded", true,
                    HostEffectState::NotStarted, budget_detail("host_operation")});
            }
            const auto [ignored, inserted] = slots.emplace(
                HandleKey{handle.handle_id(), handle.generation()}, std::move(entry));
            (void)ignored;
            if (!inserted) {
                dispatcher->abandon(handle);
                return HostResult::failure({HostErrorCode::Internal,
                    "resource handle identity collision", false,
                    HostEffectState::NotStarted, std::nullopt});
            }
            ++resolved;
        } catch (...) {
            dispatcher->abandon(handle);
            throw;
        }
        return HostResult::success(HostValue(std::move(handle)));
    }

    [[nodiscard]] HostResult read(
        const HostCallContext& context,
        const HostArguments& arguments)
    {
        if (arguments.size() != 2 || !arguments[0] || !arguments[1] ||
            arguments[0]->type() != HostValueType::HostResource ||
            arguments[1]->type() != HostValueType::Integer)
            return invalid("invalid resource read arguments");
        const auto& handle = std::get<HostHandleValue>(arguments[0]->storage());
        if (handle.transfer_kind() != HostHandleTransferKind::BorrowedReference ||
            handle.adapter_id() != resource_release_adapter_id ||
            handle.snapshot_id() != snapshot->numeric_snapshot_id())
            return HostResult::failure({HostErrorCode::HandleClosed,
                "resource handle does not belong to this snapshot", false,
                HostEffectState::NotStarted, std::nullopt});
        const auto requested = std::get<std::int64_t>(arguments[1]->storage());
        if (requested <= 0)
            return HostResult::failure({HostErrorCode::BudgetExceeded,
                "resource byte budget must be positive", true,
                HostEffectState::NotStarted, budget_detail("external_memory")});
        const auto max_bytes = static_cast<std::uint64_t>(requested);
        std::shared_ptr<const resources::ResourceEntry> entry;
        {
            const std::scoped_lock lock(mutex);
            const auto found = slots.find({handle.handle_id(), handle.generation()});
            if (found == slots.end())
                return HostResult::failure({HostErrorCode::HandleClosed,
                    "resource handle is closed", false,
                    HostEffectState::NotStarted, std::nullopt});
            entry = found->second;
        }
        if (entry->retained_bytes() > max_bytes ||
            entry->retained_bytes() > limits.max_single_read_bytes)
            return HostResult::failure({HostErrorCode::BudgetExceeded,
                "resource byte budget exceeded", true,
                HostEffectState::NotStarted, budget_detail("external_memory")});
        if (context.deadline_exceeded()) return deadline();
        if (context.cancelled()) return cancelled();

        auto used = read_bytes.load(std::memory_order_relaxed);
        for (;;) {
            if (entry->retained_bytes() > limits.max_total_read_bytes -
                    std::min(used, limits.max_total_read_bytes))
                return HostResult::failure({HostErrorCode::BudgetExceeded,
                    "resource aggregate byte budget exceeded", true,
                    HostEffectState::NotStarted, budget_detail("external_memory")});
            if (read_bytes.compare_exchange_weak(
                    used, used + entry->retained_bytes(),
                    std::memory_order_acq_rel, std::memory_order_relaxed)) break;
        }
        std::vector<std::byte> output;
        try {
            output.reserve(entry->retained_bytes());
            const auto source = entry->bytes();
            for (std::size_t offset = 0; offset < source.size();) {
                if (context.deadline_exceeded()) {
                    read_bytes.fetch_sub(entry->retained_bytes(), std::memory_order_acq_rel);
                    return deadline();
                }
                if (context.cancelled()) {
                    read_bytes.fetch_sub(entry->retained_bytes(), std::memory_order_acq_rel);
                    return cancelled();
                }
                const auto count = std::min(limits.cooperative_chunk_bytes, source.size() - offset);
                output.insert(output.end(), source.begin() + offset, source.begin() + offset + count);
                offset += count;
            }
        } catch (...) {
            read_bytes.fetch_sub(entry->retained_bytes(), std::memory_order_acq_rel);
            throw;
        }
        read_calls.fetch_add(1, std::memory_order_relaxed);
        return HostResult::success(HostValue(std::move(output)));
    }

    [[nodiscard]] bool release(const HostHandleValue& handle) noexcept
    {
        try {
            if (handle.type_id() != HostHandleTypeId::Resource ||
                handle.adapter_id() != resource_release_adapter_id ||
                handle.snapshot_id() != snapshot->numeric_snapshot_id()) return false;
            const std::scoped_lock lock(mutex);
            const auto erased = slots.erase({handle.handle_id(), handle.generation()});
            if (erased != 0) ++released;
            return true; // idempotent ACK for an already-retired native slot
        } catch (...) {
            return false;
        }
    }

    std::shared_ptr<const resources::ResourceSnapshot> snapshot;
    ResourceHostLimits limits;
    mutable std::mutex mutex;
    std::unordered_map<HandleKey, std::shared_ptr<const resources::ResourceEntry>,
                       HandleKeyHash> slots;
    std::size_t resolved{};
    std::size_t released{};
    std::atomic<std::size_t> read_calls{};
    std::atomic<std::size_t> read_bytes{};
};

ResourceHost::ResourceHost(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
ResourceHost::~ResourceHost() = default;

const std::shared_ptr<const resources::ResourceSnapshot>&
ResourceHost::snapshot() const noexcept { return impl_->snapshot; }

ResourceHostStats ResourceHost::stats() const noexcept
{
    const std::scoped_lock lock(impl_->mutex);
    return {impl_->slots.size(), impl_->resolved, impl_->released,
            impl_->read_calls.load(std::memory_order_relaxed),
            impl_->read_bytes.load(std::memory_order_relaxed)};
}

ResourceHostRuntime make_resource_host_runtime(
    std::shared_ptr<const resources::ResourceSnapshot> snapshot,
    const ResourceHostLimits limits)
{
    if (!snapshot) throw std::invalid_argument("resource snapshot is absent");
    if (limits.max_open_handles == 0 || limits.max_single_read_bytes == 0 ||
        limits.max_total_read_bytes == 0 || limits.cooperative_chunk_bytes == 0 ||
        limits.max_single_read_bytes > limits.max_total_read_bytes)
        throw std::invalid_argument("resource Host limits must be non-zero and ordered");
    auto host = std::shared_ptr<ResourceHost>(new ResourceHost(
        std::make_unique<ResourceHost::Impl>(snapshot, limits)));
    std::weak_ptr<ResourceHost> weak = host;
    auto handles = std::make_shared<runtime::HostReleaseDispatcher>(
        snapshot->numeric_snapshot_id(),
        std::vector<runtime::HostReleaseAdapter>{{
            resource_release_adapter_id,
            [weak](const HostHandleValue& handle) {
                const auto owner = weak.lock();
                return owner && owner->impl_->release(handle);
            }}});

    runtime::SynchronousNativeBinding resolve;
    resolve.binding_id = "host.resource.resolve.v1";
    resolve.contract = {
        {{"resource_id", HostValueType::String, true},
         {"locale", HostValueType::String, false}},
        HostValueType::HostResource, "resource_lookups",
        runtime::HostExecutionMode::ThreadSafe,
        runtime::HostCancellationMode::Preflight};
    resolve.callback = [host, handles](const HostCallContext&, const HostArguments& arguments) {
        try {
            return host->impl_->resolve(handles, arguments);
        } catch (const runtime::RuntimeError& error) {
            if (error.code() == runtime::RuntimeErrorCode::ExternalMemoryLimitExceeded)
                return HostResult::failure({HostErrorCode::BudgetExceeded,
                    "resource external-memory budget exceeded", true,
                    HostEffectState::NotStarted, budget_detail("external_memory")});
            return HostResult::failure({HostErrorCode::Internal,
                "resource handle allocation failed", false,
                HostEffectState::NotStarted, std::nullopt});
        }
    };

    runtime::SynchronousNativeBinding read;
    read.binding_id = "host.resource.read.v1";
    read.contract = {
        {{"resource", HostValueType::HostResource, true},
         {"max_bytes", HostValueType::Integer, true}},
        HostValueType::Bytes, "resource_bytes",
        runtime::HostExecutionMode::ThreadSafe,
        runtime::HostCancellationMode::Cooperative};
    read.callback = [host](const HostCallContext& context, const HostArguments& arguments) {
        return host->impl_->read(context, arguments);
    };

    auto metadata = std::make_shared<const runtime::HostModuleRegistry>(
        std::vector<runtime::HostModuleDescriptor>{{
            "baas/resource", {1, 0},
            {{"read", "host.resource.read.v1", "resource.read"},
             {"resolve", "host.resource.resolve.v1", "resource.read"}}}});
    auto bindings = std::make_shared<const runtime::SynchronousNativeBindingSet>(
        std::vector<runtime::SynchronousNativeBinding>{
            std::move(resolve), std::move(read)});
    return {std::move(host), std::move(handles), std::move(metadata), std::move(bindings)};
}

}  // namespace baas::script::host
