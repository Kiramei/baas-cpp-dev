#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace baas::script::runtime {

enum class ModuleKind { Package, Host };

enum class ModuleSpecifierErrorCode {
    Empty,
    ByteLimitExceeded,
    SegmentLimitExceeded,
    InvalidUtf8,
    NfcCheckUnavailable,
    NotNfc,
    LeadingSlash,
    DrivePrefix,
    Backslash,
    EmbeddedNul,
    EmptySegment,
    DotSegment,
    SourceExtension,
};

[[nodiscard]] std::string_view module_specifier_error_code_name(
    ModuleSpecifierErrorCode code) noexcept;

class ModuleSpecifierError final : public std::runtime_error {
public:
    ModuleSpecifierError(ModuleSpecifierErrorCode code, std::string message);
    [[nodiscard]] ModuleSpecifierErrorCode code() const noexcept { return code_; }

private:
    ModuleSpecifierErrorCode code_;
};

// ASCII is already NFC. Non-ASCII specifiers fail closed unless the embedding
// runtime supplies its platform-independent NFC implementation here.
using NfcPredicate = bool (*)(std::string_view) noexcept;

struct ModuleSpecifierLimits {
    std::size_t max_bytes{1'024};
    std::size_t max_segments{128};
};

struct ModuleSpecifier {
    ModuleKind kind{ModuleKind::Package};
    std::string canonical_id;

    [[nodiscard]] std::string manifest_source_path() const;
    friend bool operator==(const ModuleSpecifier&, const ModuleSpecifier&) = default;
};

// This function performs no filesystem access and never case-folds. Its output
// is byte-for-byte identical to the accepted source specifier.
[[nodiscard]] ModuleSpecifier validate_module_specifier(
    std::string_view specifier,
    NfcPredicate is_nfc = nullptr,
    ModuleSpecifierLimits limits = {});

}  // namespace baas::script::runtime
