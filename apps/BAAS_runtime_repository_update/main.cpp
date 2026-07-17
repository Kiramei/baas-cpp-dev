#include "service/app/RuntimeRepositoryUpdateApplication.h"

#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] int run(const std::span<const std::string_view> arguments) {
  return baas::service::app::run_runtime_repository_update_application(
      arguments, std::cin, std::cout, std::cerr);
}

} // namespace

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>

int wmain(const int argc, wchar_t *argv[]) {
  if (argc < 1 || argv == nullptr ||
      argc - 1 >
          static_cast<int>(baas::service::app::
                               runtime_repository_update_max_argument_count)) {
    std::cout << "{\"ok\":false,\"error\":\"command_line\"}\n";
    std::cerr << "BAAS_runtime_repository_update: command_line\n";
    return static_cast<int>(
        baas::service::app::RuntimeRepositoryUpdateProcessExit::command_line);
  }
  std::vector<std::string> utf8;
  std::vector<std::string_view> views;
  try {
    utf8.reserve(static_cast<std::size_t>(argc - 1));
    views.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
      if (argv[index] == nullptr)
        throw std::invalid_argument{"null argument"};
      const int required =
          WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                              nullptr, 0, nullptr, nullptr);
      if (required <= 0 ||
          required - 1 > static_cast<int>(
                             baas::service::app::
                                 runtime_repository_update_max_argument_bytes))
        throw std::invalid_argument{"invalid argument"};
      std::string value(static_cast<std::size_t>(required), '\0');
      if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                              value.data(), required, nullptr,
                              nullptr) != required)
        throw std::invalid_argument{"invalid argument"};
      value.pop_back();
      utf8.push_back(std::move(value));
    }
    for (const auto &value : utf8)
      views.emplace_back(value);
  } catch (...) {
    std::cout << "{\"ok\":false,\"error\":\"command_line\"}\n";
    std::cerr << "BAAS_runtime_repository_update: command_line\n";
    return static_cast<int>(
        baas::service::app::RuntimeRepositoryUpdateProcessExit::command_line);
  }
  return run(views);
}

#else

int main(const int argc, char *argv[]) {
  if (argc < 1 || argv == nullptr ||
      argc - 1 >
          static_cast<int>(baas::service::app::
                               runtime_repository_update_max_argument_count)) {
    std::cout << "{\"ok\":false,\"error\":\"command_line\"}\n";
    std::cerr << "BAAS_runtime_repository_update: command_line\n";
    return static_cast<int>(
        baas::service::app::RuntimeRepositoryUpdateProcessExit::command_line);
  }
  std::vector<std::string_view> arguments;
  try {
    arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
      if (argv[index] == nullptr)
        throw std::invalid_argument{"null argument"};
      arguments.emplace_back(argv[index]);
    }
  } catch (...) {
    std::cout << "{\"ok\":false,\"error\":\"command_line\"}\n";
    std::cerr << "BAAS_runtime_repository_update: command_line\n";
    return static_cast<int>(
        baas::service::app::RuntimeRepositoryUpdateProcessExit::command_line);
  }
  return run(arguments);
}

#endif
