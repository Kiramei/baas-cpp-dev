#include <array>
#include <chrono>
#include <cstdlib>
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
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

extern char** environ;
#endif

namespace {

constexpr std::string_view arguments_file =
    "service-process-test-arguments.bin";
constexpr std::string_view exit_file = "service-process-test-exit-code.txt";
constexpr std::string_view ready_file = "service-process-test-ready.txt";
constexpr std::string_view spawn_grandchild_file =
    "service-process-test-spawn-grandchild.txt";
constexpr std::string_view grandchild_pid_file =
    "service-process-test-grandchild-pid.txt";
constexpr std::string_view environment_file =
    "service-process-test-environment.txt";
constexpr std::string_view sentinel_file =
    "service-process-test-inheritance-sentinel.txt";
constexpr std::string_view sentinel_result_file =
    "service-process-test-inheritance-result.txt";

void replace_file(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination)
{
#if defined(_WIN32)
    if (MoveFileExW(
            temporary.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        == FALSE) {
        throw std::runtime_error{"atomic fixture publication failed"};
    }
#else
    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        throw std::runtime_error{"atomic fixture publication failed"};
    }
#endif
}

void write_text_atomic(
    const std::filesystem::path& destination,
    const std::string& value)
{
    auto temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << value;
        output.close();
        if (!output) throw std::runtime_error{"fixture write failed"};
    }
    replace_file(temporary, destination);
}

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
    const auto destination = project_root / arguments_file;
    auto temporary = destination;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
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
    replace_file(temporary, destination);
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

[[nodiscard]] bool environment_is_empty() noexcept
{
#if defined(_WIN32)
    wchar_t* environment = GetEnvironmentStringsW();
    if (environment == nullptr) return false;
    const bool empty = environment[0] == L'\0';
    FreeEnvironmentStringsW(environment);
    return empty;
#else
    return environ == nullptr || environ[0] == nullptr;
#endif
}

void check_inheritance_sentinel(const std::filesystem::path& project_root)
{
    std::ifstream input(project_root / sentinel_file);
    std::uint64_t raw = 0;
    input >> raw;
    if (!input) return;
#if defined(_WIN32)
    DWORD flags = 0;
    const bool inherited = GetHandleInformation(
        reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(raw)), &flags)
        != FALSE;
#else
    errno = 0;
    const bool inherited = ::fcntl(static_cast<int>(raw), F_GETFD) != -1
        || errno != EBADF;
#endif
    write_text_atomic(
        project_root / sentinel_result_file,
        inherited ? "inherited" : "closed");
}

#if defined(_WIN32)
[[nodiscard]] std::uint64_t spawn_grandchild(
    const std::filesystem::path& executable)
{
    std::wstring command = L"\"" + executable.native() + L"\" --grandchild";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::array<wchar_t, 2> empty_environment{L'\0', L'\0'};
    if (CreateProcessW(
            executable.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
            CREATE_UNICODE_ENVIRONMENT, empty_environment.data(), nullptr,
            &startup, &process)
        == FALSE) {
        throw std::runtime_error{"grandchild launch failed"};
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<std::uint64_t>(process.dwProcessId);
}
#else
[[nodiscard]] std::uint64_t spawn_grandchild(
    const std::filesystem::path& /* executable */)
{
    const pid_t child = ::fork();
    if (child < 0) throw std::runtime_error{"grandchild fork failed"};
    if (child == 0) block_forever();
    return static_cast<std::uint64_t>(child);
}
#endif

void publish_ready(
    const std::filesystem::path& project_root,
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments)
{
    write_arguments(project_root, arguments);
    write_text_atomic(
        project_root / environment_file,
        environment_is_empty() ? "empty" : "inherited");
    check_inheritance_sentinel(project_root);
    if (std::filesystem::is_regular_file(project_root / spawn_grandchild_file)) {
        write_text_atomic(
            project_root / grandchild_pid_file,
            std::to_string(spawn_grandchild(executable)));
    }
    write_text_atomic(project_root / ready_file, "ready");
}

}  // namespace

#if defined(_WIN32)

int wmain(const int argc, wchar_t* argv[])
{
    try {
        if (argc < 1 || argv == nullptr) return 126;
        if (argc == 2 && argv[1] != nullptr
            && std::wstring_view{argv[1]} == L"--grandchild") {
            block_forever();
        }
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        arguments.push_back(wide_to_utf8(argv[0]));
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
        publish_ready(
            project_root, std::filesystem::path{argv[0]}, arguments);
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
        if (argc == 2 && argv[1] != nullptr
            && std::string_view{argv[1]} == "--grandchild") {
            block_forever();
        }
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc));
        arguments.emplace_back(argv[0]);
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
        publish_ready(
            project_root, std::filesystem::path{argv[0]}, arguments);
        const int requested_exit = exit_code(project_root);
        if (requested_exit >= 0) return requested_exit;
        block_forever();
    } catch (...) {
        return 126;
    }
}

#endif
