#include "script/host/ProcedureSnapshot.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <set>
#include <utility>

namespace baas::script::host {
namespace {

[[nodiscard]] bool valid_limits(const ProcedureSnapshotLimits& limits) noexcept
{
    return limits.max_procedures != 0 && limits.max_terminals_per_procedure != 0 &&
        limits.max_effects_per_procedure != 0 && limits.max_resources_per_procedure != 0 &&
        limits.max_string_bytes != 0 && limits.max_total_string_bytes != 0 &&
        limits.max_validation_work != 0 && limits.max_effects_per_procedure <= 5;
}

[[nodiscard]] bool lowercase_ascii_path(
    const std::string_view value, const std::size_t max_bytes) noexcept
{
    if (value.empty() || value.size() > max_bytes || value.front() == '/' ||
        value.back() == '/' || value.find("//") != std::string_view::npos ||
        value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos) return false;
    std::size_t begin{};
    while (begin < value.size()) {
        const auto end = value.find('/', begin);
        const auto segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        if (segment.empty() || segment == "." || segment == ".." ||
            segment.front() == '-' || segment.back() == '-' ||
            segment.front() == '.' || segment.back() == '.') return false;
        for (const auto character : segment) {
            if (!(character >= 'a' && character <= 'z') &&
                !(character >= '0' && character <= '9') && character != '-' &&
                character != '_' && character != '.') return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return true;
}

[[nodiscard]] bool valid_digest(const std::string_view value) noexcept
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](const char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    });
}

[[nodiscard]] bool valid_effect(const ProcedureEffect effect) noexcept
{
    return effect >= ProcedureEffect::Capture && effect <= ProcedureEffect::ForegroundCheck;
}

[[nodiscard]] std::string ascii_fold(std::string_view value)
{
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    });
    return result;
}

void add_length_prefixed(std::string& target, const std::string_view value)
{
    target += std::to_string(value.size());
    target.push_back(':');
    target.append(value);
    target.push_back(';');
}

struct CanonicalDescriptor {
    ProcedureDescriptorInput value;
    std::size_t string_bytes{};
    std::size_t work{};
};

[[nodiscard]] CanonicalDescriptor canonicalize(
    const ProcedureDescriptorInput& source, const ProcedureSnapshotLimits& limits,
    const bool check_digest)
{
    if (!valid_limits(limits))
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::InvalidLimits, "procedure limits must be non-zero");
    CanonicalDescriptor result{source};
    auto add_string = [&](const std::string_view value) {
        if (value.size() > limits.max_string_bytes ||
            value.size() > limits.max_total_string_bytes -
                std::min(result.string_bytes, limits.max_total_string_bytes))
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::StringLimitExceeded,
                "procedure descriptor string budget exceeded");
        result.string_bytes += value.size();
        if (++result.work > limits.max_validation_work)
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::WorkLimitExceeded,
                "procedure descriptor validation work exceeded");
    };

    add_string(result.value.procedure_id);
    if (!valid_procedure_id(result.value.procedure_id, limits.max_string_bytes))
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::InvalidProcedureId,
            "procedure id is not a canonical lowercase logical id");
    if (result.value.terminal_ids.empty() ||
        result.value.terminal_ids.size() > limits.max_terminals_per_procedure)
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::TerminalLimitExceeded,
            "procedure terminal limit exceeded");
    std::set<std::string, std::less<>> terminals;
    for (const auto& terminal : result.value.terminal_ids) {
        add_string(terminal);
        if (!valid_procedure_terminal_id(terminal, limits.max_string_bytes))
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::InvalidTerminalId,
                "procedure terminal id is not canonical");
        if (!terminals.insert(terminal).second)
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::DuplicateTerminal,
                "duplicate procedure terminal id");
    }

    std::sort(result.value.declared_effects.begin(), result.value.declared_effects.end());
    for (const auto effect : result.value.declared_effects) {
        if (++result.work > limits.max_validation_work)
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::WorkLimitExceeded,
                "procedure descriptor validation work exceeded");
        if (!valid_effect(effect))
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::InvalidEffect, "invalid procedure effect");
    }
    if (std::adjacent_find(result.value.declared_effects.begin(),
                           result.value.declared_effects.end()) !=
        result.value.declared_effects.end())
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::DuplicateEffect, "duplicate procedure effect");
    if (result.value.declared_effects.size() > limits.max_effects_per_procedure)
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::EffectLimitExceeded,
            "procedure effect limit exceeded");

    if (result.value.resource_ids.size() > limits.max_resources_per_procedure)
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::ResourceLimitExceeded,
            "procedure resource limit exceeded");
    std::sort(result.value.resource_ids.begin(), result.value.resource_ids.end());
    for (const auto& resource : result.value.resource_ids) {
        add_string(resource);
        if (!resources::valid_resource_id(resource, limits.max_string_bytes))
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::InvalidResourceId,
                "procedure resource id is not canonical");
    }
    if (std::adjacent_find(result.value.resource_ids.begin(),
                           result.value.resource_ids.end()) != result.value.resource_ids.end())
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::DuplicateResource, "duplicate procedure resource id");

    if (check_digest) {
        add_string(result.value.sha256);
        if (!valid_digest(result.value.sha256))
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::InvalidDigest,
                "procedure descriptor digest is not lowercase SHA-256");
    }
    return result;
}

[[nodiscard]] std::string digest_canonical(const ProcedureDescriptorInput& value)
{
    std::string material;
    add_length_prefixed(material, "baas.procedure.descriptor/v1");
    add_length_prefixed(material, value.procedure_id);
    add_length_prefixed(material, std::to_string(value.terminal_ids.size()));
    for (const auto& terminal : value.terminal_ids) add_length_prefixed(material, terminal);
    add_length_prefixed(material, std::to_string(value.declared_effects.size()));
    for (const auto effect : value.declared_effects)
        add_length_prefixed(material, procedure_effect_name(effect));
    add_length_prefixed(material, std::to_string(value.resource_ids.size()));
    for (const auto& resource : value.resource_ids) add_length_prefixed(material, resource);
    return resources::sha256_hex(std::as_bytes(std::span(material)));
}

}  // namespace

struct ProcedureSnapshot::Impl {
    ProcedureSnapshotLimits limits;
    std::shared_ptr<const resources::ResourceSnapshot> resources;
    std::string snapshot_id;
    std::uint64_t numeric_snapshot_id{};
    std::map<std::string, std::shared_ptr<const ProcedureDescriptor>, std::less<>> descriptors;
};

std::string_view procedure_effect_name(const ProcedureEffect effect) noexcept
{
    switch (effect) {
        case ProcedureEffect::Capture: return "capture";
        case ProcedureEffect::Vision: return "vision";
        case ProcedureEffect::Input: return "input";
        case ProcedureEffect::Wait: return "wait";
        case ProcedureEffect::ForegroundCheck: return "foreground_check";
    }
    return "invalid";
}

std::string_view procedure_snapshot_error_code_name(
    const ProcedureSnapshotErrorCode code) noexcept
{
    using enum ProcedureSnapshotErrorCode;
    switch (code) {
        case InvalidLimits: return "PRC001_INVALID_LIMITS";
        case ProcedureLimitExceeded: return "PRC002_PROCEDURE_LIMIT_EXCEEDED";
        case TerminalLimitExceeded: return "PRC003_TERMINAL_LIMIT_EXCEEDED";
        case EffectLimitExceeded: return "PRC004_EFFECT_LIMIT_EXCEEDED";
        case ResourceLimitExceeded: return "PRC005_RESOURCE_LIMIT_EXCEEDED";
        case StringLimitExceeded: return "PRC006_STRING_LIMIT_EXCEEDED";
        case WorkLimitExceeded: return "PRC007_WORK_LIMIT_EXCEEDED";
        case InvalidProcedureId: return "PRC008_INVALID_PROCEDURE_ID";
        case InvalidTerminalId: return "PRC009_INVALID_TERMINAL_ID";
        case InvalidEffect: return "PRC010_INVALID_EFFECT";
        case InvalidResourceId: return "PRC011_INVALID_RESOURCE_ID";
        case InvalidDigest: return "PRC012_INVALID_DIGEST";
        case DigestMismatch: return "PRC013_DIGEST_MISMATCH";
        case DuplicateProcedure: return "PRC014_DUPLICATE_PROCEDURE";
        case ProcedureIdCaseCollision: return "PRC015_PROCEDURE_ID_CASE_COLLISION";
        case DuplicateTerminal: return "PRC016_DUPLICATE_TERMINAL";
        case DuplicateEffect: return "PRC017_DUPLICATE_EFFECT";
        case DuplicateResource: return "PRC018_DUPLICATE_RESOURCE";
        case ResourceNotFound: return "PRC019_RESOURCE_NOT_FOUND";
        case ResourceSnapshotAbsent: return "PRC020_RESOURCE_SNAPSHOT_ABSENT";
    }
    return "PRC000_UNKNOWN";
}

ProcedureSnapshotError::ProcedureSnapshotError(
    const ProcedureSnapshotErrorCode code, std::string message)
    : std::runtime_error(
          std::string(procedure_snapshot_error_code_name(code)) + ": " + std::move(message)),
      code_(code)
{
}

bool valid_procedure_id(const std::string_view value, const std::size_t max_bytes) noexcept
{
    return lowercase_ascii_path(value, max_bytes);
}

bool valid_procedure_terminal_id(
    const std::string_view value, const std::size_t max_bytes) noexcept
{
    if (value.empty() || value.size() > max_bytes ||
        !(value.front() >= 'a' && value.front() <= 'z')) return false;
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
    });
}

std::string procedure_descriptor_sha256(
    const ProcedureDescriptorInput& descriptor, const ProcedureSnapshotLimits limits)
{
    auto canonical = canonicalize(descriptor, limits, false);
    return digest_canonical(canonical.value);
}

ProcedureDescriptor::ProcedureDescriptor(ProcedureDescriptorInput input)
    : input_(std::move(input))
{
}

const std::string& ProcedureDescriptor::procedure_id() const noexcept
{
    return input_.procedure_id;
}

std::span<const std::string> ProcedureDescriptor::terminal_ids() const noexcept
{
    return input_.terminal_ids;
}

std::span<const ProcedureEffect> ProcedureDescriptor::declared_effects() const noexcept
{
    return input_.declared_effects;
}

std::span<const std::string> ProcedureDescriptor::resource_ids() const noexcept
{
    return input_.resource_ids;
}

const std::string& ProcedureDescriptor::sha256() const noexcept { return input_.sha256; }

bool ProcedureDescriptor::accepts_terminal(const std::string_view terminal_id) const noexcept
{
    return std::find(input_.terminal_ids.begin(), input_.terminal_ids.end(), terminal_id) !=
        input_.terminal_ids.end();
}

bool ProcedureDescriptor::declares_effect(const ProcedureEffect effect) const noexcept
{
    return std::binary_search(
        input_.declared_effects.begin(), input_.declared_effects.end(), effect);
}

ProcedureSnapshot::ProcedureSnapshot(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

ProcedureSnapshot::~ProcedureSnapshot() = default;

std::shared_ptr<const ProcedureSnapshot> ProcedureSnapshot::build(
    std::vector<ProcedureDescriptorInput> descriptors,
    std::shared_ptr<const resources::ResourceSnapshot> resources,
    const ProcedureSnapshotLimits limits)
{
    if (!resources)
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::ResourceSnapshotAbsent,
            "procedure snapshot requires an external immutable resource snapshot");
    if (!valid_limits(limits))
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::InvalidLimits, "procedure limits must be non-zero");
    if (descriptors.empty() || descriptors.size() > limits.max_procedures)
        throw ProcedureSnapshotError(
            ProcedureSnapshotErrorCode::ProcedureLimitExceeded,
            "procedure descriptor limit exceeded");

    // Detect case aliases before canonical lowercase validation so manifests
    // cannot hide the collision behind whichever entry happens to validate first.
    std::map<std::string, std::string, std::less<>> folded_ids;
    for (const auto& descriptor : descriptors) {
        const auto folded = ascii_fold(descriptor.procedure_id);
        const auto [found, inserted] = folded_ids.emplace(folded, descriptor.procedure_id);
        if (!inserted && found->second != descriptor.procedure_id)
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::ProcedureIdCaseCollision,
                "procedure ids collide under ASCII case folding");
    }

    auto impl = std::make_unique<Impl>();
    impl->limits = limits;
    impl->resources = std::move(resources);
    std::sort(descriptors.begin(), descriptors.end(), [](const auto& left, const auto& right) {
        return left.procedure_id < right.procedure_id;
    });

    std::size_t total_strings{};
    std::size_t total_work{};
    std::string identity;
    add_length_prefixed(identity, "baas.procedure.snapshot/v1");
    add_length_prefixed(identity, impl->resources->snapshot_id());
    add_length_prefixed(identity, std::to_string(descriptors.size()));
    std::string previous;
    for (const auto& source : descriptors) {
        if (!previous.empty() && previous == source.procedure_id)
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::DuplicateProcedure, "duplicate procedure id");
        previous = source.procedure_id;
        auto canonical = canonicalize(source, limits, true);
        if (canonical.string_bytes > limits.max_total_string_bytes -
                std::min(total_strings, limits.max_total_string_bytes))
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::StringLimitExceeded,
                "procedure snapshot aggregate string budget exceeded");
        total_strings += canonical.string_bytes;
        if (canonical.work > limits.max_validation_work -
                std::min(total_work, limits.max_validation_work))
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::WorkLimitExceeded,
                "procedure snapshot aggregate validation work exceeded");
        total_work += canonical.work;
        const auto computed = digest_canonical(canonical.value);
        if (computed != canonical.value.sha256)
            throw ProcedureSnapshotError(
                ProcedureSnapshotErrorCode::DigestMismatch,
                "procedure descriptor digest does not match its logical fields");
        for (const auto& resource_id : canonical.value.resource_ids) {
            if (!impl->resources->resolve(resource_id))
                throw ProcedureSnapshotError(
                    ProcedureSnapshotErrorCode::ResourceNotFound,
                    "procedure resource is absent from the external snapshot");
        }
        add_length_prefixed(identity, canonical.value.procedure_id);
        add_length_prefixed(identity, canonical.value.sha256);
        auto descriptor = std::shared_ptr<const ProcedureDescriptor>(
            new ProcedureDescriptor(std::move(canonical.value)));
        impl->descriptors.emplace(descriptor->procedure_id(), std::move(descriptor));
    }
    impl->snapshot_id = resources::sha256_hex(std::as_bytes(std::span(identity)));
    const auto prefix = std::string_view(impl->snapshot_id).substr(0, 16);
    const auto [end, error] = std::from_chars(
        prefix.data(), prefix.data() + prefix.size(), impl->numeric_snapshot_id, 16);
    if (error != std::errc{} || end != prefix.data() + prefix.size() ||
        impl->numeric_snapshot_id == 0) impl->numeric_snapshot_id = 1;
    return std::shared_ptr<const ProcedureSnapshot>(
        new ProcedureSnapshot(std::move(impl)));
}

std::shared_ptr<const ProcedureDescriptor> ProcedureSnapshot::resolve(
    const std::string_view procedure_id) const noexcept
{
    if (!accepts_procedure_id(procedure_id)) return {};
    const auto found = impl_->descriptors.find(procedure_id);
    return found == impl_->descriptors.end() ? nullptr : found->second;
}

bool ProcedureSnapshot::accepts_procedure_id(
    const std::string_view procedure_id) const noexcept
{
    return valid_procedure_id(procedure_id, impl_->limits.max_string_bytes);
}

const std::shared_ptr<const resources::ResourceSnapshot>&
ProcedureSnapshot::resource_snapshot() const noexcept
{
    return impl_->resources;
}

const std::string& ProcedureSnapshot::snapshot_id() const noexcept
{
    return impl_->snapshot_id;
}

std::uint64_t ProcedureSnapshot::numeric_snapshot_id() const noexcept
{
    return impl_->numeric_snapshot_id;
}

std::size_t ProcedureSnapshot::procedure_count() const noexcept
{
    return impl_->descriptors.size();
}

}  // namespace baas::script::host
