#pragma once

#include "service/protocol/TriggerEnvelope.h"
#include "service/trigger/TriggerCommandCatalog.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace baas::service::protocol::trigger {

struct TriggerIngressLimits {
    TriggerEnvelopeLimits envelope{};
    std::size_t max_json_frame_bytes{1U * 1'024U * 1'024U};
    std::size_t max_binary_frame_bytes{64U * 1'024U * 1'024U};
    std::size_t max_aggregate_bytes{65U * 1'024U * 1'024U};
};

enum class TriggerIngressState : std::uint8_t {
    accepting_json,
    awaiting_binary,
    ready,
    closed,
};

enum class TriggerIngressOutcome : std::uint8_t {
    rejected,
    awaiting_binary,
    ready,
};

enum class TriggerIngressError : std::uint8_t {
    none,
    closed,
    item_pending,
    json_while_awaiting_binary,
    binary_without_declaration,
    json_too_large,
    binary_too_large,
    aggregate_too_large,
    envelope_rejected,
    unknown_command,
    config_id_required,
    binary_marker_required,
    binary_marker_forbidden,
    admission_rejected,
};

[[nodiscard]] std::string_view trigger_ingress_error_name(
    TriggerIngressError error
) noexcept;

struct TriggerIngressResult {
    TriggerIngressOutcome outcome{TriggerIngressOutcome::rejected};
    TriggerIngressError error{TriggerIngressError::none};
    EnvelopeError envelope_error{EnvelopeError::none};
    std::size_t error_offset{};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TriggerIngressError::none
            && outcome != TriggerIngressOutcome::rejected;
    }
};

// A completed ingress unit owns every value needed by a later dispatcher. The
// optional vector distinguishes no BYTES frame from a present zero-byte frame.
class TriggerIngressItem final {
public:
    TriggerIngressItem(const TriggerIngressItem&) = delete;
    TriggerIngressItem& operator=(const TriggerIngressItem&) = delete;
    TriggerIngressItem(TriggerIngressItem&&) noexcept = default;
    TriggerIngressItem& operator=(TriggerIngressItem&&) noexcept = default;

    [[nodiscard]] const CommandEnvelope& envelope() const noexcept { return envelope_; }
    [[nodiscard]] bool has_binary() const noexcept { return binary_.has_value(); }
    [[nodiscard]] const std::optional<std::vector<std::byte>>& binary() const noexcept
    {
        return binary_;
    }
    [[nodiscard]] const BuildAdmissionResult& build_admission() const noexcept
    {
        return build_admission_;
    }
    [[nodiscard]] const CommandAdmission& admission() const noexcept
    {
        return build_admission_.admission;
    }
    [[nodiscard]] const baas::service::trigger::TriggerCommandDescriptor&
    descriptor() const noexcept
    {
        return *descriptor_;
    }
    // The admission was derived from this item's immutable catalog descriptor,
    // so transports do not need to reconstruct or choose response policy.
    [[nodiscard]] AdmissionResult admit_to(TriggerSession& session) const;

private:
    TriggerIngressItem(
        CommandEnvelope envelope,
        std::optional<std::vector<std::byte>> binary,
        BuildAdmissionResult build_admission,
        const baas::service::trigger::TriggerCommandDescriptor* descriptor
    );

    friend class TriggerIngress;

    CommandEnvelope envelope_;
    std::optional<std::vector<std::byte>> binary_;
    BuildAdmissionResult build_admission_;
    const baas::service::trigger::TriggerCommandDescriptor* descriptor_{};
};

// Serial, transport-independent ingress for exactly one command at a time. A
// transport maps text/JSON and binary frames to the two receive methods. This
// Catalog policy is resolved before a command can become ready. The class
// performs no I/O, automatic session mutation, dispatch, or execution.
class TriggerIngress final {
public:
    explicit TriggerIngress(TriggerIngressLimits limits = {});

    TriggerIngress(const TriggerIngress&) = delete;
    TriggerIngress& operator=(const TriggerIngress&) = delete;
    TriggerIngress(TriggerIngress&&) = delete;
    TriggerIngress& operator=(TriggerIngress&&) = delete;

    [[nodiscard]] TriggerIngressResult receive_json_frame(std::string_view json);
    [[nodiscard]] TriggerIngressResult receive_binary_frame(
        std::span<const std::byte> binary
    );

    [[nodiscard]] std::optional<TriggerIngressItem> take_ready();

    // reset drops a pending or completed item but never reopens a closed ingress.
    void reset() noexcept;
    // close is permanent and drops all frame state and completed input.
    void close() noexcept;

    [[nodiscard]] TriggerIngressState state() const noexcept;
    [[nodiscard]] const TriggerIngressLimits& limits() const noexcept { return limits_; }

private:
    struct PendingBinary {
        CommandEnvelope envelope;
        std::size_t json_frame_bytes{};
        ResponseMode response_mode{ResponseMode::single};
        const baas::service::trigger::TriggerCommandDescriptor* descriptor{};
    };

    [[nodiscard]] TriggerIngressResult complete(
        CommandEnvelope envelope,
        std::optional<std::vector<std::byte>> binary,
        ResponseMode response_mode,
        const baas::service::trigger::TriggerCommandDescriptor* descriptor
    );
    [[nodiscard]] static TriggerIngressResult reject(
        TriggerIngressError error,
        EnvelopeError envelope_error = EnvelopeError::none,
        std::size_t error_offset = 0
    ) noexcept;

    TriggerIngressLimits limits_;
    std::optional<PendingBinary> pending_binary_;
    std::optional<TriggerIngressItem> ready_;
    bool closed_{};
};

}  // namespace baas::service::protocol::trigger
