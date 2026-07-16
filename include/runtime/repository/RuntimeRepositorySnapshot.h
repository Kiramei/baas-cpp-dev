#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace baas::runtime::repository {

inline constexpr std::string_view current_schema =
    "baas.runtime-repositories.current/v1";
inline constexpr std::string_view snapshot_schema =
    "baas.runtime-repositories.snapshot/v1";

enum class RuntimeRepositoryErrorCode {
    Io,
    FileLimitExceeded,
    InvalidJson,
    InvalidSchema,
    InvalidFieldSet,
    InvalidGeneration,
    InvalidRepository,
    IdentityMismatch,
    PathViolation,
};

class RuntimeRepositoryError final : public std::runtime_error {
public:
    RuntimeRepositoryError(RuntimeRepositoryErrorCode code, std::string message);
    [[nodiscard]] RuntimeRepositoryErrorCode code() const noexcept;

private:
    RuntimeRepositoryErrorCode code_;
};

struct RuntimeRepositoryLimits {
    std::size_t max_current_bytes{4U * 1024U};
    std::size_t max_snapshot_bytes{64U * 1024U};
    std::size_t max_json_string_bytes{4U * 1024U};
    std::size_t max_json_nodes{64};
    std::size_t max_json_depth{8};
};

struct RuntimeRepository final {
    std::string id;
    std::string commit;
    std::string root;
    std::string manifest;
    std::string manifest_sha256;
};

class RuntimeRepositorySnapshot final {
public:
    ~RuntimeRepositorySnapshot();
    RuntimeRepositorySnapshot(const RuntimeRepositorySnapshot&) = delete;
    RuntimeRepositorySnapshot& operator=(const RuntimeRepositorySnapshot&) = delete;

    // state_root is the runtime-repositories directory. Activation reads only
    // current.json and its named immutable snapshot; repository objects and
    // manifests are deliberately not opened here.
    [[nodiscard]] static std::shared_ptr<const RuntimeRepositorySnapshot> activate(
        const std::filesystem::path& state_root,
        RuntimeRepositoryLimits limits = {});

    [[nodiscard]] const std::string& generation() const noexcept;
    [[nodiscard]] const std::array<RuntimeRepository, 2>& repositories() const noexcept;
    [[nodiscard]] const RuntimeRepository& resources() const noexcept;
    [[nodiscard]] const RuntimeRepository& scripts() const noexcept;

private:
    struct Impl;
    explicit RuntimeRepositorySnapshot(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string runtime_repository_generation(
    const std::array<RuntimeRepository, 2>& repositories);

}  // namespace baas::runtime::repository
