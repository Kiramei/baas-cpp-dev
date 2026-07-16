#include "script/Ast.h"
#include "script/SemanticAnalyzer.h"
#include "script/Parser.h"
#include "script/runtime/JsonBridge.h"
#include "script/runtime/ModuleGraph.h"
#include "script/runtime/ModuleSpecifier.h"
#include "script/runtime/SynchronousEvaluator.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace script = baas::script;
namespace runtime = baas::script::runtime;

namespace {

constexpr std::string_view schema_version = "1";
constexpr std::string_view engine = "synchronous_ast_conformance";

struct Options {
    fs::path package_root;
    std::string entry;
    std::string export_name{"result"};
    runtime::EvaluatorLimits limits{};
    runtime::HeapLimits heap_limits{};
    runtime::JsonBridgeLimits json_limits{};
    std::size_t max_json_output_bytes{64U * 1024U * 1024U};
    std::size_t max_loader_path_bytes{32U * 1024U};
    std::size_t max_loader_depth{128};
    std::size_t max_loader_work{1'000'000};
};

struct RunnerError final : std::runtime_error {
    RunnerError(std::string phase, std::string code, std::string message,
                std::string module = {}, script::SourceSpan span = {})
        : std::runtime_error(std::move(message)), phase(std::move(phase)),
          code(std::move(code)), module(std::move(module)), span(span) {}
    std::string phase;
    std::string code;
    std::string module;
    script::SourceSpan span{};
};

void check_output_size(const std::string& out, const std::size_t limit)
{
    if (out.size() > limit) {
        throw RunnerError("result", "RUN021_OUTPUT_LIMIT_EXCEEDED",
                          "JSON output byte limit exceeded");
    }
}

void append_json_string(std::string& out, const std::string_view value,
                        const std::size_t limit = std::numeric_limits<std::size_t>::max())
{
    static constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) {
                    out += "\\u00";
                    out.push_back(hex[c >> 4U]);
                    out.push_back(hex[c & 0x0fU]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    check_output_size(out, limit);
}

void append_size(std::string& out, const std::size_t value)
{
    char buffer[32]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    out.append(buffer, result.ptr);
}

void append_json_value(std::string& out, const runtime::JsonValue& value,
                       const std::size_t limit = std::numeric_limits<std::size_t>::max(),
                       const std::size_t depth = 0,
                       const std::size_t depth_limit = 1'024)
{
    if (depth > depth_limit) {
        throw RunnerError("result", "RUN021_OUTPUT_LIMIT_EXCEEDED",
                          "JSON output depth limit exceeded");
    }
    switch (value.kind()) {
        case runtime::JsonKind::Null: out += "null"; break;
        case runtime::JsonKind::Boolean:
            out += std::get<bool>(value.value()) ? "true" : "false";
            break;
        case runtime::JsonKind::Integer: {
            char buffer[32]{};
            const auto integer = std::get<std::int64_t>(value.value());
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), integer);
            out.append(buffer, result.ptr);
            break;
        }
        case runtime::JsonKind::Float: {
            char buffer[64]{};
            const auto floating = std::get<double>(value.value());
            const auto result = std::to_chars(
                buffer, buffer + sizeof(buffer), floating, std::chars_format::general,
                std::numeric_limits<double>::max_digits10);
            if (result.ec != std::errc{} || !std::isfinite(floating)) {
                throw RunnerError("result", "RUN020_RESULT_NOT_JSON", "result is not JSON-safe");
            }
            out.append(buffer, result.ptr);
            break;
        }
        case runtime::JsonKind::String:
            append_json_string(out, std::get<std::string>(value.value()), limit);
            break;
        case runtime::JsonKind::Array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : std::get<runtime::JsonArray>(value.value())) {
                if (!first) out.push_back(',');
                first = false;
                append_json_value(out, item, limit, depth + 1, depth_limit);
            }
            out.push_back(']');
            break;
        }
        case runtime::JsonKind::Object: {
            out.push_back('{');
            bool first = true;
            for (const auto& [key, item] : std::get<runtime::JsonObject>(value.value())) {
                if (!first) out.push_back(',');
                first = false;
                append_json_string(out, key, limit);
                out.push_back(':');
                append_json_value(out, item, limit, depth + 1, depth_limit);
            }
            out.push_back('}');
            break;
        }
    }
    check_output_size(out, limit);
}

std::string error_json(const std::string_view phase, const std::string_view code,
                       const std::string_view message, const std::string_view module = {},
                       const script::SourceSpan span = {},
                       const std::optional<std::size_t> steps = std::nullopt)
{
    const auto clipped = [](const std::string_view value) {
        constexpr std::size_t limit = 256;
        constexpr std::string_view replacement = "\xef\xbf\xbd";
        std::string result;
        result.reserve(std::min(value.size(), limit));
        std::size_t offset = 0;
        while (offset < value.size()) {
            const auto first = static_cast<unsigned char>(value[offset]);
            std::size_t length = 0;
            bool valid = false;
            if (first <= 0x7fU) {
                length = 1;
                valid = true;
            } else if (first >= 0xc2U && first <= 0xdfU && offset + 1 < value.size()) {
                length = 2;
                valid = (static_cast<unsigned char>(value[offset + 1]) & 0xc0U) == 0x80U;
            } else if (first >= 0xe0U && first <= 0xefU && offset + 2 < value.size()) {
                const auto second = static_cast<unsigned char>(value[offset + 1]);
                const auto third = static_cast<unsigned char>(value[offset + 2]);
                length = 3;
                valid = (third & 0xc0U) == 0x80U
                    && ((first == 0xe0U && second >= 0xa0U && second <= 0xbfU)
                        || (first == 0xedU && second >= 0x80U && second <= 0x9fU)
                        || ((first >= 0xe1U && first <= 0xecU)
                            || (first >= 0xeeU && first <= 0xefU))
                               && (second & 0xc0U) == 0x80U);
            } else if (first >= 0xf0U && first <= 0xf4U && offset + 3 < value.size()) {
                const auto second = static_cast<unsigned char>(value[offset + 1]);
                const auto third = static_cast<unsigned char>(value[offset + 2]);
                const auto fourth = static_cast<unsigned char>(value[offset + 3]);
                length = 4;
                valid = (third & 0xc0U) == 0x80U && (fourth & 0xc0U) == 0x80U
                    && ((first == 0xf0U && second >= 0x90U && second <= 0xbfU)
                        || (first >= 0xf1U && first <= 0xf3U && (second & 0xc0U) == 0x80U)
                        || (first == 0xf4U && second >= 0x80U && second <= 0x8fU));
            }
            const auto addition = valid ? std::string_view(value.data() + offset, length) : replacement;
            if (result.size() + addition.size() + 3 > limit) break;
            result.append(addition);
            offset += valid ? length : 1;
        }
        if (offset < value.size()) result += "...";
        return result;
    };
    const auto safe_phase = clipped(phase);
    const auto safe_code = clipped(code);
    const auto safe_message = clipped(message);
    const auto safe_module = clipped(module);
    std::string out{"{\"schema_version\":"};
    out += schema_version;
    out += ",\"engine\":";
    append_json_string(out, engine);
    out += ",\"ok\":false,\"error\":{\"phase\":";
    append_json_string(out, safe_phase);
    out += ",\"code\":";
    append_json_string(out, safe_code);
    out += ",\"message\":";
    append_json_string(out, safe_message);
    if (!safe_module.empty()) {
        out += ",\"module\":";
        append_json_string(out, safe_module);
    }
    if (span.begin.byte_offset != 0 || span.end.byte_offset != 0
        || span.begin.line != 1 || span.end.line != 1
        || span.begin.column != 1 || span.end.column != 1) {
        out += ",\"span\":{\"begin\":{\"byte\":";
        append_size(out, span.begin.byte_offset);
        out += ",\"line\":"; append_size(out, span.begin.line);
        out += ",\"column\":"; append_size(out, span.begin.column);
        out += "},\"end\":{\"byte\":"; append_size(out, span.end.byte_offset);
        out += ",\"line\":"; append_size(out, span.end.line);
        out += ",\"column\":"; append_size(out, span.end.column);
        out += "}}";
    }
    if (steps) {
        out += ",\"steps\":";
        append_size(out, *steps);
    }
    out += "}}\n";
    return out;
}

[[noreturn]] void usage_error(const std::string& message)
{
    throw RunnerError("usage", "RUN001_INVALID_ARGUMENT", message);
}

std::size_t parse_size(const std::string_view text, const std::string_view name)
{
    std::size_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || value == 0) {
        usage_error(std::string(name) + " must be a positive integer");
    }
    return value;
}

Options parse_options(const int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            std::cout
                << "BAAS_script_run --package-root DIR --entry MODULE [--export NAME]\n"
                   "                [--max-steps N] [--max-call-depth N]\n"
                   "                [--max-module-bytes N] [--max-total-bytes N]\n"
                   "                [--max-modules N] [--max-json-nodes N]\n"
                   "                [--max-json-output-bytes N]\n"
                   "                [--max-loader-path-bytes N]\n"
                   "                [--max-loader-depth N] [--max-loader-work N]\n\n"
                   "Executes the bounded synchronous AST conformance evaluator.\n"
                   "It is not the production VM and exposes no Host modules.\n";
            std::exit(0);
        }
        if (index + 1 >= argc) usage_error("option requires a value");
        const std::string_view value(argv[++index]);
        if (argument == "--package-root") options.package_root = fs::path(std::string(value));
        else if (argument == "--entry") options.entry = value;
        else if (argument == "--export") options.export_name = value;
        else if (argument == "--max-steps") options.limits.max_steps = parse_size(value, argument);
        else if (argument == "--max-call-depth") options.limits.max_call_depth = parse_size(value, argument);
        else if (argument == "--max-module-bytes") options.limits.max_module_source_bytes = parse_size(value, argument);
        else if (argument == "--max-total-bytes") options.limits.max_total_source_bytes = parse_size(value, argument);
        else if (argument == "--max-modules") options.limits.max_modules = parse_size(value, argument);
        else if (argument == "--max-json-nodes") options.json_limits.max_nodes = parse_size(value, argument);
        else if (argument == "--max-json-output-bytes") options.max_json_output_bytes = parse_size(value, argument);
        else if (argument == "--max-loader-path-bytes") options.max_loader_path_bytes = parse_size(value, argument);
        else if (argument == "--max-loader-depth") options.max_loader_depth = parse_size(value, argument);
        else if (argument == "--max-loader-work") options.max_loader_work = parse_size(value, argument);
        else usage_error("unknown argument");
    }
    if (options.package_root.empty()) usage_error("--package-root is required");
    if (options.entry.empty()) usage_error("--entry is required");
    if (options.export_name.empty()) usage_error("--export must not be empty");
    const auto identifier_start = [](const unsigned char value) {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    };
    const auto identifier_continue = [&](const unsigned char value) {
        return identifier_start(value) || (value >= '0' && value <= '9') || value == '_';
    };
    if (!identifier_start(static_cast<unsigned char>(options.export_name.front()))
        || !std::all_of(options.export_name.begin() + 1, options.export_name.end(),
                        [&](const char value) {
                            return identifier_continue(static_cast<unsigned char>(value));
                        })) {
        usage_error("--export must be an ASCII public identifier");
    }
    return options;
}

class PackageLoader final {
public:
    explicit PackageLoader(const Options& options) : options_(options)
    {
        std::error_code error;
        const auto root_status = fs::symlink_status(options.package_root, error);
        if (error || fs::is_symlink(root_status)) {
            throw RunnerError("load", "RUN002_PACKAGE_ROOT_INVALID",
                              "package root must not be a filesystem link");
        }
        const auto absolute_root = fs::absolute(options.package_root, error).lexically_normal();
        if (error) {
            throw RunnerError("load", "RUN002_PACKAGE_ROOT_INVALID",
                              "package root is not a readable directory");
        }
        root_ = fs::canonical(options.package_root, error);
        if (error || !fs::is_directory(root_, error) || error) {
            throw RunnerError("load", "RUN002_PACKAGE_ROOT_INVALID",
                              "package root is not a readable directory");
        }
        if (absolute_root != root_) {
            throw RunnerError("load", "RUN002_PACKAGE_ROOT_INVALID",
                              "package root must not contain a filesystem alias");
        }
    }

    std::vector<runtime::SourceModule> load()
    {
        const auto entry = checked_specifier(options_.entry, "entry module");
        if (entry.kind != runtime::ModuleKind::Package) {
            throw RunnerError("load", "RUN006_HOST_IMPORT_UNAVAILABLE",
                              "Host modules are unavailable in the conformance runner", options_.entry);
        }
        visit(entry.canonical_id, 1);

        std::vector<runtime::ModuleDefinition> definitions;
        std::vector<runtime::SourceModule> sources;
        definitions.reserve(records_.size());
        sources.reserve(records_.size());
        for (const auto& [id, record] : records_) {
            definitions.push_back({id, record.imports});
            sources.push_back({id, record.source});
        }
        runtime::ModuleGraphLimits graph_limits;
        graph_limits.max_modules = options_.limits.max_modules;
        graph_limits.max_import_edges = options_.limits.max_collection_work;
        graph_limits.max_validation_work = std::max(
            options_.limits.max_collection_work, options_.limits.max_modules);
        static_cast<void>(runtime::validate_module_graph(definitions, nullptr, graph_limits));
        return sources;
    }

private:
    struct Record { std::string source; std::vector<std::string> imports; };

    runtime::ModuleSpecifier checked_specifier(const std::string_view id,
                                               const std::string_view subject) const
    {
        try {
            return runtime::validate_module_specifier(id);
        } catch (const runtime::ModuleSpecifierError& error) {
            throw RunnerError("load", std::string(runtime::module_specifier_error_code_name(error.code())),
                              std::string(subject) + " is not canonical");
        }
    }

    std::string read_source(const std::string& id)
    {
        const auto specifier = checked_specifier(id, "module id");
        const fs::path relative = fs::path(specifier.manifest_source_path());
        if (root_.generic_u8string().size() + relative.generic_u8string().size() + 1
            > options_.max_loader_path_bytes) {
            throw RunnerError("load", "RUN008_LOADER_LIMIT_EXCEEDED",
                              "loader path byte limit exceeded", id);
        }
        const fs::path requested = root_ / relative;
        fs::path exact = root_;
        for (const auto& component : relative) {
            charge_work(id);
            bool found = false;
            std::error_code directory_error;
            for (fs::directory_iterator item(exact, directory_error), end; !directory_error && item != end;
                 item.increment(directory_error)) {
                charge_work(id);
                if (item->path().filename().generic_u8string() == component.generic_u8string()) {
                    const auto status = item->symlink_status(directory_error);
                    if (directory_error || fs::is_symlink(status)) {
                        throw RunnerError("load", "RUN004_PATH_ESCAPE_OR_ALIAS",
                                          "module source contains a filesystem link", id);
                    }
                    exact /= item->path().filename();
                    found = true;
                    break;
                }
            }
            if (!found || directory_error) {
                throw RunnerError("load", "RUN003_MODULE_NOT_FOUND",
                                  "package module source is missing or has noncanonical case", id);
            }
        }
        std::error_code error;
        const fs::path resolved = fs::canonical(requested, error);
        if (error) {
            throw RunnerError("load", "RUN003_MODULE_NOT_FOUND",
                              "package module source is missing", id);
        }
        const fs::path actual_relative = resolved.lexically_relative(root_);
        if (actual_relative.empty() || actual_relative.is_absolute()
            || *actual_relative.begin() == fs::path("..")
            || actual_relative.generic_u8string() != relative.generic_u8string()) {
            throw RunnerError("load", "RUN004_PATH_ESCAPE_OR_ALIAS",
                              "module source escapes or aliases the package root", id);
        }
        if (!fs::is_regular_file(resolved, error) || error) {
            throw RunnerError("load", "RUN003_MODULE_NOT_FOUND",
                              "package module source is not a regular file", id);
        }
        const auto size = fs::file_size(resolved, error);
        if (error || size > options_.limits.max_module_source_bytes
            || size > options_.limits.max_total_source_bytes - total_bytes_
            || size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
            throw RunnerError("load", "RUN005_SOURCE_LIMIT_EXCEEDED",
                              "package source byte limit exceeded", id);
        }
        std::ifstream input(resolved, std::ios::binary);
        if (!input) {
            throw RunnerError("load", "RUN003_MODULE_NOT_FOUND",
                              "package module source is unreadable", id);
        }
        std::string source(static_cast<std::size_t>(size), '\0');
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (input.gcount() != static_cast<std::streamsize>(source.size()) || input.peek() != EOF) {
            throw RunnerError("load", "RUN007_SOURCE_CHANGED",
                              "module source changed while it was being read", id);
        }
        total_bytes_ += source.size();
        return source;
    }

    void charge_work(const std::string_view id, const std::size_t amount = 1)
    {
        if (amount > options_.max_loader_work - loader_work_) {
            throw RunnerError("load", "RUN008_LOADER_LIMIT_EXCEEDED",
                              "loader work limit exceeded", std::string(id));
        }
        loader_work_ += amount;
    }

    void visit(const std::string& id, const std::size_t depth)
    {
        charge_work(id);
        if (depth > options_.max_loader_depth) {
            throw RunnerError("load", "RUN008_LOADER_LIMIT_EXCEEDED",
                              "loader import depth limit exceeded", id);
        }
        if (records_.contains(id)) return;
        if (records_.size() >= options_.limits.max_modules) {
            throw RunnerError("load", "RUN005_SOURCE_LIMIT_EXCEEDED",
                              "package module count limit exceeded", id);
        }
        Record record;
        record.source = read_source(id);
        const auto parsed = script::parse(record.source);
        if (parsed.has_errors()) {
            const auto found = std::find_if(parsed.diagnostics.begin(), parsed.diagnostics.end(),
                [](const script::Diagnostic& diagnostic) {
                    return diagnostic.severity == script::DiagnosticSeverity::Error;
                });
            throw RunnerError("compile", found->code, found->message, id, found->span);
        }
        const auto semantic = script::analyze_semantics(parsed.program);
        charge_work(id, semantic.visited_ast_nodes);
        if (semantic.has_errors()) {
            const auto found = std::find_if(semantic.diagnostics.begin(), semantic.diagnostics.end(),
                [](const script::Diagnostic& diagnostic) {
                    return diagnostic.severity == script::DiagnosticSeverity::Error;
                });
            throw RunnerError("compile", found->code, found->message, id, found->span);
        }
        for (const auto* imported : semantic.imports) {
                const auto specifier = checked_specifier(imported->module, "import specifier");
                if (specifier.kind != runtime::ModuleKind::Package) {
                    throw RunnerError("load", "RUN006_HOST_IMPORT_UNAVAILABLE",
                                      "Host modules are unavailable in the conformance runner",
                                      imported->module, imported->span);
                }
                charge_work(id);
                record.imports.push_back(specifier.canonical_id);
        }
        const auto imports = record.imports;
        records_.emplace(id, std::move(record));
        for (const auto& imported : imports) visit(imported, depth + 1);
    }

    const Options& options_;
    fs::path root_;
    std::size_t total_bytes_{};
    std::size_t loader_work_{};
    std::map<std::string, Record, std::less<>> records_;
};

std::string success_json(const Options& options, const runtime::JsonValue& value,
                         const runtime::EvaluationStats& stats)
{
    std::string out{"{\"schema_version\":"};
    out += schema_version;
    out += ",\"engine\":"; append_json_string(out, engine);
    out += ",\"ok\":true,\"entry\":"; append_json_string(out, options.entry);
    out += ",\"export\":"; append_json_string(out, options.export_name);
    out += ",\"value\":";
    append_json_value(out, value, options.max_json_output_bytes, 0, options.json_limits.max_depth);
    out += ",\"stats\":{\"steps\":"; append_size(out, stats.steps);
    out += ",\"peak_call_depth\":"; append_size(out, stats.peak_call_depth);
    out += ",\"peak_value_stack\":"; append_size(out, stats.peak_value_stack);
    out += ",\"collection_work\":"; append_size(out, stats.collection_work);
    out += ",\"initialized_modules\":"; append_size(out, stats.initialized_modules);
    out += ",\"created_functions\":"; append_size(out, stats.created_functions);
    out += "}}\n";
    check_output_size(out, options.max_json_output_bytes);
    return out;
}

}  // namespace

int main(const int argc, char** argv)
{
    try {
        const Options options = parse_options(argc, argv);
        auto modules = PackageLoader(options).load();
        runtime::SynchronousEvaluator evaluator(
            std::move(modules), options.limits, options.heap_limits);
        static_cast<void>(evaluator.execute(options.entry));
        const auto value = evaluator.module_export(options.entry, options.export_name);
        const auto json_value = runtime::heap_value_to_json(evaluator.heap(), value, options.json_limits);
        std::cout << success_json(options, json_value, evaluator.stats());
        return 0;
    } catch (const RunnerError& error) {
        std::cout << error_json(error.phase, error.code, error.what(), error.module, error.span);
        return error.phase == "usage" ? 2 : 1;
    } catch (const runtime::EvaluationCompileError& error) {
        const auto& diagnostic = error.diagnostics().front();
        std::cout << error_json("compile", diagnostic.diagnostic.code,
                                diagnostic.diagnostic.message, diagnostic.module,
                                diagnostic.diagnostic.span);
        return 1;
    } catch (const runtime::EvaluationError& error) {
        std::cout << error_json("execute", error.code_name(), error.what(), error.module(),
                                error.span(), error.steps());
        return 1;
    } catch (const runtime::RuntimeError& error) {
        std::cout << error_json("result", runtime::runtime_error_code_name(error.code()),
                                error.what());
        return 1;
    } catch (const runtime::ModuleGraphError& error) {
        std::cout << error_json("load", runtime::module_graph_error_code_name(error.code()),
                                error.what(), error.module());
        return 1;
    } catch (const runtime::ModuleSpecifierError& error) {
        std::cout << error_json("load", runtime::module_specifier_error_code_name(error.code()),
                                "module specifier is not canonical");
        return 1;
    } catch (const std::exception&) {
        std::cout << error_json("internal", "RUN999_INTERNAL_ERROR",
                                "conformance runner failed without exposing host details");
        return 2;
    }
}
