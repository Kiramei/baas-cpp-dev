#pragma once

#include "script/runtime/ValueHeap.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace baas::script::runtime {

enum class BindingMutability {
    Immutable,
    Mutable,
};

enum class AssignResult {
    Updated,
    Undefined,
    Immutable,
};

// A lexical environment owns explicit heap roots for every local binding.
// The Heap must outlive all environments that refer to it.
class Environment final {
public:
    explicit Environment(Heap& heap, std::shared_ptr<Environment> parent = {});
    ~Environment();

    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&&) = delete;
    Environment& operator=(Environment&&) = delete;

    [[nodiscard]] bool define(
        std::string name,
        Value value,
        BindingMutability mutability = BindingMutability::Mutable);
    [[nodiscard]] std::optional<Value> lookup(std::string_view name) const;
    [[nodiscard]] AssignResult assign(std::string_view name, Value value);

    [[nodiscard]] bool contains_local(std::string_view name) const;
    [[nodiscard]] std::size_t local_size() const noexcept;
    [[nodiscard]] Heap& heap() const noexcept { return *heap_; }
    [[nodiscard]] const std::shared_ptr<Environment>& parent() const noexcept { return parent_; }

private:
    struct Binding {
        Value value;
        Heap::RootId root;
        BindingMutability mutability;
    };

    Heap* heap_;
    std::shared_ptr<Environment> parent_;
    std::unordered_map<std::string, Binding> bindings_;
};

}  // namespace baas::script::runtime
