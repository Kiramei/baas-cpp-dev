#include "script/runtime/ErrorTranslation.h"

namespace baas::script::runtime {

LanguageErrorDescriptor translate_runtime_error_code(const RuntimeErrorCode code) noexcept
{
    using enum RuntimeErrorCode;
    switch (code) {
        case TypeMismatch: return {"TypeMismatch", true};
        case CrossHeapReference:
        case StaleReference:
        case CellKindMismatch:
        case HeapTornDown: return {"InternalInvariant", false};
        case MemoryLimitExceeded:
        case CellLimitExceeded:
        case SingleAllocationExceeded:
        case StringLimitExceeded:
        case ExternalMemoryLimitExceeded:
        case CollectionWorkLimitExceeded: return {"MemoryLimitExceeded", false};
        case InvalidUtf8: return {"InvalidUtf8", true};
        case JsonCycle: return {"JsonCycle", true};
        case JsonNonFinite: return {"JsonNonFinite", true};
        case JsonUnsupported: return {"JsonUnsupported", true};
        case IndexOutOfRange: return {"IndexOutOfRange", true};
        case ReleaseQueueLimitExceeded: return {"CleanupLimitExceeded", false};
        case JsonDepthLimitExceeded:
        case JsonNodeLimitExceeded:
        case JsonStringLimitExceeded:
        case JsonByteLimitExceeded:
        case JsonWorkLimitExceeded: return {"JsonLimitExceeded", true};
        case JsonDuplicateKey: return {"JsonDuplicateKey", true};
    }
    return {"InternalInvariant", false};
}

}  // namespace baas::script::runtime
