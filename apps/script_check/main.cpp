#include "script/SyntaxCheck.h"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::size_t default_max_source_bytes = 8U * 1024U * 1024U;

struct Options {
    bool json{false};
    bool help{false};
    std::size_t max_source_bytes{default_max_source_bytes};
    baas::script::SyntaxCheckOptions check{};
    std::vector<std::string> inputs;
};

struct CheckedInput {
    std::string label;
    baas::script::SyntaxCheckResult result;
};

void usage(std::ostream& stream)
{
    stream << "usage: BAAS_script_check [--json] [--max-bytes N] "
              "[--max-ast-nodes N] [--max-depth N] FILE...\n"
              "       FILE '-' reads UTF-8 source from standard input once\n";
}

std::optional<std::size_t> parse_size(const std::string_view text)
{
    std::size_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

bool parse_options(const int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
            continue;
        }
        if (argument == "--json") {
            options.json = true;
            continue;
        }
        const auto parse_limit = [&](std::size_t& destination) -> bool {
            if (++index >= argc) return false;
            const auto parsed = parse_size(argv[index]);
            if (!parsed) return false;
            destination = *parsed;
            return true;
        };
        if (argument == "--max-bytes") {
            if (!parse_limit(options.max_source_bytes)) return false;
            continue;
        }
        if (argument == "--max-ast-nodes") {
            if (!parse_limit(options.check.semantic.max_ast_nodes)) return false;
            continue;
        }
        if (argument == "--max-depth") {
            if (!parse_limit(options.check.semantic.max_nesting_depth)) return false;
            continue;
        }
        if (!argument.empty() && argument.front() == '-' && argument != "-") return false;
        options.inputs.emplace_back(argument);
    }
    return options.help || !options.inputs.empty();
}

bool read_bounded(std::istream& stream, const std::size_t limit, std::string& output)
{
    char buffer[8192];
    while (stream) {
        stream.read(buffer, sizeof(buffer));
        const auto count = static_cast<std::size_t>(stream.gcount());
        if (count > limit - output.size()) return false;
        output.append(buffer, count);
    }
    return !stream.bad();
}

std::string path_label(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

bool read_input(const std::string& input, const std::size_t limit, bool& used_stdin,
                std::string& label, std::string& source, std::string& error)
{
    if (input == "-") {
        if (used_stdin) {
            error = "standard input may be specified only once";
            return false;
        }
        used_stdin = true;
        label = "<stdin>";
        if (!read_bounded(std::cin, limit, source)) {
            error = "standard input exceeds --max-bytes or could not be read";
            return false;
        }
        return true;
    }

    const auto* begin = reinterpret_cast<const char8_t*>(input.data());
    const std::filesystem::path path{std::u8string(begin, begin + input.size())};
    label = path_label(path);
    std::error_code file_error;
    const auto size = std::filesystem::file_size(path, file_error);
    if (file_error) {
        error = "cannot stat input '" + label + "': " + file_error.message();
        return false;
    }
    if (size > limit) {
        error = "input '" + label + "' exceeds --max-bytes";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open input '" + label + "'";
        return false;
    }
    if (!read_bounded(stream, limit, source)) {
        error = "input '" + label + "' changed size or could not be read";
        return false;
    }
    return true;
}

void json_string(std::ostream& stream, const std::string_view value)
{
    static constexpr char digits[] = "0123456789abcdef";
    stream << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (byte < 0x20) {
                    stream << "\\u00" << digits[byte >> 4] << digits[byte & 0x0f];
                } else {
                    stream << static_cast<char>(byte);
                }
        }
    }
    stream << '"';
}

void write_location(std::ostream& stream, const baas::script::SourceLocation location)
{
    stream << "{\"byte_offset\":" << location.byte_offset
           << ",\"line\":" << location.line << ",\"column\":" << location.column << '}';
}

void write_json(const std::vector<CheckedInput>& checked)
{
    std::cout << "{\"schema_version\":1,\"files\":[";
    for (std::size_t file_index = 0; file_index < checked.size(); ++file_index) {
        if (file_index != 0) std::cout << ',';
        const auto& file = checked[file_index];
        std::cout << "{\"path\":";
        json_string(std::cout, file.label);
        std::cout << ",\"valid\":" << (file.result.has_errors() ? "false" : "true")
                  << ",\"visited_ast_nodes\":" << file.result.visited_ast_nodes
                  << ",\"diagnostics\":[";
        for (std::size_t index = 0; index < file.result.diagnostics.size(); ++index) {
            if (index != 0) std::cout << ',';
            const auto& diagnostic = file.result.diagnostics[index];
            std::cout << "{\"severity\":";
            json_string(std::cout, diagnostic.severity == baas::script::DiagnosticSeverity::Error
                                       ? "error" : "warning");
            std::cout << ",\"code\":";
            json_string(std::cout, diagnostic.code);
            std::cout << ",\"message\":";
            json_string(std::cout, diagnostic.message);
            std::cout << ",\"span\":{\"begin\":";
            write_location(std::cout, diagnostic.span.begin);
            std::cout << ",\"end\":";
            write_location(std::cout, diagnostic.span.end);
            std::cout << "}}";
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

}  // namespace

int main(const int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        usage(std::cerr);
        return 2;
    }
    if (options.help) {
        usage(std::cout);
        return 0;
    }

    std::vector<CheckedInput> checked;
    bool used_stdin = false;
    try {
        checked.reserve(options.inputs.size());
        for (const auto& input : options.inputs) {
            std::string label;
            std::string source;
            std::string error;
            if (!read_input(input, options.max_source_bytes, used_stdin, label, source, error)) {
                std::cerr << "BAAS_script_check: " << error << '\n';
                return 2;
            }
            checked.push_back({std::move(label), baas::script::check_source(source, options.check)});
        }
    } catch (const std::bad_alloc&) {
        std::cerr << "BAAS_script_check: memory allocation failed\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "BAAS_script_check: " << error.what() << '\n';
        return 2;
    }

    bool failed = false;
    for (const auto& file : checked) failed = failed || file.result.has_errors();
    if (options.json) {
        write_json(checked);
    } else {
        for (const auto& file : checked) {
            if (file.result.diagnostics.empty()) {
                std::cout << file.label << ": ok\n";
                continue;
            }
            for (const auto& diagnostic : file.result.diagnostics) {
                std::cout << file.label << ':' << diagnostic.span.begin.line << ':'
                          << diagnostic.span.begin.column << ": "
                          << (diagnostic.severity == baas::script::DiagnosticSeverity::Error
                                  ? "error " : "warning ")
                          << diagnostic.code << ": " << diagnostic.message << '\n';
            }
        }
    }
    return failed ? 1 : 0;
}
