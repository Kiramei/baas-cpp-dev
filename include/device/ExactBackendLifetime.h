#pragma once

#include <memory>
#include <utility>

namespace baas::detail {

// Own a concrete backend until its potentially-throwing initialization has
// completed. The wrappers intentionally retain their historical Base* layout;
// exact deletion avoids requiring a new virtual destructor in the legacy base
// classes and therefore preserves their vtable ABI.
template <class Derived, class Initialize, class... Arguments>
[[nodiscard]] std::unique_ptr<Derived> make_initialized_backend(
    Initialize&& initialize, Arguments&&... arguments)
{
    auto owner = std::make_unique<Derived>(
        std::forward<Arguments>(arguments)...);
    std::forward<Initialize>(initialize)(*owner);
    return owner;
}

template <class Derived, class Base>
void delete_exact_backend(Base*& backend) noexcept
{
    auto* exact = static_cast<Derived*>(std::exchange(backend, nullptr));
    delete exact;
}

}  // namespace baas::detail
