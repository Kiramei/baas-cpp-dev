#include "script/runtime/Environment.h"

#include <new>
#include <stdexcept>
#include <utility>

namespace baas::script::runtime {

Environment::Environment(Heap& heap, std::shared_ptr<Environment> parent)
    : heap_(&heap), parent_(std::move(parent))
{
    if (parent_ && parent_->heap().identity() != heap.identity()) {
        throw std::invalid_argument("parent environment belongs to a different heap");
    }
}

Environment::~Environment()
{
    for (const auto& [name, binding] : bindings_) {
        (void)name;
        heap_->remove_root(binding.root);
    }
}

bool Environment::define(std::string name, const Value value, const BindingMutability mutability)
{
    if (name.empty() || bindings_.contains(name)) {
        return false;
    }

    const auto root = heap_->add_root(value);
    try {
        const auto [position, inserted] = bindings_.emplace(
            std::move(name), Binding{value, root, mutability});
        (void)position;
        if (!inserted) {
            heap_->remove_root(root);
        }
        return inserted;
    } catch (const std::bad_alloc&) {
        heap_->remove_root(root);
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "environment binding allocation failed");
    } catch (...) {
        heap_->remove_root(root);
        throw;
    }
}

std::optional<Value> Environment::lookup(const std::string_view name) const
{
    const std::string key(name);
    for (auto current = this; current != nullptr; current = current->parent_.get()) {
        const auto found = current->bindings_.find(key);
        if (found != current->bindings_.end()) {
            return found->second.value;
        }
    }
    return std::nullopt;
}

AssignResult Environment::assign(const std::string_view name, const Value value)
{
    const std::string key(name);
    for (auto current = this; current != nullptr; current = current->parent_.get()) {
        const auto found = current->bindings_.find(key);
        if (found == current->bindings_.end()) {
            continue;
        }
        if (found->second.mutability == BindingMutability::Immutable) {
            return AssignResult::Immutable;
        }
        if (!current->heap_->update_root(found->second.root, value)) {
            throw std::logic_error("environment binding root is missing");
        }
        found->second.value = value;
        return AssignResult::Updated;
    }
    return AssignResult::Undefined;
}

bool Environment::contains_local(const std::string_view name) const
{
    return bindings_.contains(std::string{name});
}

std::size_t Environment::local_size() const noexcept { return bindings_.size(); }

}  // namespace baas::script::runtime
