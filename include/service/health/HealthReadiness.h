#pragma once

#include "service/router/Router.h"

#include <mutex>

namespace baas::service::health {

// Production ownership point for the public readiness projection. Runtime and
// auth owners publish complete values; HTTP readers receive a stable copy from
// one publication under the same lock.
class HealthReadinessOwner final : public router::HealthSnapshotProvider {
public:
    explicit HealthReadinessOwner(router::HealthSnapshot initial = {});

    HealthReadinessOwner(const HealthReadinessOwner&) = delete;
    HealthReadinessOwner& operator=(const HealthReadinessOwner&) = delete;
    HealthReadinessOwner(HealthReadinessOwner&&) = delete;
    HealthReadinessOwner& operator=(HealthReadinessOwner&&) = delete;

    // Starts a new lifecycle. This is the only transition out of
    // failed and is also used before a stopped service is restarted.
    void begin_startup(router::HealthSnapshot snapshot = {});

    // Readiness may be published only after begin_startup(). Returning false
    // prevents a failed service from becoming ready without an explicit restart.
    [[nodiscard]] bool publish_ready(router::HealthSnapshot snapshot);

    // A failure can be published while starting, ready, or already failed.
    void publish_failed(router::HealthSnapshot snapshot = {});

    [[nodiscard]] router::HealthReadinessSnapshot readiness_snapshot() const override;
    [[nodiscard]] router::HealthReadinessState state() const;

private:
    mutable std::mutex mutex_;
    router::HealthReadinessSnapshot snapshot_;
};

}  // namespace baas::service::health
