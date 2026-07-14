#include "service/health/HealthReadiness.h"

#include <utility>

namespace baas::service::health {

HealthReadinessOwner::HealthReadinessOwner(router::HealthSnapshot initial)
    : snapshot_{router::HealthReadinessState::starting, std::move(initial)}
{}

void HealthReadinessOwner::begin_startup(router::HealthSnapshot snapshot)
{
    std::lock_guard<std::mutex> lock{mutex_};
    snapshot_.health = std::move(snapshot);
    snapshot_.state = router::HealthReadinessState::starting;
}

bool HealthReadinessOwner::publish_ready(router::HealthSnapshot snapshot)
{
    std::lock_guard<std::mutex> lock{mutex_};
    if (snapshot_.state != router::HealthReadinessState::starting) return false;
    snapshot_.health = std::move(snapshot);
    snapshot_.state = router::HealthReadinessState::ready;
    return true;
}

void HealthReadinessOwner::publish_failed(router::HealthSnapshot snapshot)
{
    std::lock_guard<std::mutex> lock{mutex_};
    snapshot_.health = std::move(snapshot);
    snapshot_.state = router::HealthReadinessState::failed;
}

router::HealthReadinessSnapshot HealthReadinessOwner::readiness_snapshot() const
{
    std::lock_guard<std::mutex> lock{mutex_};
    return snapshot_;
}

router::HealthReadinessState HealthReadinessOwner::state() const
{
    std::lock_guard<std::mutex> lock{mutex_};
    return snapshot_.state;
}

}  // namespace baas::service::health
