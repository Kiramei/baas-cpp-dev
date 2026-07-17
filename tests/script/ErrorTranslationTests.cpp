#include "script/runtime/ErrorTranslation.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace runtime = baas::script::runtime;

namespace {

struct Expected {
    runtime::RuntimeErrorCode runtime_code;
    std::string_view language_code;
    bool catchable;
};

constexpr std::array expected{
    Expected{runtime::RuntimeErrorCode::TypeMismatch, "TypeMismatch", true},
    Expected{runtime::RuntimeErrorCode::CrossHeapReference, "InternalInvariant", false},
    Expected{runtime::RuntimeErrorCode::StaleReference, "InternalInvariant", false},
    Expected{runtime::RuntimeErrorCode::CellKindMismatch, "InternalInvariant", false},
    Expected{runtime::RuntimeErrorCode::MemoryLimitExceeded, "MemoryLimitExceeded", false},
    Expected{runtime::RuntimeErrorCode::CellLimitExceeded, "MemoryLimitExceeded", false},
    Expected{runtime::RuntimeErrorCode::SingleAllocationExceeded, "MemoryLimitExceeded", false},
    Expected{runtime::RuntimeErrorCode::StringLimitExceeded, "MemoryLimitExceeded", false},
    Expected{runtime::RuntimeErrorCode::ExternalMemoryLimitExceeded, "MemoryLimitExceeded", false},
    Expected{runtime::RuntimeErrorCode::CollectionWorkLimitExceeded, "MemoryLimitExceeded", false},
    Expected{runtime::RuntimeErrorCode::InvalidUtf8, "InvalidUtf8", true},
    Expected{runtime::RuntimeErrorCode::JsonCycle, "JsonCycle", true},
    Expected{runtime::RuntimeErrorCode::JsonNonFinite, "JsonNonFinite", true},
    Expected{runtime::RuntimeErrorCode::JsonUnsupported, "JsonUnsupported", true},
    Expected{runtime::RuntimeErrorCode::HeapTornDown, "InternalInvariant", false},
    Expected{runtime::RuntimeErrorCode::IndexOutOfRange, "IndexOutOfRange", true},
    Expected{runtime::RuntimeErrorCode::ReleaseQueueLimitExceeded, "CleanupLimitExceeded", false},
    Expected{runtime::RuntimeErrorCode::JsonDepthLimitExceeded, "JsonLimitExceeded", true},
    Expected{runtime::RuntimeErrorCode::JsonNodeLimitExceeded, "JsonLimitExceeded", true},
    Expected{runtime::RuntimeErrorCode::JsonStringLimitExceeded, "JsonLimitExceeded", true},
    Expected{runtime::RuntimeErrorCode::JsonByteLimitExceeded, "JsonLimitExceeded", true},
    Expected{runtime::RuntimeErrorCode::JsonWorkLimitExceeded, "JsonLimitExceeded", true},
    Expected{runtime::RuntimeErrorCode::JsonDuplicateKey, "JsonDuplicateKey", true},
    Expected{runtime::RuntimeErrorCode::HeapBusy, "InternalInvariant", false},
};

}  // namespace

int main()
{
    for (const auto& item : expected) {
        const auto actual = runtime::translate_runtime_error_code(item.runtime_code);
        if (actual.name() != item.language_code || actual.catchable() != item.catchable) {
            std::cerr << "FAIL: " << runtime::runtime_error_code_name(item.runtime_code)
                      << " mapped to " << actual.name() << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "All " << expected.size() << " runtime error mappings passed\n";
    return EXIT_SUCCESS;
}
