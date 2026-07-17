#include "script/runtime/ErrorTranslation.h"

namespace baas::script::runtime {

LanguageErrorDescriptor translate_runtime_error_code(const RuntimeErrorCode code) noexcept
{
    using enum RuntimeErrorCode;
    switch (code) {
        case TypeMismatch: return {LanguageErrorCode::TypeMismatch};
        case CrossHeapReference:
        case StaleReference:
        case CellKindMismatch:
        case HeapTornDown:
        case HeapBusy: return {LanguageErrorCode::InternalInvariant};
        case MemoryLimitExceeded:
        case CellLimitExceeded:
        case SingleAllocationExceeded:
        case StringLimitExceeded:
        case ExternalMemoryLimitExceeded:
        case CollectionWorkLimitExceeded: return {LanguageErrorCode::MemoryLimitExceeded};
        case InvalidUtf8: return {LanguageErrorCode::InvalidUtf8};
        case JsonCycle: return {LanguageErrorCode::JsonCycle};
        case JsonNonFinite: return {LanguageErrorCode::JsonNonFinite};
        case JsonUnsupported: return {LanguageErrorCode::JsonUnsupported};
        case IndexOutOfRange: return {LanguageErrorCode::IndexOutOfRange};
        case ReleaseQueueLimitExceeded: return {LanguageErrorCode::CleanupLimitExceeded};
        case JsonDepthLimitExceeded:
        case JsonNodeLimitExceeded:
        case JsonStringLimitExceeded:
        case JsonByteLimitExceeded:
        case JsonWorkLimitExceeded: return {LanguageErrorCode::JsonLimitExceeded};
        case JsonDuplicateKey: return {LanguageErrorCode::JsonDuplicateKey};
    }
    return {LanguageErrorCode::InternalInvariant};
}

}  // namespace baas::script::runtime
