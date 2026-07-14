#pragma once

#include "script/runtime/ValueHeap.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace baas::script::runtime {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
// A vector is intentional: object insertion order is part of the bridge ABI.
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

enum class JsonKind { Null, Boolean, Integer, Float, String, Array, Object };

struct JsonValue {
    using Storage = std::variant<std::monostate, bool, std::int64_t, double,
                                 std::string, JsonArray, JsonObject>;

    JsonValue() noexcept = default;
    explicit JsonValue(bool value) : storage(std::move(value)) {}
    explicit JsonValue(std::int64_t value) : storage(value) {}
    explicit JsonValue(double value) : storage(value) {}
    explicit JsonValue(std::string value) : storage(std::move(value)) {}
    explicit JsonValue(const char* value) : storage(std::string(value)) {}
    explicit JsonValue(JsonArray value) : storage(std::move(value)) {}
    explicit JsonValue(JsonObject value) : storage(std::move(value)) {}

    [[nodiscard]] JsonKind kind() const noexcept;
    [[nodiscard]] const Storage& value() const noexcept { return storage; }
    [[nodiscard]] Storage& value() noexcept { return storage; }

    friend bool operator==(const JsonValue&, const JsonValue&) = default;

    Storage storage;
};

struct JsonBridgeLimits {
    std::size_t max_depth{256};
    std::size_t max_nodes{100'000};
    std::size_t max_string_bytes{16U * 1024U * 1024U};
    std::size_t max_total_bytes{64U * 1024U * 1024U};
    std::size_t max_work{500'000};
};

// Logical bridge bytes are deterministic: one byte per node, primitive payload
// bytes, UTF-8 string/key bytes, and sizeof(size_t) per array/object value slot.
// Work is one unit per visited node and child edge. The implementation clamps
// max_depth to 1024 before traversing hostile input.
[[nodiscard]] JsonValue heap_value_to_json(
    const Heap& source, Value value, JsonBridgeLimits limits = {});

// The returned Value is not an explicit root. No allocation occurs after its
// final publication, so callers can immediately register it in their context.
[[nodiscard]] Value json_to_heap_value(
    Heap& destination, const JsonValue& value, JsonBridgeLimits limits = {});

// Cross-context copy is JSON-safe only and always duplicates mutable lists/maps.
// Source and destination must be distinct heaps.
[[nodiscard]] Value deep_copy_json_value(
    const Heap& source, Value value, Heap& destination,
    JsonBridgeLimits limits = {});

}  // namespace baas::script::runtime
