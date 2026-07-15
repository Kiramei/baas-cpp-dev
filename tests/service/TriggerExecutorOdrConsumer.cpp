#include "service/trigger/TriggerExecutor.h"

#include <cstddef>

std::size_t trigger_executor_production_header_size() noexcept
{
    return sizeof(baas::service::trigger::TriggerExecutor);
}
