#include "script/runtime/ModuleSpecifier.h"

#include <cstdint>
#include <utility>

namespace baas::script::runtime {
namespace {

[[nodiscard]] bool is_valid_utf8(const std::string_view value) noexcept
{
    std::size_t index = 0;
    while (index < value.size()) {
        const auto lead = static_cast<unsigned char>(value[index]);
        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        if (lead <= 0x7F) {
            ++index;
            continue;
        }
        if (lead >= 0xC2 && lead <= 0xDF) {
            continuation_count = 1;
            code_point = lead & 0x1F;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            continuation_count = 2;
            code_point = lead & 0x0F;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            continuation_count = 3;
            code_point = lead & 0x07;
        } else {
            return false;
        }
        if (continuation_count > value.size() - index - 1) return false;
        for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0) != 0x80) return false;
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if ((continuation_count == 2 && code_point < 0x800)
            || (continuation_count == 3 && code_point < 0x10000)
            || code_point > 0x10FFFF
            || (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
        index += continuation_count + 1;
    }
    return true;
}

[[nodiscard]] bool contains_non_ascii(const std::string_view value) noexcept
{
    for (const char byte : value) {
        if (static_cast<unsigned char>(byte) >= 0x80) return true;
    }
    return false;
}

[[nodiscard]] bool is_ascii_alpha(const char value) noexcept
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[noreturn]] void fail(
    const ModuleSpecifierErrorCode code,
    const std::string_view message)
{
    throw ModuleSpecifierError(code, std::string(message));
}

}  // namespace

std::string_view module_specifier_error_code_name(
    const ModuleSpecifierErrorCode code) noexcept
{
    using enum ModuleSpecifierErrorCode;
    switch (code) {
        case Empty: return "MS001_EMPTY";
        case ByteLimitExceeded: return "MS002_BYTE_LIMIT_EXCEEDED";
        case SegmentLimitExceeded: return "MS003_SEGMENT_LIMIT_EXCEEDED";
        case InvalidUtf8: return "MS004_INVALID_UTF8";
        case NfcCheckUnavailable: return "MS005_NFC_CHECK_UNAVAILABLE";
        case NotNfc: return "MS006_NOT_NFC";
        case LeadingSlash: return "MS007_LEADING_SLASH";
        case DrivePrefix: return "MS008_DRIVE_PREFIX";
        case Backslash: return "MS009_BACKSLASH";
        case EmbeddedNul: return "MS010_EMBEDDED_NUL";
        case EmptySegment: return "MS011_EMPTY_SEGMENT";
        case DotSegment: return "MS012_DOT_SEGMENT";
        case SourceExtension: return "MS013_SOURCE_EXTENSION";
    }
    return "MS000_UNKNOWN";
}

ModuleSpecifierError::ModuleSpecifierError(
    const ModuleSpecifierErrorCode code,
    std::string message)
    : std::runtime_error(std::move(message)), code_(code)
{
}

std::string ModuleSpecifier::manifest_source_path() const
{
    if (kind == ModuleKind::Host) {
        throw std::logic_error("host modules do not have package manifest paths");
    }
    return canonical_id + ".baas";
}

ModuleSpecifier validate_module_specifier(
    const std::string_view specifier,
    const NfcPredicate is_nfc,
    const ModuleSpecifierLimits limits)
{
    using enum ModuleSpecifierErrorCode;
    if (limits.max_bytes == 0 || limits.max_segments == 0) {
        throw std::invalid_argument("module specifier limits must be positive");
    }
    if (specifier.empty()) fail(Empty, "module specifier is empty");
    if (specifier.size() > limits.max_bytes) {
        fail(ByteLimitExceeded, "module specifier exceeds byte limit");
    }
    if (!is_valid_utf8(specifier)) fail(InvalidUtf8, "module specifier is not valid UTF-8");
    if (specifier.find('\0') != std::string_view::npos) {
        fail(EmbeddedNul, "module specifier contains NUL");
    }
    if (contains_non_ascii(specifier)) {
        if (is_nfc == nullptr) {
            fail(NfcCheckUnavailable, "non-ASCII module specifier requires an NFC validator");
        }
        if (!is_nfc(specifier)) fail(NotNfc, "module specifier is not NFC");
    }
    if (specifier.front() == '/') fail(LeadingSlash, "module specifier has a leading slash");
    if (specifier.size() >= 2 && is_ascii_alpha(specifier[0]) && specifier[1] == ':') {
        fail(DrivePrefix, "module specifier has a drive prefix");
    }
    if (specifier.find('\\') != std::string_view::npos) {
        fail(Backslash, "module specifier contains a backslash");
    }

    std::size_t segment_count = 0;
    std::size_t begin = 0;
    while (begin <= specifier.size()) {
        const auto end = specifier.find('/', begin);
        const auto stop = end == std::string_view::npos ? specifier.size() : end;
        const auto segment = specifier.substr(begin, stop - begin);
        if (segment.empty()) fail(EmptySegment, "module specifier contains an empty segment");
        if (segment == "." || segment == "..") {
            fail(DotSegment, "module specifier contains a dot segment");
        }
        ++segment_count;
        if (segment_count > limits.max_segments) {
            fail(SegmentLimitExceeded, "module specifier exceeds segment limit");
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }

    if (specifier.size() >= 5 && specifier.ends_with(".baas")) {
        fail(SourceExtension, "module specifier must be extensionless");
    }
    const auto kind = specifier.starts_with("baas/") ? ModuleKind::Host : ModuleKind::Package;
    return ModuleSpecifier{kind, std::string(specifier)};
}

}  // namespace baas::script::runtime
