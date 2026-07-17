#include "service/runtime/RuntimeTaskOwner.h"

#include <cstddef>

std::size_t runtime_task_owner_size_from_non_hook_tu() noexcept
{
    return sizeof(baas::service::runtime::RuntimeTaskOwner);
}
