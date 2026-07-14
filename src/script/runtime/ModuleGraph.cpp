#include "script/runtime/ModuleGraph.h"

#include <algorithm>
#include <functional>
#include <map>
#include <queue>
#include <utility>

namespace baas::script::runtime {
namespace {

struct WorkBudget {
    std::size_t used{};
    std::size_t limit{};

    void charge()
    {
        if (used >= limit) {
            throw ModuleGraphError(
                ModuleGraphErrorCode::ValidationWorkLimitExceeded,
                "module graph validation work limit exceeded");
        }
        ++used;
    }
};

[[nodiscard]] bool has_self_edge(
    const std::size_t node,
    const std::vector<std::vector<std::size_t>>& edges)
{
    return std::find(edges[node].begin(), edges[node].end(), node) != edges[node].end();
}

[[nodiscard]] std::vector<std::string> find_cycle(
    const std::vector<std::size_t>& component,
    const std::vector<std::string>& ids,
    const std::vector<std::vector<std::size_t>>& edges,
    WorkBudget& work)
{
    const auto start = *std::min_element(component.begin(), component.end());
    std::vector<bool> member(ids.size(), false);
    for (const auto node : component) member[node] = true;

    struct Frame {
        std::size_t node{};
        std::size_t next_edge{};
    };
    std::vector<bool> visited(ids.size(), false);
    std::vector<std::size_t> path{start};
    std::vector<Frame> stack{{start, 0}};
    visited[start] = true;

    while (!stack.empty()) {
        work.charge();
        auto& frame = stack.back();
        if (frame.next_edge >= edges[frame.node].size()) {
            stack.pop_back();
            path.pop_back();
            continue;
        }
        const auto target = edges[frame.node][frame.next_edge++];
        if (!member[target]) continue;
        if (target == start) {
            std::vector<std::string> cycle;
            cycle.reserve(path.size() + 1);
            for (const auto node : path) cycle.push_back(ids[node]);
            cycle.push_back(ids[start]);
            return cycle;
        }
        if (visited[target]) continue;
        visited[target] = true;
        path.push_back(target);
        stack.push_back({target, 0});
    }
    throw std::logic_error("strongly connected component did not contain a cycle");
}

}  // namespace

std::string_view module_graph_error_code_name(const ModuleGraphErrorCode code) noexcept
{
    using enum ModuleGraphErrorCode;
    switch (code) {
        case ModuleLimitExceeded: return "MG001_MODULE_LIMIT_EXCEEDED";
        case ImportEdgeLimitExceeded: return "MG002_IMPORT_EDGE_LIMIT_EXCEEDED";
        case ValidationWorkLimitExceeded: return "MG003_VALIDATION_WORK_LIMIT_EXCEEDED";
        case DuplicateModule: return "MG004_DUPLICATE_MODULE";
        case HostModuleDefinition: return "MG005_HOST_MODULE_DEFINITION";
        case MissingModule: return "MG006_MISSING_MODULE";
        case ImportCycle: return "MG007_IMPORT_CYCLE";
    }
    return "MG000_UNKNOWN";
}

ModuleGraphError::ModuleGraphError(
    const ModuleGraphErrorCode code,
    std::string message,
    std::string module,
    std::vector<std::string> cycle)
    : std::runtime_error(std::move(message)), code_(code), module_(std::move(module)),
      cycle_(std::move(cycle))
{
}

ValidatedModuleGraph validate_module_graph(
    const std::vector<ModuleDefinition>& modules,
    const NfcPredicate is_nfc,
    const ModuleGraphLimits limits)
{
    using enum ModuleGraphErrorCode;
    if (limits.max_modules == 0 || limits.max_import_edges == 0
        || limits.max_validation_work == 0) {
        throw std::invalid_argument("module graph limits must be positive");
    }
    if (modules.size() > limits.max_modules) {
        throw ModuleGraphError(ModuleLimitExceeded, "module count exceeds graph limit");
    }

    WorkBudget work{0, limits.max_validation_work};
    std::map<std::string, const ModuleDefinition*, std::less<>> ordered;
    for (const auto& module : modules) {
        work.charge();
        const auto specifier = validate_module_specifier(
            module.canonical_id, is_nfc, limits.specifier);
        if (specifier.kind != ModuleKind::Package) {
            throw ModuleGraphError(
                HostModuleDefinition,
                "host module cannot be defined by a package",
                module.canonical_id);
        }
        const auto [iterator, inserted] = ordered.emplace(specifier.canonical_id, &module);
        if (!inserted) {
            throw ModuleGraphError(
                DuplicateModule,
                "duplicate package module definition",
                iterator->first);
        }
    }

    std::vector<std::string> ids;
    std::vector<const ModuleDefinition*> definitions;
    ids.reserve(ordered.size());
    definitions.reserve(ordered.size());
    std::map<std::string, std::size_t, std::less<>> indices;
    for (const auto& [id, definition] : ordered) {
        indices.emplace(id, ids.size());
        ids.push_back(id);
        definitions.push_back(definition);
    }

    std::vector<std::vector<std::size_t>> edges(ids.size());
    std::vector<std::vector<std::size_t>> reverse_edges(ids.size());
    std::size_t total_edges = 0;
    std::size_t package_edges = 0;
    std::size_t host_edges = 0;
    for (std::size_t source = 0; source < definitions.size(); ++source) {
        for (const auto& imported_id : definitions[source]->imports) {
            work.charge();
            if (total_edges >= limits.max_import_edges) {
                throw ModuleGraphError(
                    ImportEdgeLimitExceeded,
                    "import edge count exceeds graph limit",
                    ids[source]);
            }
            ++total_edges;
            const auto imported = validate_module_specifier(
                imported_id, is_nfc, limits.specifier);
            if (imported.kind == ModuleKind::Host) {
                ++host_edges;
                continue;
            }
            const auto target = indices.find(imported.canonical_id);
            if (target == indices.end()) {
                throw ModuleGraphError(
                    MissingModule,
                    "imported package module is absent from the graph",
                    imported.canonical_id);
            }
            ++package_edges;
            edges[source].push_back(target->second);
            reverse_edges[target->second].push_back(source);
        }
    }

    // Iterative Kosaraju traversal avoids consuming the native stack on the
    // maximum permitted module graph.
    struct DfsFrame {
        std::size_t node{};
        std::size_t next_edge{};
    };
    std::vector<bool> visited(ids.size(), false);
    std::vector<std::size_t> finish_order;
    finish_order.reserve(ids.size());
    for (std::size_t root = 0; root < ids.size(); ++root) {
        if (visited[root]) continue;
        visited[root] = true;
        std::vector<DfsFrame> stack{{root, 0}};
        while (!stack.empty()) {
            work.charge();
            auto& frame = stack.back();
            if (frame.next_edge < edges[frame.node].size()) {
                const auto target = edges[frame.node][frame.next_edge++];
                if (!visited[target]) {
                    visited[target] = true;
                    stack.push_back({target, 0});
                }
                continue;
            }
            finish_order.push_back(frame.node);
            stack.pop_back();
        }
    }

    std::fill(visited.begin(), visited.end(), false);
    std::vector<std::size_t> selected_component;
    std::size_t selected_min = ids.size();
    for (auto iterator = finish_order.rbegin(); iterator != finish_order.rend(); ++iterator) {
        const auto root = *iterator;
        if (visited[root]) continue;
        std::vector<std::size_t> component;
        std::vector<std::size_t> stack{root};
        visited[root] = true;
        while (!stack.empty()) {
            work.charge();
            const auto node = stack.back();
            stack.pop_back();
            component.push_back(node);
            for (const auto target : reverse_edges[node]) {
                if (!visited[target]) {
                    visited[target] = true;
                    stack.push_back(target);
                }
            }
        }
        const bool cyclic = component.size() > 1
            || (component.size() == 1 && has_self_edge(component.front(), edges));
        if (!cyclic) continue;
        const auto minimum = *std::min_element(component.begin(), component.end());
        if (minimum < selected_min) {
            selected_min = minimum;
            selected_component = std::move(component);
        }
    }

    if (!selected_component.empty()) {
        auto cycle = find_cycle(selected_component, ids, edges, work);
        const auto primary_module = cycle.front();
        throw ModuleGraphError(
            ImportCycle,
            "package import graph contains a cycle",
            primary_module,
            std::move(cycle));
    }

    // Kahn over dependency counts emits dependencies before importers. A
    // min-heap makes independent ready nodes bytewise deterministic.
    std::vector<std::size_t> remaining_dependencies(ids.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(ids.size());
    for (std::size_t importer = 0; importer < edges.size(); ++importer) {
        remaining_dependencies[importer] = edges[importer].size();
        for (const auto dependency : edges[importer]) {
            dependents[dependency].push_back(importer);
        }
    }
    std::priority_queue<
        std::size_t,
        std::vector<std::size_t>,
        std::greater<>> ready;
    for (std::size_t node = 0; node < ids.size(); ++node) {
        if (remaining_dependencies[node] == 0) ready.push(node);
    }

    ValidatedModuleGraph result;
    result.initialization_order.reserve(ids.size());
    result.package_import_edges = package_edges;
    result.host_import_edges = host_edges;
    while (!ready.empty()) {
        work.charge();
        const auto dependency = ready.top();
        ready.pop();
        result.initialization_order.push_back(ids[dependency]);
        for (const auto importer : dependents[dependency]) {
            if (--remaining_dependencies[importer] == 0) ready.push(importer);
        }
    }
    if (result.initialization_order.size() != ids.size()) {
        throw std::logic_error("acyclic module graph did not produce a complete order");
    }
    result.validation_work = work.used;
    return result;
}

}  // namespace baas::script::runtime
