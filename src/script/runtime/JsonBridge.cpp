#include "script/runtime/JsonBridge.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace baas::script::runtime {
namespace {

constexpr std::size_t hard_depth_ceiling = 1024;

[[nodiscard]] bool checked_add(const std::size_t left, const std::size_t right,
                               std::size_t& output) noexcept
{
    if (right > std::numeric_limits<std::size_t>::max() - left) return false;
    output = left + right;
    return true;
}

[[nodiscard]] std::size_t checked_multiply(const std::size_t left, const std::size_t right)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
        throw RuntimeError(RuntimeErrorCode::JsonByteLimitExceeded, "JSON bridge byte accounting overflow");
    return left * right;
}

class Budget {
public:
    explicit Budget(const JsonBridgeLimits limits)
        : limits_(limits), max_depth_(std::min(limits.max_depth, hard_depth_ceiling)) {}

    void enter(const std::size_t depth)
    {
        if (depth > max_depth_)
            throw RuntimeError(RuntimeErrorCode::JsonDepthLimitExceeded, "JSON bridge depth limit exceeded");
        if (visited_ >= limits_.max_nodes)
            throw RuntimeError(RuntimeErrorCode::JsonNodeLimitExceeded, "JSON bridge node limit exceeded");
        if (work_ >= limits_.max_work)
            throw RuntimeError(RuntimeErrorCode::JsonWorkLimitExceeded, "JSON bridge work limit exceeded");
        ++visited_;
        ++work_;
        add_total(1); // kind tag
    }

    void schedule_children(const std::size_t current, const std::size_t additional)
    {
        std::size_t scheduled{};
        if (!checked_add(current, additional, scheduled) || scheduled > limits_.max_nodes)
            throw RuntimeError(RuntimeErrorCode::JsonNodeLimitExceeded, "JSON bridge node limit exceeded");
        if (additional > limits_.max_work - std::min(work_, limits_.max_work))
            throw RuntimeError(RuntimeErrorCode::JsonWorkLimitExceeded,
                               "JSON bridge work limit exceeded");
        work_ += additional;
    }

    void add_string(const std::size_t bytes)
    {
        std::size_t projected{};
        if (!checked_add(strings_, bytes, projected) || projected > limits_.max_string_bytes)
            throw RuntimeError(RuntimeErrorCode::JsonStringLimitExceeded,
                               "JSON bridge string byte limit exceeded");
        strings_ = projected;
        add_total(bytes);
    }

    void add_total(const std::size_t bytes)
    {
        std::size_t projected{};
        if (!checked_add(total_, bytes, projected) || projected > limits_.max_total_bytes)
            throw RuntimeError(RuntimeErrorCode::JsonByteLimitExceeded,
                               "JSON bridge total byte limit exceeded");
        total_ = projected;
    }

private:
    JsonBridgeLimits limits_;
    std::size_t max_depth_;
    std::size_t visited_{};
    std::size_t work_{};
    std::size_t strings_{};
    std::size_t total_{};
};

struct FlatNode {
    JsonKind kind{JsonKind::Null};
    bool boolean{};
    std::int64_t integer{};
    double floating{};
    std::string string;
    std::vector<std::size_t> children;
    std::vector<std::string> keys;
};

struct RefHash {
    std::size_t operator()(const HeapRef& reference) const noexcept
    {
        auto value = static_cast<std::size_t>(reference.heap_identity);
        value ^= static_cast<std::size_t>(reference.slot) + 0x9e3779b9U + (value << 6) + (value >> 2);
        value ^= static_cast<std::size_t>(reference.generation) + 0x9e3779b9U + (value << 6) + (value >> 2);
        return value;
    }
};

[[nodiscard]] std::vector<FlatNode> flatten_heap(
    const Heap& source, const Value root, const JsonBridgeLimits limits)
{
    struct Frame {
        Value value;
        std::size_t depth;
        std::size_t node;
        bool exit;
        HeapRef reference;
    };

    Budget budget(limits);
    std::vector<FlatNode> nodes(1);
    std::vector<Frame> frames{{root, 1, 0, false, {}}};
    std::unordered_set<HeapRef, RefHash> active;

    while (!frames.empty()) {
        const auto frame = frames.back();
        frames.pop_back();
        if (frame.exit) {
            active.erase(frame.reference);
            continue;
        }

        budget.enter(frame.depth);
        auto& output = nodes[frame.node];
        const auto kind = source.kind(frame.value);
        switch (kind) {
            case ValueKind::Null:
                output.kind = JsonKind::Null;
                break;
            case ValueKind::Boolean:
                output.kind = JsonKind::Boolean;
                output.boolean = frame.value.as_boolean();
                budget.add_total(1);
                break;
            case ValueKind::Integer:
                output.kind = JsonKind::Integer;
                output.integer = frame.value.as_integer();
                budget.add_total(sizeof(std::int64_t));
                break;
            case ValueKind::Float:
                output.kind = JsonKind::Float;
                output.floating = frame.value.as_float();
                if (!std::isfinite(output.floating))
                    throw RuntimeError(RuntimeErrorCode::JsonNonFinite,
                                       "non-finite float is not JSON-safe");
                budget.add_total(sizeof(double));
                break;
            case ValueKind::String:
                output.kind = JsonKind::String;
                output.string = source.string_copy(frame.value.as_heap_ref());
                budget.add_string(output.string.size());
                break;
            case ValueKind::List: {
                const auto reference = frame.value.as_heap_ref();
                if (!active.insert(reference).second)
                    throw RuntimeError(RuntimeErrorCode::JsonCycle, "cyclic list is not JSON-safe");
                auto values = source.list_values(reference);
                budget.schedule_children(nodes.size(), values.size());
                budget.add_total(checked_multiply(values.size(), sizeof(std::size_t)));
                output.kind = JsonKind::Array;
                output.children.resize(values.size());
                const auto first = nodes.size();
                nodes.resize(first + values.size());
                for (std::size_t index = 0; index < values.size(); ++index)
                    nodes[frame.node].children[index] = first + index;
                frames.push_back({Value::null(), frame.depth, frame.node, true, reference});
                for (std::size_t index = values.size(); index-- > 0;)
                    frames.push_back({values[index], frame.depth + 1, first + index, false, {}});
                break;
            }
            case ValueKind::OrderedMap: {
                const auto reference = frame.value.as_heap_ref();
                if (!active.insert(reference).second)
                    throw RuntimeError(RuntimeErrorCode::JsonCycle, "cyclic map is not JSON-safe");
                auto entries = source.map_entries(reference);
                budget.schedule_children(nodes.size(), entries.size());
                budget.add_total(checked_multiply(entries.size(), sizeof(std::size_t)));
                output.kind = JsonKind::Object;
                output.children.resize(entries.size());
                output.keys.reserve(entries.size());
                for (const auto& entry : entries) {
                    budget.add_string(entry.first.size());
                    output.keys.push_back(entry.first);
                }
                const auto first = nodes.size();
                nodes.resize(first + entries.size());
                for (std::size_t index = 0; index < entries.size(); ++index)
                    nodes[frame.node].children[index] = first + index;
                frames.push_back({Value::null(), frame.depth, frame.node, true, reference});
                for (std::size_t index = entries.size(); index-- > 0;)
                    frames.push_back({entries[index].second, frame.depth + 1, first + index, false, {}});
                break;
            }
            default:
                throw RuntimeError(RuntimeErrorCode::JsonUnsupported,
                                   "identity-bearing value is not JSON-safe");
        }
    }
    return nodes;
}

[[nodiscard]] JsonValue materialize_json(std::vector<FlatNode> nodes)
{
    std::vector<JsonValue> converted(nodes.size());
    for (std::size_t index = nodes.size(); index-- > 0;) {
        auto& node = nodes[index];
        switch (node.kind) {
            case JsonKind::Null: converted[index] = JsonValue(); break;
            case JsonKind::Boolean: converted[index] = JsonValue(node.boolean); break;
            case JsonKind::Integer: converted[index] = JsonValue(node.integer); break;
            case JsonKind::Float: converted[index] = JsonValue(node.floating); break;
            case JsonKind::String: converted[index] = JsonValue(std::move(node.string)); break;
            case JsonKind::Array: {
                JsonArray values;
                values.reserve(node.children.size());
                for (const auto child : node.children) values.push_back(std::move(converted[child]));
                converted[index] = JsonValue(std::move(values));
                break;
            }
            case JsonKind::Object: {
                JsonObject entries;
                entries.reserve(node.children.size());
                for (std::size_t child = 0; child < node.children.size(); ++child)
                    entries.emplace_back(std::move(node.keys[child]),
                                         std::move(converted[node.children[child]]));
                converted[index] = JsonValue(std::move(entries));
                break;
            }
        }
    }
    return std::move(converted.front());
}

struct InputNode {
    const JsonValue* value{};
    std::vector<std::size_t> children;
};

[[nodiscard]] std::vector<InputNode> flatten_json(
    const JsonValue& root, const JsonBridgeLimits limits)
{
    struct Frame { const JsonValue* value; std::size_t depth; std::size_t node; };
    Budget budget(limits);
    std::vector<InputNode> nodes{{&root, {}}};
    std::vector<Frame> frames{{&root, 1, 0}};

    while (!frames.empty()) {
        const auto frame = frames.back();
        frames.pop_back();
        budget.enter(frame.depth);
        const auto kind = frame.value->kind();
        switch (kind) {
            case JsonKind::Null: break;
            case JsonKind::Boolean: budget.add_total(1); break;
            case JsonKind::Integer: budget.add_total(sizeof(std::int64_t)); break;
            case JsonKind::Float:
                if (!std::isfinite(std::get<double>(frame.value->storage)))
                    throw RuntimeError(RuntimeErrorCode::JsonNonFinite,
                                       "non-finite float is not JSON-safe");
                budget.add_total(sizeof(double));
                break;
            case JsonKind::String:
                budget.add_string(std::get<std::string>(frame.value->storage).size());
                break;
            case JsonKind::Array: {
                const auto& values = std::get<JsonArray>(frame.value->storage);
                budget.schedule_children(nodes.size(), values.size());
                budget.add_total(checked_multiply(values.size(), sizeof(std::size_t)));
                nodes[frame.node].children.resize(values.size());
                const auto first = nodes.size();
                nodes.resize(first + values.size());
                for (std::size_t index = 0; index < values.size(); ++index) {
                    nodes[frame.node].children[index] = first + index;
                    nodes[first + index].value = &values[index];
                }
                for (std::size_t index = values.size(); index-- > 0;)
                    frames.push_back({&values[index], frame.depth + 1, first + index});
                break;
            }
            case JsonKind::Object: {
                const auto& entries = std::get<JsonObject>(frame.value->storage);
                std::unordered_set<std::string_view> keys;
                keys.reserve(entries.size());
                for (const auto& entry : entries) {
                    if (!keys.insert(entry.first).second)
                        throw RuntimeError(RuntimeErrorCode::JsonDuplicateKey,
                                           "duplicate JSON object key");
                }
                budget.schedule_children(nodes.size(), entries.size());
                budget.add_total(checked_multiply(entries.size(), sizeof(std::size_t)));
                nodes[frame.node].children.resize(entries.size());
                const auto first = nodes.size();
                nodes.resize(first + entries.size());
                for (std::size_t index = 0; index < entries.size(); ++index) {
                    budget.add_string(entries[index].first.size());
                    nodes[frame.node].children[index] = first + index;
                    nodes[first + index].value = &entries[index].second;
                }
                for (std::size_t index = entries.size(); index-- > 0;)
                    frames.push_back({&entries[index].second, frame.depth + 1, first + index});
                break;
            }
        }
    }
    return nodes;
}

[[nodiscard]] Value materialize_heap(Heap& destination, const std::vector<InputNode>& nodes)
{
    std::vector<Value> converted(nodes.size());
    auto roots = destination.root_scope();
    for (std::size_t index = nodes.size(); index-- > 0;) {
        const auto& node = nodes[index];
        const auto& json = *node.value;
        switch (json.kind()) {
            case JsonKind::Null: converted[index] = Value::null(); break;
            case JsonKind::Boolean: converted[index] = Value(std::get<bool>(json.storage)); break;
            case JsonKind::Integer: converted[index] = Value(std::get<std::int64_t>(json.storage)); break;
            case JsonKind::Float: converted[index] = Value(std::get<double>(json.storage)); break;
            case JsonKind::String:
                converted[index] = destination.allocate_string(std::get<std::string>(json.storage));
                roots.add(converted[index]);
                break;
            case JsonKind::Array: {
                std::vector<Value> values;
                values.reserve(node.children.size());
                for (const auto child : node.children) values.push_back(converted[child]);
                converted[index] = destination.allocate_list(std::move(values));
                roots.add(converted[index]);
                break;
            }
            case JsonKind::Object: {
                const auto& source_entries = std::get<JsonObject>(json.storage);
                std::vector<std::pair<std::string, Value>> entries;
                entries.reserve(node.children.size());
                for (std::size_t child = 0; child < node.children.size(); ++child)
                    entries.emplace_back(source_entries[child].first, converted[node.children[child]]);
                converted[index] = destination.allocate_map(std::move(entries));
                roots.add(converted[index]);
                break;
            }
        }
    }
    return converted.front();
}

}  // namespace

JsonKind JsonValue::kind() const noexcept
{
    return static_cast<JsonKind>(storage.index());
}

JsonValue heap_value_to_json(const Heap& source, const Value value, const JsonBridgeLimits limits)
{
    try {
        return materialize_json(flatten_heap(source, value, limits));
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "JSON bridge native allocation failed");
    }
}

Value json_to_heap_value(Heap& destination, const JsonValue& value, const JsonBridgeLimits limits)
{
    try {
        return materialize_heap(destination, flatten_json(value, limits));
    } catch (const std::bad_alloc&) {
        throw RuntimeError(RuntimeErrorCode::MemoryLimitExceeded, "JSON bridge native allocation failed");
    }
}

Value deep_copy_json_value(const Heap& source, const Value value, Heap& destination,
                           const JsonBridgeLimits limits)
{
    if (source.identity() == destination.identity())
        throw RuntimeError(RuntimeErrorCode::CrossHeapReference,
                           "checked transfer requires distinct source and destination heaps");
    auto json = heap_value_to_json(source, value, limits);
    return json_to_heap_value(destination, json, limits);
}

}  // namespace baas::script::runtime
