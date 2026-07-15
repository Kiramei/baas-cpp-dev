#include "service/app/ServiceApplication.h"

#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>

#include <string>

int wmain(const int argc, wchar_t* argv[])
{
    if (argc < 1 || argv == nullptr) {
        std::cerr << "BAAS_service: command_line:invalid_argument_vector\n";
        return static_cast<int>(
            baas::service::app::ServiceProcessExit::command_line);
    }
    if (argc - 1 > static_cast<int>(
            baas::service::app::service_command_line_max_argument_count)) {
        std::cerr << "BAAS_service: command_line:too_many_arguments\n";
        return static_cast<int>(
            baas::service::app::ServiceProcessExit::command_line);
    }
    std::vector<std::string> utf8;
    std::vector<std::string_view> views;
    try {
        if (argc > 1) {
            utf8.reserve(static_cast<std::size_t>(argc - 1));
            views.reserve(static_cast<std::size_t>(argc - 1));
        }
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) {
                std::cerr << "BAAS_service: command_line:invalid_argument_vector\n";
                return static_cast<int>(
                    baas::service::app::ServiceProcessExit::command_line);
            }
            const int required = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                nullptr, 0, nullptr, nullptr);
            if (required <= 0) {
                std::cerr << "BAAS_service: command_line:invalid_argument_vector\n";
                return static_cast<int>(
                    baas::service::app::ServiceProcessExit::command_line);
            }
            if (required - 1 > static_cast<int>(
                    baas::service::app::service_command_line_max_argument_bytes)) {
                std::cerr << "BAAS_service: command_line:argument_too_long\n";
                return static_cast<int>(
                    baas::service::app::ServiceProcessExit::command_line);
            }
            std::string value(static_cast<std::size_t>(required), '\0');
            if (WideCharToMultiByte(
                    CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                    value.data(), required, nullptr, nullptr) != required) {
                std::cerr << "BAAS_service: command_line:invalid_argument_vector\n";
                return static_cast<int>(
                    baas::service::app::ServiceProcessExit::command_line);
            }
            value.pop_back();
            utf8.push_back(std::move(value));
        }
        for (const auto& value : utf8) views.emplace_back(value);
    } catch (...) {
        std::cerr << "BAAS_service: command_line:resource_exhausted\n";
        return static_cast<int>(
            baas::service::app::ServiceProcessExit::command_line);
    }
    return baas::service::app::run_service_application(
        views, std::cout, std::cerr);
}

#else

int main(const int argc, char* argv[])
{
    if (argc < 1 || argv == nullptr) {
        std::cerr << "BAAS_service: command_line:invalid_argument_vector\n";
        return static_cast<int>(
            baas::service::app::ServiceProcessExit::command_line);
    }
    if (argc - 1 > static_cast<int>(
            baas::service::app::service_command_line_max_argument_count)) {
        std::cerr << "BAAS_service: command_line:too_many_arguments\n";
        return static_cast<int>(
            baas::service::app::ServiceProcessExit::command_line);
    }
    std::vector<std::string_view> arguments;
    try {
        if (argc > 1) arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) {
                std::cerr << "BAAS_service: command_line:invalid_argument_vector\n";
                return static_cast<int>(
                    baas::service::app::ServiceProcessExit::command_line);
            }
            arguments.emplace_back(argv[index]);
        }
    } catch (...) {
        std::cerr << "BAAS_service: command_line:resource_exhausted\n";
        return static_cast<int>(
            baas::service::app::ServiceProcessExit::command_line);
    }
    return baas::service::app::run_service_application(
        arguments, std::cout, std::cerr);
}

#endif
