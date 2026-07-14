#pragma once

#include "script/runtime/ModuleSpecifier.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace baas::script::runtime {

enum class ModuleGraphErrorCode {
    ModuleLimitExceeded,
    ImportEdgeLimitExceeded,
    ValidationWorkLimitExceeded,
    DuplicateModule,
    HostModuleDefinition,
    MissingModule,
    ImportCycle,
};

[[nodiscard]] std::string_view module_graph_error_code_name(
    ModuleGraphErrorCode code) noexcept;

class ModuleGraphError final : public std::runtime_error {
public:
    ModuleGraphError(
        ModuleGraphErrorCode code,
        std::string message,
        std::string module = {},
        std::vector<std::string> cycle = {});

    [[nodiscard]] ModuleGraphErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& module() const noexcept { return module_; }
    [[nodiscard]] const std::vector<std::string>& cycle() const noexcept { return cycle_; }

private:
    ModuleGraphErrorCode code_;
    std::string module_;
    std::vector<std::string> cycle_;
};

struct ModuleDefinition {
    std::string canonical_id;
    // Import order is source order and is retained for deterministic cycle
    // reporting. Host imports participate in validation but not the package DAG.
    std::vector<std::string> imports;
};

struct ModuleGraphLimits {
    std::size_t max_modules{4'096};
    std::size_t max_import_edges{65'536};
    std::size_t max_validation_work{1'000'000};
    ModuleSpecifierLimits specifier{};
};

struct ValidatedModuleGraph {
    // Dependencies precede their importers. Independent ready modules use
    // canonical bytewise lexical order.
    std::vector<std::string> initialization_order;
    std::size_t package_import_edges{};
    std::size_t host_import_edges{};
    std::size_t validation_work{};
};

// Validates an already extracted manifest/source graph without executing code
// or probing the filesystem. ModuleSpecifierError is propagated for malformed
// logical IDs; graph failures use ModuleGraphError.
[[nodiscard]] ValidatedModuleGraph validate_module_graph(
    const std::vector<ModuleDefinition>& modules,
    NfcPredicate is_nfc = nullptr,
    ModuleGraphLimits limits = {});

}  // namespace baas::script::runtime
