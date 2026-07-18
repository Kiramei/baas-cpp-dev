#pragma once

#include "core_defines.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

BAAS_NAMESPACE_BEGIN

enum class LegacyProcedureRunCode : std::uint8_t {
    Success,
    MissingProcedure,
    InvalidDefinition,
    Cancelled,
    DeadlineExceeded,
    DeviceDisconnected,
    ForegroundPackageMismatch,
    ResourceNotFound,
    BudgetExceeded,
    ResourceExhausted,
    Unavailable,
    Internal,
};

struct LegacyProcedureRunResult {
    LegacyProcedureRunCode code{LegacyProcedureRunCode::Internal};
    bool retryable{};
    std::string source_terminal;

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return code == LegacyProcedureRunCode::Success;
    }
};

[[nodiscard]] std::string_view legacy_procedure_run_code_name(
    LegacyProcedureRunCode code) noexcept;
[[nodiscard]] bool valid_legacy_procedure_id(
    std::string_view value, std::size_t max_bytes = 1'024) noexcept;
[[nodiscard]] LegacyProcedureRunResult map_legacy_procedure_exception(
    std::exception_ptr error) noexcept;

class LegacyProcedureCancelled final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override;
};

class LegacyProcedureDeadlineExceeded final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override;
};

class LegacyProcedureForegroundPackageMismatch final : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override;
};

enum class LegacyProcedureCheckpoint : std::uint8_t {
    Proceed,
    Cancelled,
    DeadlineExceeded,
};

class LegacyProcedureExecutionControl final {
public:
    using Clock = std::chrono::steady_clock;

    explicit LegacyProcedureExecutionControl(
        std::stop_token stop = {},
        std::optional<Clock::time_point> deadline = std::nullopt) noexcept;

    [[nodiscard]] LegacyProcedureCheckpoint checkpoint(
        bool owner_running = true) const noexcept;
    void throw_if_stopped(bool owner_running = true) const;

private:
    std::stop_token stop_;
    std::optional<Clock::time_point> deadline_;
};

[[nodiscard]] LegacyProcedureRunResult map_legacy_procedure_exception(
    std::exception_ptr error,
    const LegacyProcedureExecutionControl& control,
    bool owner_running) noexcept;

enum class LegacyProcedureEffect : std::uint8_t {
    Capture,
    Vision,
    Input,
    Wait,
    ForegroundCheck,
};

enum class LegacyProcedureEffectStage : std::uint8_t {
    Began,
    Committed,
    Unknown,
};

class LegacyProcedureEffectObserver {
public:
    virtual ~LegacyProcedureEffectObserver() = default;
    [[nodiscard]] virtual bool report(
        LegacyProcedureEffect effect, LegacyProcedureEffectStage stage) noexcept = 0;
};

class LegacyProcedureEffectScope final {
public:
    LegacyProcedureEffectScope(
        LegacyProcedureEffectObserver* observer, LegacyProcedureEffect effect) noexcept;
    ~LegacyProcedureEffectScope();

    LegacyProcedureEffectScope(const LegacyProcedureEffectScope&) = delete;
    LegacyProcedureEffectScope& operator=(const LegacyProcedureEffectScope&) = delete;
    LegacyProcedureEffectScope(LegacyProcedureEffectScope&& other) noexcept;
    LegacyProcedureEffectScope& operator=(LegacyProcedureEffectScope&&) = delete;

    [[nodiscard]] bool began() const noexcept;
    [[nodiscard]] bool commit() noexcept;

private:
    LegacyProcedureEffectObserver* observer_{};
    LegacyProcedureEffect effect_{};
    bool began_{};
    bool terminal_{};
};

enum class LegacyProcedureRunMode : std::uint8_t {
    Production,
    CompatibilityUninstrumented,
};

struct LegacyProcedureRunOptions {
    const LegacyProcedureExecutionControl* control{};
    LegacyProcedureEffectObserver* effects{};
    LegacyProcedureRunMode mode{LegacyProcedureRunMode::Production};
};

// The legacy operation layer has not yet wired every required effect. Native
// production adapters must fail closed until this reports true. Compatibility
// callers may still use the old solve_procedure API explicitly.
[[nodiscard]] constexpr bool legacy_procedure_production_ready() noexcept
{
    return false;
}

BAAS_NAMESPACE_END
