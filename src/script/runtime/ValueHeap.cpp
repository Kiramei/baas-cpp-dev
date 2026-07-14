#include "script/runtime/ValueHeap.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace baas::script::runtime {
namespace {

// This counter allocates opaque identities only; it owns no heap/cell/runtime
// state and does not couple collection or budgets between contexts.
std::atomic<std::uint64_t> next_heap_identity{1};

struct ErrorCodeDescriptor {
    LanguageErrorCode code;
    std::string_view name;
    bool catchable;
};

constexpr std::array error_code_descriptors{
    ErrorCodeDescriptor{LanguageErrorCode::ThrownValue, "ThrownValue", true},
    ErrorCodeDescriptor{LanguageErrorCode::TypeMismatch, "TypeMismatch", true},
    ErrorCodeDescriptor{LanguageErrorCode::ArgumentInvalid, "ArgumentInvalid", true},
    ErrorCodeDescriptor{LanguageErrorCode::NameNotFound, "NameNotFound", true},
    ErrorCodeDescriptor{LanguageErrorCode::UninitializedBinding, "UninitializedBinding", true},
    ErrorCodeDescriptor{LanguageErrorCode::NotCallable, "NotCallable", true},
    ErrorCodeDescriptor{LanguageErrorCode::CallArityMismatch, "CallArityMismatch", true},
    ErrorCodeDescriptor{LanguageErrorCode::CallArgumentDuplicate, "CallArgumentDuplicate", true},
    ErrorCodeDescriptor{LanguageErrorCode::CallArgumentUnknown, "CallArgumentUnknown", true},
    ErrorCodeDescriptor{LanguageErrorCode::TaskCycle, "TaskCycle", true},
    ErrorCodeDescriptor{LanguageErrorCode::IndexOutOfRange, "IndexOutOfRange", true},
    ErrorCodeDescriptor{LanguageErrorCode::NumericOverflow, "NumericOverflow", true},
    ErrorCodeDescriptor{LanguageErrorCode::DivisionByZero, "DivisionByZero", true},
    ErrorCodeDescriptor{LanguageErrorCode::InvalidUtf8, "InvalidUtf8", true},
    ErrorCodeDescriptor{LanguageErrorCode::JsonCycle, "JsonCycle", true},
    ErrorCodeDescriptor{LanguageErrorCode::JsonNonFinite, "JsonNonFinite", true},
    ErrorCodeDescriptor{LanguageErrorCode::JsonUnsupported, "JsonUnsupported", true},
    ErrorCodeDescriptor{LanguageErrorCode::JsonDuplicateKey, "JsonDuplicateKey", true},
    ErrorCodeDescriptor{LanguageErrorCode::JsonLimitExceeded, "JsonLimitExceeded", true},
    ErrorCodeDescriptor{LanguageErrorCode::ImportSpecifierInvalid, "ImportSpecifierInvalid", true},
    ErrorCodeDescriptor{LanguageErrorCode::ImportCycle, "ImportCycle", true},
    ErrorCodeDescriptor{LanguageErrorCode::ImportDepthLimit, "ImportDepthLimit", true},
    ErrorCodeDescriptor{LanguageErrorCode::ModuleInitializationFailed, "ModuleInitializationFailed", true},
    ErrorCodeDescriptor{LanguageErrorCode::ModuleMemberMissing, "ModuleMemberMissing", true},
    ErrorCodeDescriptor{LanguageErrorCode::CapabilityDenied, "CapabilityDenied", true},
    ErrorCodeDescriptor{LanguageErrorCode::HostValidationFailed, "HostValidationFailed", true},
    ErrorCodeDescriptor{LanguageErrorCode::HostUnavailable, "HostUnavailable", true},
    ErrorCodeDescriptor{LanguageErrorCode::DeviceDisconnected, "DeviceDisconnected", true},
    ErrorCodeDescriptor{LanguageErrorCode::PackageMismatch, "PackageMismatch", true},
    ErrorCodeDescriptor{LanguageErrorCode::OcrModelUnavailable, "OcrModelUnavailable", true},
    ErrorCodeDescriptor{LanguageErrorCode::ResourceMissing, "ResourceMissing", true},
    ErrorCodeDescriptor{LanguageErrorCode::Timeout, "Timeout", true},
    ErrorCodeDescriptor{LanguageErrorCode::HostInternal, "HostInternal", true},
    ErrorCodeDescriptor{LanguageErrorCode::Cancelled, "Cancelled", false},
    ErrorCodeDescriptor{LanguageErrorCode::HumanTakeover, "HumanTakeover", false},
    ErrorCodeDescriptor{LanguageErrorCode::DeadlineExceeded, "DeadlineExceeded", false},
    ErrorCodeDescriptor{LanguageErrorCode::InstructionLimitExceeded, "InstructionLimitExceeded", false},
    ErrorCodeDescriptor{LanguageErrorCode::MemoryLimitExceeded, "MemoryLimitExceeded", false},
    ErrorCodeDescriptor{LanguageErrorCode::StackLimitExceeded, "StackLimitExceeded", false},
    ErrorCodeDescriptor{LanguageErrorCode::CleanupLimitExceeded, "CleanupLimitExceeded", false},
    ErrorCodeDescriptor{LanguageErrorCode::TaskLimitExceeded, "TaskLimitExceeded", false},
    ErrorCodeDescriptor{LanguageErrorCode::InternalInvariant, "InternalInvariant", false},
};

[[nodiscard]] std::uint64_t allocate_heap_identity()
{
    auto current = next_heap_identity.load(std::memory_order_relaxed);
    for (;;) {
        if (current == std::numeric_limits<std::uint64_t>::max()) {
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "heap identity space exhausted");
        }
        if (next_heap_identity.compare_exchange_weak(current, current + 1,
                                                     std::memory_order_relaxed)) return current;
    }
}

struct StringCell { std::string value; };
struct ListCell { std::vector<Value> values; };
struct MapCell { std::vector<std::pair<std::string, Value>> entries; };
struct FunctionCell { FunctionMetadata metadata; };
struct ModuleCell { ModuleMetadata metadata; };
struct ErrorCell { ErrorMetadata metadata; };
struct TaskCell { TaskMetadata metadata; };
struct HostCell { HostHandleMetadata metadata; };

using CellData = std::variant<StringCell, ListCell, MapCell, FunctionCell, ModuleCell,
                              ErrorCell, TaskCell, HostCell>;

struct Cell {
    explicit Cell(CellData value, const std::size_t bytes, const std::size_t strings,
                  const std::size_t external)
        : data(std::move(value)), accounted_bytes(bytes), accounted_string_bytes(strings),
          accounted_external_bytes(external) {}
    CellData data;
    std::size_t accounted_bytes{};
    std::size_t accounted_string_bytes{};
    std::size_t accounted_external_bytes{};
    bool marked{false};
};

struct Charge {
    std::size_t bytes{};
    std::size_t strings{};
    std::size_t external{};
};

[[nodiscard]] bool checked_add(const std::size_t left, const std::size_t right,
                               std::size_t& output) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(const std::size_t left, const std::size_t right,
                                    std::size_t& output) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) return false;
    output = left * right;
    return true;
}

void saturating_add(std::size_t& destination, const std::size_t value) noexcept
{
    if (value > std::numeric_limits<std::size_t>::max() - destination)
        destination = std::numeric_limits<std::size_t>::max();
    else
        destination += value;
}

void charged_add(std::size_t& destination, const std::size_t value)
{
    std::size_t result{};
    if (!checked_add(destination, value, result)) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "heap accounting overflow");
    }
    destination = result;
}

void add_source_charge(Charge& charge, const SourceReference& source)
{
    charged_add(charge.bytes, source.snapshot_id.capacity());
    charged_add(charge.strings, source.snapshot_id.capacity());
    charged_add(charge.bytes, source.module.capacity());
    charged_add(charge.strings, source.module.capacity());
}

void add_optional_source_charge(Charge& charge, const std::optional<SourceReference>& source)
{
    if (source) add_source_charge(charge, *source);
}

[[nodiscard]] std::size_t vector_bytes(const std::size_t capacity, const std::size_t item_size)
{
    std::size_t result{};
    if (!checked_multiply(capacity, item_size, result)) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "heap accounting overflow");
    }
    return result;
}

[[nodiscard]] Charge charge_for(const CellData& data)
{
    Charge charge{sizeof(Cell), 0, 0};
    const auto add_string = [&](const std::string& value) {
        charged_add(charge.bytes, value.capacity());
        charged_add(charge.strings, value.capacity());
    };
    std::visit([&](const auto& cell) {
        using T = std::decay_t<decltype(cell)>;
        if constexpr (std::is_same_v<T, StringCell>) {
            add_string(cell.value);
        } else if constexpr (std::is_same_v<T, ListCell>) {
            charged_add(charge.bytes, vector_bytes(cell.values.capacity(), sizeof(Value)));
        } else if constexpr (std::is_same_v<T, MapCell>) {
            charged_add(charge.bytes, vector_bytes(cell.entries.capacity(),
                                                    sizeof(std::pair<std::string, Value>)));
            for (const auto& [key, ignored] : cell.entries) { (void)ignored; add_string(key); }
        } else if constexpr (std::is_same_v<T, FunctionCell>) {
            charged_add(charge.bytes, vector_bytes(cell.metadata.captures.capacity(), sizeof(Value)));
        } else if constexpr (std::is_same_v<T, ModuleCell>) {
            add_string(cell.metadata.name);
            charged_add(charge.bytes, vector_bytes(cell.metadata.exports.capacity(),
                                                    sizeof(std::pair<std::string, Value>)));
            for (const auto& [key, ignored] : cell.metadata.exports) { (void)ignored; add_string(key); }
        } else if constexpr (std::is_same_v<T, ErrorCell>) {
            add_string(cell.metadata.message);
            add_optional_source_charge(charge, cell.metadata.source);
            charged_add(charge.bytes,
                        vector_bytes(cell.metadata.stack.capacity(), sizeof(ErrorStackFrame)));
            for (const auto& frame : cell.metadata.stack) {
                add_string(frame.module);
                add_string(frame.function);
                add_optional_source_charge(charge, frame.call_source);
                add_optional_source_charge(charge, frame.definition_source);
                add_optional_source_charge(charge, frame.defer_source);
            }
            charged_add(charge.bytes,
                        vector_bytes(cell.metadata.suppressed.capacity(), sizeof(Value)));
            charged_add(charge.bytes,
                        vector_bytes(cell.metadata.details.capacity(),
                                     sizeof(std::pair<std::string, Value>)));
            for (const auto& [key, ignored] : cell.metadata.details) {
                (void)ignored;
                add_string(key);
            }
            const auto add_context = [&](const std::optional<std::string>& value) {
                if (value) add_string(*value);
            };
            add_context(cell.metadata.context.task_id);
            add_context(cell.metadata.context.session_id);
            add_context(cell.metadata.context.package_id);
            add_context(cell.metadata.context.snapshot_id);
            add_context(cell.metadata.context.language_version);
            add_context(cell.metadata.context.correlation_id);
        } else if constexpr (std::is_same_v<T, TaskCell>) {
            charged_add(charge.bytes, vector_bytes(cell.metadata.retained_values.capacity(), sizeof(Value)));
        } else if constexpr (std::is_same_v<T, HostCell>) {
            if (!cell.metadata.closed) charge.external = cell.metadata.external_bytes;
        }
    }, data);
    return charge;
}

[[nodiscard]] ValueKind data_kind(const CellData& data) noexcept
{
    return std::visit([](const auto& cell) -> ValueKind {
        using T = std::decay_t<decltype(cell)>;
        if constexpr (std::is_same_v<T, StringCell>) return ValueKind::String;
        else if constexpr (std::is_same_v<T, ListCell>) return ValueKind::List;
        else if constexpr (std::is_same_v<T, MapCell>) return ValueKind::OrderedMap;
        else if constexpr (std::is_same_v<T, FunctionCell>) return ValueKind::Function;
        else if constexpr (std::is_same_v<T, ModuleCell>) return ValueKind::Module;
        else if constexpr (std::is_same_v<T, ErrorCell>) return ValueKind::Error;
        else if constexpr (std::is_same_v<T, TaskCell>) return ValueKind::Task;
        else return ValueKind::HostHandle;
    }, data);
}

[[nodiscard]] bool is_valid_utf8(const std::string_view text) noexcept
{
    std::size_t offset = 0;
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset]);
        if (first <= 0x7f) { ++offset; continue; }
        std::size_t length{};
        char32_t value{};
        char32_t minimum{};
        if (first >= 0xc2 && first <= 0xdf) { length = 2; value = first & 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { length = 3; value = first & 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { length = 4; value = first & 0x07; minimum = 0x10000; }
        else return false;
        if (offset + length > text.size()) return false;
        for (std::size_t index = 1; index < length; ++index) {
            const auto byte = static_cast<unsigned char>(text[offset + index]);
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
        offset += length;
    }
    return true;
}

void truncate_utf8(std::string& text, const std::size_t limit,
                   std::size_t& omitted_bytes)
{
    if (text.size() <= limit) return;
    auto end = limit;
    while (end > 0 &&
           (static_cast<unsigned char>(text[end]) & 0xc0U) == 0x80U) --end;
    const auto original = text.size();
    text.resize(end);
    saturating_add(omitted_bytes, original - end);
}

void compact_string(std::string& text)
{
    std::string compact{text};
    text.swap(compact);
}

[[nodiscard]] std::optional<ModuleKind> canonical_module_kind(
    const std::string_view module,
    const NfcPredicate is_nfc)
{
    try {
        return validate_module_specifier(module, is_nfc).kind;
    } catch (const ModuleSpecifierError&) {
        return std::nullopt;
    }
}

void validate_source_reference(const SourceReference& source, const NfcPredicate is_nfc)
{
    if (!is_valid_utf8(source.snapshot_id) || !is_valid_utf8(source.module)) {
        throw RuntimeError(RuntimeErrorCode::InvalidUtf8,
                           "error source reference is not valid UTF-8");
    }
    if (source.snapshot_id.empty() || source.snapshot_id.find('/') != std::string::npos ||
        source.snapshot_id.find('\\') != std::string::npos ||
        source.snapshot_id.find('\0') != std::string::npos ||
        canonical_module_kind(source.module, is_nfc) != ModuleKind::Package) {
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "error source reference is not canonical");
    }
    const auto& begin = source.span.begin;
    const auto& end = source.span.end;
    if (begin.line == 0 || begin.column == 0 || end.line == 0 || end.column == 0 ||
        begin.byte_offset > end.byte_offset || begin.line > end.line ||
        (begin.line == end.line && begin.column > end.column)) {
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "error source span is not ordered");
    }
}

void validate_optional_source(
    const std::optional<SourceReference>& source,
    const NfcPredicate is_nfc)
{
    if (source) validate_source_reference(*source, is_nfc);
}

void validate_error_frame(const ErrorStackFrame& frame, const NfcPredicate is_nfc)
{
    if (!is_valid_utf8(frame.module) || !is_valid_utf8(frame.function)) {
        throw RuntimeError(RuntimeErrorCode::InvalidUtf8,
                           "error stack frame is not valid UTF-8");
    }
    const auto module_kind = canonical_module_kind(frame.module, is_nfc);
    if (!module_kind || frame.function.empty() ||
        (frame.kind == ErrorFrameKind::Host) != (*module_kind == ModuleKind::Host)) {
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "error stack frame identity is not canonical");
    }
    validate_optional_source(frame.call_source, is_nfc);
    validate_optional_source(frame.definition_source, is_nfc);
    validate_optional_source(frame.defer_source, is_nfc);
    if (frame.kind == ErrorFrameKind::Host && frame.definition_source) {
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "host error frame cannot carry a definition source");
    }
    if (frame.phase != ErrorFramePhase::Cleanup && frame.defer_source) {
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "non-cleanup error frame cannot carry a defer source");
    }
    if (frame.phase == ErrorFramePhase::Cleanup && !frame.defer_source) {
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "cleanup error frame requires a defer source");
    }
    if ((frame.kind == ErrorFrameKind::Host) !=
        (frame.phase == ErrorFramePhase::Host)) {
        throw RuntimeError(RuntimeErrorCode::TypeMismatch,
                           "error frame kind and phase disagree");
    }
}

void validate_optional_context_string(const std::optional<std::string>& value)
{
    if (value && !is_valid_utf8(*value)) {
        throw RuntimeError(RuntimeErrorCode::InvalidUtf8,
                           "error context is not valid UTF-8");
    }
}

template <typename Function>
void visit_children(const CellData& data, Function&& function)
{
    std::visit([&](const auto& cell) {
        using T = std::decay_t<decltype(cell)>;
        if constexpr (std::is_same_v<T, ListCell>) {
            for (const auto value : cell.values) function(value);
        } else if constexpr (std::is_same_v<T, MapCell>) {
            for (const auto& entry : cell.entries) function(entry.second);
        } else if constexpr (std::is_same_v<T, FunctionCell>) {
            for (const auto value : cell.metadata.captures) function(value);
        } else if constexpr (std::is_same_v<T, ModuleCell>) {
            for (const auto& entry : cell.metadata.exports) function(entry.second);
        } else if constexpr (std::is_same_v<T, ErrorCell>) {
            if (cell.metadata.cause) function(*cell.metadata.cause);
            for (const auto value : cell.metadata.suppressed) function(value);
            for (const auto& detail : cell.metadata.details) function(detail.second);
        } else if constexpr (std::is_same_v<T, TaskCell>) {
            for (const auto value : cell.metadata.retained_values) function(value);
        }
    }, data);
}

[[noreturn]] void type_error(const char* expected)
{
    throw RuntimeError(RuntimeErrorCode::TypeMismatch, std::string("expected ") + expected);
}

}  // namespace

std::string_view language_error_code_name(const LanguageErrorCode code) noexcept
{
    const auto index = static_cast<std::size_t>(code);
    return index < error_code_descriptors.size() ? error_code_descriptors[index].name :
                                                   "InternalInvariant";
}

bool language_error_code_catchable(const LanguageErrorCode code) noexcept
{
    const auto index = static_cast<std::size_t>(code);
    return index < error_code_descriptors.size() && error_code_descriptors[index].catchable;
}

std::optional<LanguageErrorCode> language_error_code_from_name(
    const std::string_view name) noexcept
{
    const auto found = std::find_if(error_code_descriptors.begin(), error_code_descriptors.end(),
                                    [&](const auto& item) { return item.name == name; });
    if (found == error_code_descriptors.end()) return std::nullopt;
    return found->code;
}

struct Heap::Impl {
    struct Slot {
        std::unique_ptr<Cell> cell;
        std::uint64_t generation{1};
    };

    explicit Impl(const HeapLimits requested, const NfcPredicate requested_module_nfc)
        : limits(requested), module_nfc(requested_module_nfc),
          heap_identity(allocate_heap_identity())
    {
        if (limits.soft_collect_threshold > limits.max_live_bytes) {
            limits.soft_collect_threshold = limits.max_live_bytes;
        }
    }

    [[nodiscard]] Cell& dereference(const HeapRef reference)
    {
        return const_cast<Cell&>(std::as_const(*this).dereference(reference));
    }

    [[nodiscard]] const Cell& dereference(const HeapRef reference) const
    {
        if (reference.heap_identity != heap_identity) {
            throw RuntimeError(RuntimeErrorCode::CrossHeapReference, "heap reference belongs to another context");
        }
        if (reference.slot >= slots.size()) {
            throw RuntimeError(RuntimeErrorCode::StaleReference, "heap reference slot is stale");
        }
        const auto& slot = slots[reference.slot];
        if (!slot.cell || slot.generation != reference.generation) {
            throw RuntimeError(RuntimeErrorCode::StaleReference, "heap reference generation is stale");
        }
        return *slot.cell;
    }

    [[nodiscard]] Cell& expected(const HeapRef reference, const ValueKind kind)
    {
        auto& cell = dereference(reference);
        if (data_kind(cell.data) != kind) {
            throw RuntimeError(RuntimeErrorCode::CellKindMismatch, "heap cell kind mismatch");
        }
        return cell;
    }

    [[nodiscard]] const Cell& expected(const HeapRef reference, const ValueKind kind) const
    {
        const auto& cell = dereference(reference);
        if (data_kind(cell.data) != kind) {
            throw RuntimeError(RuntimeErrorCode::CellKindMismatch, "heap cell kind mismatch");
        }
        return cell;
    }

    void validate_edge(const Value value) const
    {
        if (value.inline_kind() == ValueKind::HeapReference) (void)dereference(value.as_heap_ref());
    }

    void validate_edges(const CellData& data) const
    {
        visit_children(data, [&](const Value value) { validate_edge(value); });
    }

    void validate_error_graph(const Value root) const
    {
        struct Frame { Value value; bool exit; };
        std::vector<Frame> work{{root, false}};
        std::unordered_set<std::uint32_t> active;
        std::unordered_set<std::uint32_t> complete;
        std::size_t visited = 0;
        while (!work.empty()) {
            const auto frame = work.back();
            work.pop_back();
            validate_edge(frame.value);
            const auto reference = frame.value.as_heap_ref();
            const auto& cell = expected(reference, ValueKind::Error);
            if (frame.exit) {
                active.erase(reference.slot);
                complete.insert(reference.slot);
                continue;
            }
            if (complete.contains(reference.slot)) continue;
            if (visited >= limits.max_collection_work) {
                throw RuntimeError(RuntimeErrorCode::CollectionWorkLimitExceeded,
                                   "structured Error graph validation work limit exceeded");
            }
            ++visited;
            if (!active.insert(reference.slot).second) {
                throw RuntimeError(RuntimeErrorCode::CellKindMismatch,
                                   "cyclic structured Error graph");
            }
            work.push_back({frame.value, true});
            const auto& metadata = std::get<ErrorCell>(cell.data).metadata;
            for (auto iterator = metadata.suppressed.rbegin();
                 iterator != metadata.suppressed.rend(); ++iterator) {
                work.push_back({*iterator, false});
            }
            if (metadata.cause) work.push_back({*metadata.cause, false});
        }
    }

    [[nodiscard]] std::size_t cause_depth(const Value root) const
    {
        std::unordered_set<std::uint32_t> visited;
        auto current = std::optional<Value>{root};
        std::size_t depth = 0;
        while (current) {
            validate_edge(*current);
            const auto reference = current->as_heap_ref();
            if (!visited.insert(reference.slot).second) {
                throw RuntimeError(RuntimeErrorCode::CellKindMismatch,
                                   "cyclic structured Error cause chain");
            }
            const auto& metadata = std::get<ErrorCell>(
                expected(reference, ValueKind::Error).data).metadata;
            if (depth == std::numeric_limits<std::size_t>::max()) {
                throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                                   "structured Error cause depth overflow");
            }
            ++depth;
            current = metadata.cause;
        }
        return depth;
    }

    [[nodiscard]] std::size_t detail_value_bytes(const Value root) const
    {
        struct Frame { Value value; bool exit; };
        std::vector<Frame> work{{root, false}};
        std::unordered_set<std::uint32_t> active;
        std::size_t bytes = 0;
        std::size_t visited = 0;
        while (!work.empty()) {
            if (visited >= limits.max_collection_work) {
                throw RuntimeError(RuntimeErrorCode::JsonWorkLimitExceeded,
                                   "structured Error detail work limit exceeded");
            }
            const auto frame = work.back();
            work.pop_back();
            const auto value = frame.value;
            if (frame.exit) {
                active.erase(value.as_heap_ref().slot);
                continue;
            }
            ++visited;
            switch (value.inline_kind() == ValueKind::HeapReference ?
                        data_kind(dereference(value.as_heap_ref()).data) : value.inline_kind()) {
                case ValueKind::Null: charged_add(bytes, 4); break;
                case ValueKind::Boolean: charged_add(bytes, 5); break;
                case ValueKind::Integer:
                case ValueKind::Float: charged_add(bytes, 24); break;
                case ValueKind::String: {
                    const auto& text = std::get<StringCell>(
                        expected(value.as_heap_ref(), ValueKind::String).data).value;
                    charged_add(bytes, text.size());
                    break;
                }
                case ValueKind::List: {
                    const auto reference = value.as_heap_ref();
                    if (!active.insert(reference.slot).second) {
                        throw RuntimeError(RuntimeErrorCode::JsonCycle,
                                           "cyclic structured Error detail");
                    }
                    const auto& values = std::get<ListCell>(
                        expected(reference, ValueKind::List).data).values;
                    if (values.size() > limits.max_collection_work - visited ||
                        work.size() > limits.max_collection_work - visited - values.size()) {
                        throw RuntimeError(RuntimeErrorCode::JsonWorkLimitExceeded,
                                           "structured Error detail work limit exceeded");
                    }
                    charged_add(bytes, 2 + values.size());
                    work.push_back({value, true});
                    for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator) {
                        work.push_back({*iterator, false});
                    }
                    break;
                }
                case ValueKind::OrderedMap: {
                    const auto reference = value.as_heap_ref();
                    if (!active.insert(reference.slot).second) {
                        throw RuntimeError(RuntimeErrorCode::JsonCycle,
                                           "cyclic structured Error detail");
                    }
                    const auto& entries = std::get<MapCell>(
                        expected(reference, ValueKind::OrderedMap).data).entries;
                    if (entries.size() > limits.max_collection_work - visited ||
                        work.size() > limits.max_collection_work - visited - entries.size()) {
                        throw RuntimeError(RuntimeErrorCode::JsonWorkLimitExceeded,
                                           "structured Error detail work limit exceeded");
                    }
                    charged_add(bytes, 2 + entries.size());
                    work.push_back({value, true});
                    for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator) {
                        charged_add(bytes, iterator->first.size());
                        work.push_back({iterator->second, false});
                    }
                    break;
                }
                default:
                    throw RuntimeError(RuntimeErrorCode::JsonUnsupported,
                                       "structured Error detail is not JSON-safe");
            }
        }
        return bytes;
    }

    void preflight(const Charge charge, const std::size_t allocation_size, const bool add_cell)
    {
        if (torn_down) throw RuntimeError(RuntimeErrorCode::HeapTornDown, "heap has been torn down");
        if (allocation_size > limits.max_single_allocation) {
            throw RuntimeError(RuntimeErrorCode::SingleAllocationExceeded, "single heap allocation exceeds limit");
        }
        std::size_t projected{};
        if (!checked_add(stats.live_bytes, charge.bytes, projected)) {
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "heap byte accounting overflow");
        }
        if (projected > limits.soft_collect_threshold && !collecting) collect();

        if (!checked_add(stats.live_bytes, charge.bytes, projected) || projected > limits.max_live_bytes) {
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "live heap byte limit exceeded");
        }
        std::size_t projected_strings{};
        if (!checked_add(stats.string_bytes, charge.strings, projected_strings) ||
            projected_strings > limits.max_string_bytes) {
            throw RuntimeError(RuntimeErrorCode::StringLimitExceeded, "heap string byte limit exceeded");
        }
        std::size_t projected_external{};
        if (!checked_add(stats.external_bytes, charge.external, projected_external) ||
            projected_external > limits.max_external_bytes) {
            throw RuntimeError(RuntimeErrorCode::ExternalMemoryLimitExceeded,
                               "heap external byte limit exceeded");
        }
        if (add_cell && stats.live_cells >= limits.max_cells) {
            throw RuntimeError(RuntimeErrorCode::CellLimitExceeded, "live heap cell limit exceeded");
        }
    }

    [[nodiscard]] Value allocate(CellData data)
    {
        validate_edges(data);
        const auto charge = charge_for(data);
        preflight(charge, charge.bytes, true);
        if (free_slots.empty() && slots.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw RuntimeError(RuntimeErrorCode::CellLimitExceeded, "heap slot index limit exceeded");
        }
        stats.live_bytes += charge.bytes;
        stats.string_bytes += charge.strings;
        stats.external_bytes += charge.external;
        ++stats.live_cells;
        std::unique_ptr<Cell> cell;
        try {
            cell = std::make_unique<Cell>(std::move(data), charge.bytes, charge.strings, charge.external);
        } catch (const std::bad_alloc&) {
            stats.live_bytes -= charge.bytes;
            stats.string_bytes -= charge.strings;
            stats.external_bytes -= charge.external;
            --stats.live_cells;
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "native allocation failed");
        }

        std::uint32_t index{};
        if (!free_slots.empty()) {
            index = free_slots.back();
            free_slots.pop_back();
            slots[index].cell = std::move(cell);
        } else {
            index = static_cast<std::uint32_t>(slots.size());
            try {
                slots.push_back({std::move(cell), 1});
            } catch (const std::bad_alloc&) {
                stats.live_bytes -= charge.bytes;
                stats.string_bytes -= charge.strings;
                stats.external_bytes -= charge.external;
                --stats.live_cells;
                throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "native slot allocation failed");
            }
        }
        return Value(HeapRef{heap_identity, index, slots[index].generation});
    }

    void reserve_sweep_storage(const bool only_unmarked)
    {
        std::size_t releases = 0;
        for (const auto& slot : slots) {
            if (!slot.cell || (only_unmarked && slot.cell->marked)) continue;
            const auto* host = std::get_if<HostCell>(&slot.cell->data);
            if (host && !host->metadata.closed) ++releases;
        }
        std::size_t release_capacity{};
        if (!checked_add(release_queue.size(), releases, release_capacity)) {
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "release queue size overflow");
        }
        if (release_capacity > limits.max_pending_release_records) {
            throw RuntimeError(RuntimeErrorCode::ReleaseQueueLimitExceeded,
                               "pending host release queue limit exceeded");
        }
        try {
            free_slots.reserve(slots.size());
            release_queue.reserve(release_capacity);
        } catch (const std::bad_alloc&) {
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "sweep bookkeeping allocation failed");
        }
    }

    void queue_host_release(Cell& cell)
    {
        auto& host = std::get<HostCell>(cell.data);
        if (host.metadata.closed) return;
        release_queue.push_back({host.metadata.handle_id, host.metadata.adapter_id,
                                 host.metadata.external_bytes});
        host.metadata.closed = true;
        stats.external_bytes -= host.metadata.external_bytes;
        cell.accounted_external_bytes = 0;
    }

    void retire_slot(const std::uint32_t index)
    {
        auto& slot = slots[index];
        auto& cell = *slot.cell;
        if (std::holds_alternative<HostCell>(cell.data)) queue_host_release(cell);
        stats.live_bytes -= cell.accounted_bytes;
        stats.string_bytes -= cell.accounted_string_bytes;
        if (cell.accounted_external_bytes != 0) cell.accounted_external_bytes = 0;
        --stats.live_cells;
        if (stats.reclaimed_cells != std::numeric_limits<std::size_t>::max()) ++stats.reclaimed_cells;
        if (cell.accounted_bytes > std::numeric_limits<std::size_t>::max() - stats.reclaimed_bytes)
            stats.reclaimed_bytes = std::numeric_limits<std::size_t>::max();
        else stats.reclaimed_bytes += cell.accounted_bytes;
        slot.cell.reset();
        if (slot.generation != std::numeric_limits<std::uint64_t>::max()) {
            ++slot.generation;
            free_slots.push_back(index);
        }
    }

    void collect()
    {
        if (torn_down) throw RuntimeError(RuntimeErrorCode::HeapTornDown, "heap has been torn down");
        collecting = true;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{collecting};
        for (auto& slot : slots) if (slot.cell) slot.cell->marked = false;

        std::vector<HeapRef> worklist;
        try { worklist.reserve(stats.live_cells); }
        catch (const std::bad_alloc&) {
            throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "collector worklist allocation failed");
        }
        const auto enqueue = [&](const Value value) {
            if (value.inline_kind() != ValueKind::HeapReference) return;
            const auto reference = value.as_heap_ref();
            auto& cell = dereference(reference);
            if (cell.marked) return;
            cell.marked = true;
            worklist.push_back(reference);
        };
        for (const auto& [ignored, value] : explicit_roots) { (void)ignored; enqueue(value); }
        for (const auto value : temporary_roots) enqueue(value);

        std::size_t work{};
        while (!worklist.empty()) {
            if (work >= limits.max_collection_work) {
                throw RuntimeError(RuntimeErrorCode::CollectionWorkLimitExceeded,
                                   "collector work limit exceeded");
            }
            const auto reference = worklist.back();
            worklist.pop_back();
            auto& cell = dereference(reference);
            ++work;
            visit_children(cell.data, enqueue);
        }

        reserve_sweep_storage(true);
        if (stats.collections != std::numeric_limits<std::size_t>::max()) ++stats.collections;
        for (std::size_t index = 0; index < slots.size(); ++index) {
            if (slots[index].cell && !slots[index].cell->marked)
                retire_slot(static_cast<std::uint32_t>(index));
        }
    }

    HeapLimits limits;
    NfcPredicate module_nfc{};
    HeapStats stats;
    std::uint64_t heap_identity;
    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_slots;
    std::unordered_map<RootId, Value> explicit_roots;
    std::vector<Value> temporary_roots;
    std::vector<HostReleaseRecord> release_queue;
    RootId next_root_id{1};
    bool collecting{false};
    bool torn_down{false};
};

std::string_view runtime_error_code_name(const RuntimeErrorCode code) noexcept
{
    switch (code) {
        case RuntimeErrorCode::TypeMismatch: return "RT001_TYPE_MISMATCH";
        case RuntimeErrorCode::CrossHeapReference: return "RT002_CROSS_HEAP_REFERENCE";
        case RuntimeErrorCode::StaleReference: return "RT003_STALE_REFERENCE";
        case RuntimeErrorCode::CellKindMismatch: return "RT004_CELL_KIND_MISMATCH";
        case RuntimeErrorCode::MemoryLimitExceeded: return "RT005_MEMORY_LIMIT_EXCEEDED";
        case RuntimeErrorCode::CellLimitExceeded: return "RT006_CELL_LIMIT_EXCEEDED";
        case RuntimeErrorCode::SingleAllocationExceeded: return "RT007_SINGLE_ALLOCATION_EXCEEDED";
        case RuntimeErrorCode::StringLimitExceeded: return "RT008_STRING_LIMIT_EXCEEDED";
        case RuntimeErrorCode::ExternalMemoryLimitExceeded: return "RT009_EXTERNAL_MEMORY_LIMIT_EXCEEDED";
        case RuntimeErrorCode::CollectionWorkLimitExceeded: return "RT010_COLLECTION_WORK_LIMIT_EXCEEDED";
        case RuntimeErrorCode::InvalidUtf8: return "RT011_INVALID_UTF8";
        case RuntimeErrorCode::JsonCycle: return "RT012_JSON_CYCLE";
        case RuntimeErrorCode::JsonNonFinite: return "RT013_JSON_NON_FINITE";
        case RuntimeErrorCode::JsonUnsupported: return "RT014_JSON_UNSUPPORTED";
        case RuntimeErrorCode::HeapTornDown: return "RT015_HEAP_TORN_DOWN";
        case RuntimeErrorCode::IndexOutOfRange: return "RT016_INDEX_OUT_OF_RANGE";
        case RuntimeErrorCode::ReleaseQueueLimitExceeded: return "RT017_RELEASE_QUEUE_LIMIT_EXCEEDED";
        case RuntimeErrorCode::JsonDepthLimitExceeded: return "RT018_JSON_DEPTH_LIMIT_EXCEEDED";
        case RuntimeErrorCode::JsonNodeLimitExceeded: return "RT019_JSON_NODE_LIMIT_EXCEEDED";
        case RuntimeErrorCode::JsonStringLimitExceeded: return "RT020_JSON_STRING_LIMIT_EXCEEDED";
        case RuntimeErrorCode::JsonByteLimitExceeded: return "RT021_JSON_BYTE_LIMIT_EXCEEDED";
        case RuntimeErrorCode::JsonWorkLimitExceeded: return "RT022_JSON_WORK_LIMIT_EXCEEDED";
        case RuntimeErrorCode::JsonDuplicateKey: return "RT023_JSON_DUPLICATE_KEY";
    }
    return "RT000_UNKNOWN";
}

RuntimeError::RuntimeError(const RuntimeErrorCode code, std::string message)
    : std::runtime_error(std::string(runtime_error_code_name(code)) + ": " + message), code_(code) {}

ValueKind Value::inline_kind() const noexcept
{
    switch (storage_.index()) {
        case 0: return ValueKind::Null;
        case 1: return ValueKind::Boolean;
        case 2: return ValueKind::Integer;
        case 3: return ValueKind::Float;
        default: return ValueKind::HeapReference;
    }
}

bool Value::as_boolean() const { if (const auto* value = std::get_if<bool>(&storage_)) return *value; type_error("Boolean"); }
std::int64_t Value::as_integer() const { if (const auto* value = std::get_if<std::int64_t>(&storage_)) return *value; type_error("Integer"); }
double Value::as_float() const { if (const auto* value = std::get_if<double>(&storage_)) return *value; type_error("Float"); }
HeapRef Value::as_heap_ref() const { if (const auto* value = std::get_if<HeapRef>(&storage_)) return *value; type_error("HeapReference"); }

Heap::RootScope::RootScope(Heap& heap) : heap_(&heap), marker_(heap.impl_->temporary_roots.size()) {}
Heap::RootScope::~RootScope() { if (heap_) heap_->pop_temporary_roots(marker_); }
void Heap::RootScope::add(const Value value) { heap_->add_temporary_root(value); }

Heap::Heap(const HeapLimits limits, const NfcPredicate module_nfc)
    : impl_(new Impl(limits, module_nfc))
{
}
Heap::~Heap()
{
    if (!impl_->torn_down) {
        try { (void)teardown(); } catch (...) {}
    }
    delete impl_;
}

Heap::RootId Heap::add_root(const Value value)
{
    if (impl_->torn_down) throw RuntimeError(RuntimeErrorCode::HeapTornDown, "heap has been torn down");
    impl_->validate_edge(value);
    if (impl_->next_root_id == std::numeric_limits<RootId>::max())
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "root id space exhausted");
    const auto id = impl_->next_root_id++;
    try { impl_->explicit_roots.emplace(id, value); }
    catch (const std::bad_alloc&) { throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "root allocation failed"); }
    return id;
}
bool Heap::update_root(const RootId id, const Value value)
{
    if (impl_->torn_down) throw RuntimeError(RuntimeErrorCode::HeapTornDown, "heap has been torn down");
    impl_->validate_edge(value);
    const auto found = impl_->explicit_roots.find(id);
    if (found == impl_->explicit_roots.end()) return false;
    found->second = value;
    return true;
}
bool Heap::remove_root(const RootId id) noexcept { return impl_->explicit_roots.erase(id) != 0; }
void Heap::add_temporary_root(const Value value)
{
    if (impl_->torn_down) throw RuntimeError(RuntimeErrorCode::HeapTornDown, "heap has been torn down");
    impl_->validate_edge(value);
    try { impl_->temporary_roots.push_back(value); }
    catch (const std::bad_alloc&) { throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "temporary root allocation failed"); }
}
void Heap::pop_temporary_roots(const std::size_t marker) noexcept { if (marker <= impl_->temporary_roots.size()) impl_->temporary_roots.resize(marker); }

Value Heap::allocate_string(std::string value)
{
    if (!is_valid_utf8(value)) throw RuntimeError(RuntimeErrorCode::InvalidUtf8, "string is not valid UTF-8");
    return impl_->allocate(StringCell{std::move(value)});
}

Value Heap::allocate_list(std::vector<Value> values)
{
    auto roots = root_scope();
    for (const auto value : values) roots.add(value);
    return impl_->allocate(ListCell{std::move(values)});
}

Value Heap::allocate_map(std::vector<std::pair<std::string, Value>> entries)
{
    std::vector<std::pair<std::string, Value>> normalized;
    normalized.reserve(entries.size());
    for (auto& entry : entries) {
        if (!is_valid_utf8(entry.first)) throw RuntimeError(RuntimeErrorCode::InvalidUtf8, "map key is not valid UTF-8");
        const auto found = std::find_if(normalized.begin(), normalized.end(), [&](const auto& item) { return item.first == entry.first; });
        if (found == normalized.end()) normalized.push_back(std::move(entry));
        else found->second = entry.second;
    }
    auto roots = root_scope();
    for (const auto& entry : normalized) roots.add(entry.second);
    return impl_->allocate(MapCell{std::move(normalized)});
}

Value Heap::allocate_function(FunctionMetadata metadata)
{
    auto roots = root_scope(); for (const auto value : metadata.captures) roots.add(value);
    return impl_->allocate(FunctionCell{std::move(metadata)});
}
Value Heap::allocate_module(ModuleMetadata metadata)
{
    if (!is_valid_utf8(metadata.name)) throw RuntimeError(RuntimeErrorCode::InvalidUtf8, "module name is not valid UTF-8");
    auto roots = root_scope();
    for (const auto& entry : metadata.exports) {
        if (!is_valid_utf8(entry.first)) throw RuntimeError(RuntimeErrorCode::InvalidUtf8, "module export is not valid UTF-8");
        roots.add(entry.second);
    }
    return impl_->allocate(ModuleCell{std::move(metadata)});
}
Value Heap::allocate_error(ErrorMetadata metadata)
{
    try {
        if (static_cast<std::size_t>(metadata.code) >= error_code_descriptors.size()) {
            throw RuntimeError(RuntimeErrorCode::CellKindMismatch,
                               "unknown stable language Error code");
        }
        if (!is_valid_utf8(metadata.message)) {
            throw RuntimeError(RuntimeErrorCode::InvalidUtf8,
                               "error message is not valid UTF-8");
        }
        truncate_utf8(metadata.message, impl_->limits.max_error_message_bytes,
                      metadata.truncated.message_bytes);
        compact_string(metadata.message);
        validate_optional_source(metadata.source, impl_->module_nfc);

        if (metadata.stack.size() > impl_->limits.max_error_stack_frames) {
            const auto omitted = metadata.stack.size() - impl_->limits.max_error_stack_frames;
            const auto inner = std::min(impl_->limits.max_error_innermost_frames,
                                        impl_->limits.max_error_stack_frames);
            const auto outer = impl_->limits.max_error_stack_frames - inner;
            std::vector<ErrorStackFrame> bounded;
            bounded.reserve(impl_->limits.max_error_stack_frames);
            bounded.insert(bounded.end(), metadata.stack.begin(), metadata.stack.begin() + inner);
            if (outer != 0) {
                bounded.insert(bounded.end(), metadata.stack.end() - outer, metadata.stack.end());
            }
            metadata.stack.swap(bounded);
            saturating_add(metadata.truncated.stack_frames, omitted);
        }
        for (const auto& frame : metadata.stack) {
            validate_error_frame(frame, impl_->module_nfc);
        }

        if (metadata.cause) {
            impl_->validate_error_graph(*metadata.cause);
            const auto depth = impl_->cause_depth(*metadata.cause);
            if (depth > impl_->limits.max_error_cause_depth) {
                throw RuntimeError(RuntimeErrorCode::CellKindMismatch,
                                   "structured Error cause depth exceeds construction limit");
            }
        }

        std::vector<Value> accepted_suppressed;
        accepted_suppressed.reserve(std::min(metadata.suppressed.size(),
                                             impl_->limits.max_error_suppressed));
        for (const auto value : metadata.suppressed) {
            if (accepted_suppressed.size() < impl_->limits.max_error_suppressed) {
                impl_->validate_error_graph(value);
                if ((metadata.cause && value == *metadata.cause) ||
                    std::find(accepted_suppressed.begin(), accepted_suppressed.end(), value) !=
                        accepted_suppressed.end()) {
                    throw RuntimeError(RuntimeErrorCode::CellKindMismatch,
                                       "duplicate causal Error edge");
                }
                accepted_suppressed.push_back(value);
            }
        }
        if (metadata.suppressed.size() > accepted_suppressed.size()) {
            saturating_add(metadata.truncated.suppressed_errors,
                           metadata.suppressed.size() - accepted_suppressed.size());
        }
        metadata.suppressed.swap(accepted_suppressed);

        std::size_t detail_bytes = 0;
        std::vector<std::string_view> detail_keys;
        detail_keys.reserve(metadata.details.size());
        for (const auto& [key, value] : metadata.details) {
            if (!is_valid_utf8(key)) {
                throw RuntimeError(RuntimeErrorCode::InvalidUtf8,
                                   "error detail key is not valid UTF-8");
            }
            if (std::find(detail_keys.begin(), detail_keys.end(), key) != detail_keys.end()) {
                throw RuntimeError(RuntimeErrorCode::JsonDuplicateKey,
                                   "duplicate structured Error detail key");
            }
            detail_keys.emplace_back(key);
            charged_add(detail_bytes, key.size());
            charged_add(detail_bytes, impl_->detail_value_bytes(value));
        }

        const auto validate_context = [&](std::optional<std::string>& value) {
            validate_optional_context_string(value);
            if (value) charged_add(detail_bytes, value->size());
        };
        validate_context(metadata.context.task_id);
        validate_context(metadata.context.session_id);
        validate_context(metadata.context.package_id);
        validate_context(metadata.context.snapshot_id);
        validate_context(metadata.context.language_version);
        validate_context(metadata.context.correlation_id);

        if (detail_bytes > impl_->limits.max_error_detail_bytes) {
            metadata.truncated.details_replaced = metadata.truncated.details_replaced ||
                                                  !metadata.details.empty();
            std::vector<std::pair<std::string, Value>>{}.swap(metadata.details);
            auto remaining = impl_->limits.max_error_detail_bytes;
            const auto bound_context = [&](std::optional<std::string>& value) {
                if (!value) return;
                std::size_t ignored{};
                truncate_utf8(*value, remaining, ignored);
                compact_string(*value);
                remaining -= value->size();
            };
            bound_context(metadata.context.task_id);
            bound_context(metadata.context.session_id);
            bound_context(metadata.context.package_id);
            bound_context(metadata.context.snapshot_id);
            bound_context(metadata.context.language_version);
            bound_context(metadata.context.correlation_id);
            const auto preserved_context = impl_->limits.max_error_detail_bytes - remaining;
            saturating_add(metadata.truncated.detail_bytes,
                           detail_bytes - preserved_context);
        }
        auto roots = root_scope();
        if (metadata.cause) roots.add(*metadata.cause);
        for (const auto value : metadata.suppressed) roots.add(value);
        for (const auto& detail : metadata.details) roots.add(detail.second);
        return impl_->allocate(ErrorCell{std::move(metadata)});
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                           "structured Error construction allocation failed");
    }
}

Value Heap::derive_error(const HeapRef primary, ErrorDerivation additions)
{
    try {
        const auto primary_value = Value(primary);
        if ((additions.cause && *additions.cause == primary_value) ||
            std::find(additions.suppressed.begin(), additions.suppressed.end(), primary_value) !=
                additions.suppressed.end()) {
            throw RuntimeError(RuntimeErrorCode::CellKindMismatch,
                               "derived Error cannot reference its primary as cause or suppressed");
        }
        auto metadata = error_metadata(primary);
        if (additions.cause) {
            if (metadata.cause) {
                throw RuntimeError(RuntimeErrorCode::CellKindMismatch,
                                   "derived Error cannot replace an existing cause");
            }
            metadata.cause = *additions.cause;
        }
        metadata.suppressed.insert(metadata.suppressed.end(), additions.suppressed.begin(),
                                   additions.suppressed.end());
        return allocate_error(std::move(metadata));
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                           "structured Error derivation allocation failed");
    }
}
Value Heap::allocate_task(TaskMetadata metadata)
{
    auto roots = root_scope(); for (const auto value : metadata.retained_values) roots.add(value);
    return impl_->allocate(TaskCell{std::move(metadata)});
}
Value Heap::allocate_host_handle(HostHandleMetadata metadata)
{
    return impl_->allocate(HostCell{std::move(metadata)});
}

ValueKind Heap::kind(const Value value) const
{
    return value.inline_kind() == ValueKind::HeapReference ? kind(value.as_heap_ref()) : value.inline_kind();
}
ValueKind Heap::kind(const HeapRef reference) const { return data_kind(impl_->dereference(reference).data); }
std::string Heap::string_copy(const HeapRef reference) const { return std::get<StringCell>(impl_->expected(reference, ValueKind::String).data).value; }
std::string_view Heap::string_view(const HeapRef reference) const { return std::get<StringCell>(impl_->expected(reference, ValueKind::String).data).value; }
std::size_t Heap::list_size(const HeapRef reference) const { return std::get<ListCell>(impl_->expected(reference, ValueKind::List).data).values.size(); }
Value Heap::list_value_at(const HeapRef reference, const std::size_t index) const
{
    const auto& values = std::get<ListCell>(impl_->expected(reference, ValueKind::List).data).values;
    if (index >= values.size())
        throw RuntimeError(RuntimeErrorCode::IndexOutOfRange, "list index is out of range");
    return values[index];
}
std::size_t Heap::map_size(const HeapRef reference) const { return std::get<MapCell>(impl_->expected(reference, ValueKind::OrderedMap).data).entries.size(); }
std::pair<std::string_view, Value> Heap::map_entry_at(
    const HeapRef reference, const std::size_t index) const
{
    const auto& entries = std::get<MapCell>(impl_->expected(reference, ValueKind::OrderedMap).data).entries;
    if (index >= entries.size())
        throw RuntimeError(RuntimeErrorCode::IndexOutOfRange, "map index is out of range");
    return {entries[index].first, entries[index].second};
}
const ErrorMetadata& Heap::error_metadata_view(const HeapRef reference) const
{
    return std::get<ErrorCell>(impl_->expected(reference, ValueKind::Error).data).metadata;
}
std::vector<Value> Heap::list_values(const HeapRef reference) const { return std::get<ListCell>(impl_->expected(reference, ValueKind::List).data).values; }
std::vector<std::pair<std::string, Value>> Heap::map_entries(const HeapRef reference) const { return std::get<MapCell>(impl_->expected(reference, ValueKind::OrderedMap).data).entries; }
std::optional<Value> Heap::map_get(const HeapRef reference, const std::string_view key) const
{
    const auto& entries = std::get<MapCell>(impl_->expected(reference, ValueKind::OrderedMap).data).entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.first == key; });
    return found == entries.end() ? std::nullopt : std::optional<Value>{found->second};
}
FunctionMetadata Heap::function_metadata(const HeapRef reference) const { return std::get<FunctionCell>(impl_->expected(reference, ValueKind::Function).data).metadata; }
ModuleMetadata Heap::module_metadata(const HeapRef reference) const { return std::get<ModuleCell>(impl_->expected(reference, ValueKind::Module).data).metadata; }
ErrorMetadata Heap::error_metadata(const HeapRef reference) const
{
    try {
        return std::get<ErrorCell>(impl_->expected(reference, ValueKind::Error).data).metadata;
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded,
                           "structured Error snapshot allocation failed");
    }
}
TaskMetadata Heap::task_metadata(const HeapRef reference) const { return std::get<TaskCell>(impl_->expected(reference, ValueKind::Task).data).metadata; }
HostHandleMetadata Heap::host_handle_metadata(const HeapRef reference) const { return std::get<HostCell>(impl_->expected(reference, ValueKind::HostHandle).data).metadata; }

void Heap::list_append(const HeapRef reference, const Value value)
{
    auto roots = root_scope(); roots.add(Value(reference)); roots.add(value);
    impl_->validate_edge(value);
    std::size_t old_capacity{};
    std::size_t old_size{};
    {
        const auto& before = std::get<ListCell>(impl_->expected(reference, ValueKind::List).data).values;
        old_capacity = before.capacity();
        old_size = before.size();
    }
    std::size_t desired = old_capacity;
    if (old_size == desired) {
        desired = desired == 0 ? 1 : desired > std::numeric_limits<std::size_t>::max() / 2 ?
                  std::numeric_limits<std::size_t>::max() : desired * 2;
    }
    const auto new_vector_bytes = vector_bytes(desired, sizeof(Value));
    const auto old_vector_bytes = vector_bytes(old_capacity, sizeof(Value));
    const Charge delta{new_vector_bytes - old_vector_bytes, 0, 0};
    impl_->preflight(delta, new_vector_bytes, false);
    impl_->stats.live_bytes += delta.bytes;
    std::size_t reserved_bytes = delta.bytes;
    std::vector<Value> replacement;
    try {
        replacement.reserve(desired);
        const auto& current = std::get<ListCell>(impl_->expected(reference, ValueKind::List).data).values;
        replacement.insert(replacement.end(), current.begin(), current.end());
        replacement.push_back(value);
        const auto actual = vector_bytes(replacement.capacity(), sizeof(Value));
        const auto actual_growth = actual - old_vector_bytes;
        if (actual_growth > reserved_bytes) {
            const auto extra = actual_growth - reserved_bytes;
            impl_->preflight({extra, 0, 0}, actual, false);
            impl_->stats.live_bytes += extra;
            reserved_bytes += extra;
        } else if (actual_growth < reserved_bytes) {
            impl_->stats.live_bytes -= reserved_bytes - actual_growth;
            reserved_bytes = actual_growth;
        }
    } catch (const std::bad_alloc&) {
        impl_->stats.live_bytes -= reserved_bytes;
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "list growth allocation failed");
    } catch (...) {
        impl_->stats.live_bytes -= reserved_bytes;
        throw;
    }
    auto& cell = impl_->expected(reference, ValueKind::List);
    auto& current = std::get<ListCell>(cell.data).values;
    current.swap(replacement);
    cell.accounted_bytes += reserved_bytes;
}

void Heap::list_set(const HeapRef reference, const std::size_t index, const Value value)
{
    impl_->validate_edge(value);
    auto& values = std::get<ListCell>(impl_->expected(reference, ValueKind::List).data).values;
    if (index >= values.size()) throw RuntimeError(RuntimeErrorCode::IndexOutOfRange, "list index out of range");
    values[index] = value;
}

void Heap::map_set(const HeapRef reference, std::string key, const Value value)
{
    if (!is_valid_utf8(key)) throw RuntimeError(RuntimeErrorCode::InvalidUtf8, "map key is not valid UTF-8");
    auto roots = root_scope(); roots.add(Value(reference)); roots.add(value);
    impl_->validate_edge(value);
    {
        auto& entries = std::get<MapCell>(impl_->expected(reference, ValueKind::OrderedMap).data).entries;
        const auto existing = std::find_if(entries.begin(), entries.end(), [&](const auto& item) { return item.first == key; });
        if (existing != entries.end()) { existing->second = value; return; }
    }

    std::size_t old_capacity{};
    std::size_t old_size{};
    {
        const auto& before = std::get<MapCell>(impl_->expected(reference, ValueKind::OrderedMap).data).entries;
        old_capacity = before.capacity();
        old_size = before.size();
    }
    std::size_t desired = old_capacity;
    if (old_size == desired) desired = desired == 0 ? 1 : desired > std::numeric_limits<std::size_t>::max() / 2 ?
                                             std::numeric_limits<std::size_t>::max() : desired * 2;
    const auto old_vector = vector_bytes(old_capacity, sizeof(std::pair<std::string, Value>));
    const auto new_vector = vector_bytes(desired, sizeof(std::pair<std::string, Value>));
    Charge delta{new_vector - old_vector, key.capacity(), 0};
    charged_add(delta.bytes, key.capacity());
    std::size_t allocation_size = new_vector;
    charged_add(allocation_size, key.capacity());
    impl_->preflight(delta, allocation_size, false);
    impl_->stats.live_bytes += delta.bytes;
    impl_->stats.string_bytes += delta.strings;
    std::size_t reserved_bytes = delta.bytes;
    std::size_t reserved_strings = delta.strings;
    std::vector<std::pair<std::string, Value>> replacement;
    try {
        replacement.reserve(desired);
        const auto& current = std::get<MapCell>(impl_->expected(reference, ValueKind::OrderedMap).data).entries;
        replacement.insert(replacement.end(), current.begin(), current.end());
        replacement.emplace_back(std::move(key), value);
        const auto actual_vector = vector_bytes(replacement.capacity(), sizeof(std::pair<std::string, Value>));
        const auto actual_key = replacement.back().first.capacity();
        Charge actual_delta{actual_vector - old_vector, actual_key, 0};
        charged_add(actual_delta.bytes, actual_key);
        if (actual_delta.bytes > reserved_bytes || actual_delta.strings > reserved_strings) {
            Charge extra{};
            if (actual_delta.bytes > reserved_bytes) extra.bytes = actual_delta.bytes - reserved_bytes;
            if (actual_delta.strings > reserved_strings) extra.strings = actual_delta.strings - reserved_strings;
            std::size_t actual_allocation = actual_vector;
            charged_add(actual_allocation, actual_key);
            impl_->preflight(extra, actual_allocation, false);
            impl_->stats.live_bytes += extra.bytes;
            impl_->stats.string_bytes += extra.strings;
        }
        if (reserved_bytes > actual_delta.bytes) impl_->stats.live_bytes -= reserved_bytes - actual_delta.bytes;
        if (reserved_strings > actual_delta.strings)
            impl_->stats.string_bytes -= reserved_strings - actual_delta.strings;
        reserved_bytes = actual_delta.bytes;
        reserved_strings = actual_delta.strings;
    } catch (const std::bad_alloc&) {
        impl_->stats.live_bytes -= reserved_bytes;
        impl_->stats.string_bytes -= reserved_strings;
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "map growth allocation failed");
    } catch (...) {
        impl_->stats.live_bytes -= reserved_bytes;
        impl_->stats.string_bytes -= reserved_strings;
        throw;
    }
    auto& cell = impl_->expected(reference, ValueKind::OrderedMap);
    auto& entries = std::get<MapCell>(cell.data).entries;
    entries.swap(replacement);
    cell.accounted_bytes += reserved_bytes;
    cell.accounted_string_bytes += reserved_strings;
}

bool Heap::truthy(const Value value) const
{
    switch (kind(value)) {
        case ValueKind::Null: return false;
        case ValueKind::Boolean: return value.as_boolean();
        case ValueKind::Integer: return value.as_integer() != 0;
        case ValueKind::Float: return value.as_float() != 0.0;
        case ValueKind::String: return !std::get<StringCell>(impl_->dereference(value.as_heap_ref()).data).value.empty();
        case ValueKind::List: return !std::get<ListCell>(impl_->dereference(value.as_heap_ref()).data).values.empty();
        case ValueKind::OrderedMap: return !std::get<MapCell>(impl_->dereference(value.as_heap_ref()).data).entries.empty();
        default: return true;
    }
}

bool Heap::equals(const Value left, const Value right) const
{
    struct Pair { Value left; Value right; };
    struct RefPair {
        HeapRef left;
        HeapRef right;
        bool operator==(const RefPair&) const = default;
    };
    struct Hash { std::size_t operator()(const RefPair& pair) const noexcept {
        return static_cast<std::size_t>(pair.left.slot) ^ (static_cast<std::size_t>(pair.right.slot) << 1) ^
               static_cast<std::size_t>(pair.left.generation) ^ (static_cast<std::size_t>(pair.right.generation) << 3);
    }};
    std::vector<Pair> work{{left, right}};
    std::unordered_set<RefPair, Hash> visited;
    while (!work.empty()) {
        const auto pair = work.back(); work.pop_back();
        const auto left_kind = kind(pair.left);
        const auto right_kind = kind(pair.right);
        if ((left_kind == ValueKind::Integer && right_kind == ValueKind::Float) ||
            (left_kind == ValueKind::Float && right_kind == ValueKind::Integer)) {
            const double a = left_kind == ValueKind::Float ? pair.left.as_float() : static_cast<double>(pair.left.as_integer());
            const double b = right_kind == ValueKind::Float ? pair.right.as_float() : static_cast<double>(pair.right.as_integer());
            if (a != b) return false;
            continue;
        }
        if (left_kind != right_kind) return false;
        switch (left_kind) {
            case ValueKind::Null: break;
            case ValueKind::Boolean: if (pair.left.as_boolean() != pair.right.as_boolean()) return false; break;
            case ValueKind::Integer: if (pair.left.as_integer() != pair.right.as_integer()) return false; break;
            case ValueKind::Float: if (pair.left.as_float() != pair.right.as_float()) return false; break;
            case ValueKind::String: if (string_copy(pair.left.as_heap_ref()) != string_copy(pair.right.as_heap_ref())) return false; break;
            case ValueKind::List:
            case ValueKind::OrderedMap: {
                const RefPair refs{pair.left.as_heap_ref(), pair.right.as_heap_ref()};
                if (!visited.insert(refs).second) break;
                if (left_kind == ValueKind::List) {
                    const auto& a = std::get<ListCell>(impl_->dereference(refs.left).data).values;
                    const auto& b = std::get<ListCell>(impl_->dereference(refs.right).data).values;
                    if (a.size() != b.size()) return false;
                    for (std::size_t index = 0; index < a.size(); ++index) work.push_back({a[index], b[index]});
                } else {
                    const auto& a = std::get<MapCell>(impl_->dereference(refs.left).data).entries;
                    const auto& b = std::get<MapCell>(impl_->dereference(refs.right).data).entries;
                    if (a.size() != b.size()) return false;
                    for (const auto& entry : a) {
                        const auto found = std::find_if(b.begin(), b.end(), [&](const auto& item) { return item.first == entry.first; });
                        if (found == b.end()) return false;
                        work.push_back({entry.second, found->second});
                    }
                }
                break;
            }
            default:
                if (pair.left.as_heap_ref() != pair.right.as_heap_ref()) return false;
                break;
        }
    }
    return true;
}

void Heap::validate_json_safe(const Value value) const
{
    struct Frame { Value value; bool exit; };
    std::vector<Frame> work{{value, false}};
    std::unordered_set<std::uint32_t> active;
    while (!work.empty()) {
        const auto frame = work.back(); work.pop_back();
        const auto kind_value = kind(frame.value);
        if (kind_value == ValueKind::Float && !std::isfinite(frame.value.as_float()))
            throw RuntimeError(RuntimeErrorCode::JsonNonFinite, "non-finite float is not JSON-safe");
        if (kind_value != ValueKind::List && kind_value != ValueKind::OrderedMap) {
            if (kind_value == ValueKind::Null || kind_value == ValueKind::Boolean ||
                kind_value == ValueKind::Integer || kind_value == ValueKind::Float || kind_value == ValueKind::String) continue;
            throw RuntimeError(RuntimeErrorCode::JsonUnsupported, "value kind is not JSON-safe");
        }
        const auto reference = frame.value.as_heap_ref();
        if (frame.exit) { active.erase(reference.slot); continue; }
        if (!active.insert(reference.slot).second)
            throw RuntimeError(RuntimeErrorCode::JsonCycle, "cyclic value is not JSON-safe");
        work.push_back({frame.value, true});
        const auto& cell = impl_->dereference(reference);
        visit_children(cell.data, [&](const Value child) { work.push_back({child, false}); });
    }
}

bool Heap::close_host_handle(const HeapRef reference)
{
    auto& cell = impl_->expected(reference, ValueKind::HostHandle);
    auto& host = std::get<HostCell>(cell.data);
    if (host.metadata.closed) return false;
    std::size_t capacity{};
    if (!checked_add(impl_->release_queue.size(), 1, capacity))
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "release queue size overflow");
    if (capacity > impl_->limits.max_pending_release_records)
        throw RuntimeError(RuntimeErrorCode::ReleaseQueueLimitExceeded,
                           "pending host release queue limit exceeded");
    try { impl_->release_queue.reserve(capacity); }
    catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "release queue allocation failed");
    }
    impl_->queue_host_release(cell);
    return true;
}

std::vector<HostReleaseRecord> Heap::drain_release_queue()
{
    std::vector<HostReleaseRecord> result;
    result.swap(impl_->release_queue);
    return result;
}

void Heap::collect() { impl_->collect(); }

std::vector<HostReleaseRecord> Heap::teardown()
{
    if (impl_->torn_down) return drain_release_queue();
    impl_->reserve_sweep_storage(false);
    for (std::size_t index = 0; index < impl_->slots.size(); ++index) {
        if (impl_->slots[index].cell) impl_->retire_slot(static_cast<std::uint32_t>(index));
    }
    impl_->explicit_roots.clear();
    impl_->temporary_roots.clear();
    impl_->torn_down = true;
    return drain_release_queue();
}

HeapStats Heap::stats() const noexcept { return impl_->stats; }
const HeapLimits& Heap::limits() const noexcept { return impl_->limits; }
std::uint64_t Heap::identity() const noexcept { return impl_->heap_identity; }

}  // namespace baas::script::runtime
