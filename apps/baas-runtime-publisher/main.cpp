#include "runtime/publisher/GroupPublicationCompiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>

namespace publisher = baas::runtime::publisher;

namespace {

[[nodiscard]] std::string read_lock(const std::filesystem::path& path)
{
    constexpr std::uintmax_t max_lock_bytes = 4U * 1024U * 1024U;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > max_lock_bytes)
        throw std::runtime_error("publication lock size is invalid");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open publication lock");
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size())) ||
        input.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("cannot read publication lock exactly");
    return text;
}

[[nodiscard]] publisher::GroupPublicationLock production_lock(
    const std::filesystem::path& path)
{
    auto lock = publisher::parse_group_publication_lock(read_lock(path));
    publisher::validate_group_production_lock(lock);
    return lock;
}

[[nodiscard]] std::map<std::string, std::string, std::less<>> options(
    const int argc, char** argv, const int start)
{
    std::map<std::string, std::string, std::less<>> result;
    for (int index = start; index < argc; index += 2) {
        if (index + 1 >= argc || !std::string_view{argv[index]}.starts_with("--") ||
            !result.emplace(argv[index], argv[index + 1]).second)
            throw std::invalid_argument("invalid or duplicate command option");
    }
    return result;
}

[[nodiscard]] const std::string& required(
    const std::map<std::string, std::string, std::less<>>& values,
    const std::string_view key)
{
    const auto found = values.find(key);
    if (found == values.end() || found->second.empty())
        throw std::invalid_argument("missing required option " + std::string{key});
    return found->second;
}

void exact_options(
    const std::map<std::string, std::string, std::less<>>& values,
    const std::initializer_list<std::string_view> expected)
{
    if (values.size() != expected.size())
        throw std::invalid_argument("unknown or missing command option");
    for (const auto key : expected) static_cast<void>(required(values, key));
}

void usage()
{
    std::cerr
        << "usage:\n"
        << "  baas-runtime-publisher verify-source --repository DIR --lock FILE\n"
        << "  baas-runtime-publisher compile-group --repository DIR --lock FILE --output DIR [--check true]\n"
        << "  baas-runtime-publisher verify-publication --repository DIR --lock FILE --output DIR\n"
        << "  baas-runtime-publisher check-reproducible --repository DIR --lock FILE\n";
}

}  // namespace

int main(const int argc, char** argv)
{
    try {
        if (argc < 2) { usage(); return 2; }
        const std::string_view command{argv[1]};
        const auto values = options(argc, argv, 2);
        if (command == "verify-source") {
            exact_options(values, {"--repository", "--lock"});
            const auto lock = production_lock(required(values, "--lock"));
            publisher::verify_group_publication_sources(
                lock, required(values, "--repository"));
        } else if (command == "compile-group") {
            const bool check = values.contains("--check");
            exact_options(values, check
                ? std::initializer_list<std::string_view>{"--repository", "--lock", "--output", "--check"}
                : std::initializer_list<std::string_view>{"--repository", "--lock", "--output"});
            if (check && required(values, "--check") != "true")
                throw std::invalid_argument("--check accepts only true");
            const auto lock = production_lock(required(values, "--lock"));
            const auto output = publisher::compile_group_publication(
                lock, required(values, "--repository"));
            publisher::write_group_publication(
                output, required(values, "--output"), check);
        } else if (command == "verify-publication") {
            exact_options(values, {"--repository", "--lock", "--output"});
            const auto lock = production_lock(required(values, "--lock"));
            publisher::verify_group_publication(
                lock, required(values, "--repository"), required(values, "--output"));
        } else if (command == "check-reproducible") {
            exact_options(values, {"--repository", "--lock"});
            const auto lock = production_lock(required(values, "--lock"));
            const auto first = publisher::compile_group_publication(
                lock, required(values, "--repository"));
            const auto second = publisher::compile_group_publication(
                lock, required(values, "--repository"));
            if (first.size() != second.size())
                throw std::runtime_error("non-reproducible output count");
            for (std::size_t index = 0; index < first.size(); ++index)
                if (first[index].relative_path != second[index].relative_path ||
                    first[index].bytes != second[index].bytes)
                    throw std::runtime_error("non-reproducible publication bytes");
        } else {
            usage(); return 2;
        }
        std::cout << "ok\n";
        return 0;
    } catch (const publisher::PublicationError& error) {
        std::cerr << publisher::publication_error_name(error.code()) << ": "
                  << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "PUB999: " << error.what() << '\n';
        return 2;
    }
}
