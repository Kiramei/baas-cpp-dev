#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

namespace {

constexpr std::string_view arguments_file =
    "service-process-test-arguments.bin";
constexpr std::string_view exit_file = "service-process-test-exit-code.txt";

#if defined(_WIN32)
[[nodiscard]] std::string wide_to_utf8(const std::wstring_view value)
{
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error{"argument exceeds UTF-16 conversion limit"};
    }
    const auto size = static_cast<int>(value.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), size, nullptr, 0,
        nullptr, nullptr);
    if (required <= 0) throw std::runtime_error{"argument is not valid UTF-16"};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), size,
            result.data(), required, nullptr, nullptr) != required) {
        throw std::runtime_error{"UTF-16 conversion failed"};
    }
    return result;
}
#endif

void write_arguments(
    const std::filesystem::path& project_root,
    const std::vector<std::string>& arguments)
{
    std::ofstream output(
        project_root / arguments_file,
        std::ios::binary | std::ios::trunc);
    const auto count = static_cast<std::uint32_t>(arguments.size());
    output.write(
        reinterpret_cast<const char*>(&count),
        static_cast<std::streamsize>(sizeof(count)));
    for (const auto& argument : arguments) {
        const auto size = static_cast<std::uint32_t>(argument.size());
        output.write(
            reinterpret_cast<const char*>(&size),
            static_cast<std::streamsize>(sizeof(size)));
        output.write(argument.data(), static_cast<std::streamsize>(argument.size()));
    }
    output.close();
    if (!output) throw std::runtime_error{"argument fixture write failed"};
}

[[nodiscard]] int exit_code(const std::filesystem::path& project_root)
{
    std::ifstream input(project_root / exit_file);
    if (!input) return -1;
    int value = -1;
    input >> value;
    if (!input || value < 0 || value > 125) return 125;
    return value;
}

[[noreturn]] void block_forever()
{
    for (;;) std::this_thread::sleep_for(std::chrono::hours{1});
}

}  // namespace

#if defined(_WIN32)

int wmain(const int argc, wchar_t* argv[])
{
    try {
        if (argc < 1 || argv == nullptr) return 126;
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc - 1));
        std::filesystem::path project_root;
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) return 126;
            arguments.push_back(wide_to_utf8(argv[index]));
            if (arguments.back() == "--project-root" && index + 1 < argc
                && argv[index + 1] != nullptr) {
                project_root = std::filesystem::path{argv[index + 1]};
            }
        }
        if (project_root.empty()) return 126;
        write_arguments(project_root, arguments);
        const int requested_exit = exit_code(project_root);
        if (requested_exit >= 0) return requested_exit;
        block_forever();
    } catch (...) {
        return 126;
    }
}

#else

int main(const int argc, char* argv[])
{
    try {
        if (argc < 1 || argv == nullptr) return 126;
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc - 1));
        std::filesystem::path project_root;
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) return 126;
            arguments.emplace_back(argv[index]);
            if (arguments.back() == "--project-root" && index + 1 < argc
                && argv[index + 1] != nullptr) {
                project_root = std::filesystem::path{argv[index + 1]};
            }
        }
        if (project_root.empty()) return 126;
        write_arguments(project_root, arguments);
        const int requested_exit = exit_code(project_root);
        if (requested_exit >= 0) return requested_exit;
        block_forever();
    } catch (...) {
        return 126;
    }
}

#endif
