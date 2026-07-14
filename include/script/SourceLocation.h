#pragma once

#include <cstddef>

namespace baas::script {

// A position in the original UTF-8 source. Offsets are byte-based while lines
// and columns are one-based. Columns count Unicode scalar values, not bytes.
struct SourceLocation {
    std::size_t byte_offset{0};
    std::size_t line{1};
    std::size_t column{1};

    friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
};

// Half-open source range [begin, end).
struct SourceSpan {
    SourceLocation begin{};
    SourceLocation end{};

    friend bool operator==(const SourceSpan&, const SourceSpan&) = default;
};

}  // namespace baas::script
