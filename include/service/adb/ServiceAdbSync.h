#pragma once

#include "service/adb/ServiceAdbTransport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string_view>

namespace baas::service::adb {

struct AdbSyncLimits {
    std::size_t max_path_bytes{1'024};
    std::uint64_t max_file_bytes{128U * 1'024U * 1'024U};
    std::size_t data_chunk_bytes{64U * 1'024U};
    std::size_t max_fail_message_bytes{64U * 1'024U};
    std::chrono::milliseconds operation_timeout{30'000};
};

struct AdbSyncStat {
    std::uint32_t mode{};
    std::uint32_t size{};
    std::uint32_t modified_time{};

    [[nodiscard]] bool exists() const noexcept { return mode != 0; }
};

class ServiceAdbSync final {
public:
    explicit ServiceAdbSync(
        ServiceAdbTransport& transport,
        AdbSyncLimits limits = {});
    ServiceAdbSync(const ServiceAdbSync&) = delete;
    ServiceAdbSync& operator=(const ServiceAdbSync&) = delete;
    ServiceAdbSync(ServiceAdbSync&&) = delete;
    ServiceAdbSync& operator=(ServiceAdbSync&&) = delete;

    [[nodiscard]] AdbTransportResult<AdbSyncStat> stat(
        std::string_view exact_serial,
        std::string_view remote_path,
        std::stop_token stop = {});

    [[nodiscard]] AdbTransportResult<std::uint64_t> push(
        std::string_view exact_serial,
        std::string_view remote_path,
        std::span<const std::byte> contents,
        std::uint32_t permissions = 0755,
        std::uint32_t modified_time = 0,
        std::stop_token stop = {});

    [[nodiscard]] AdbTransportResult<std::uint64_t> push_file(
        std::string_view exact_serial,
        std::string_view remote_path,
        const std::filesystem::path& local_path,
        std::uint32_t permissions = 0755,
        std::uint32_t modified_time = 0,
        std::stop_token stop = {});

    [[nodiscard]] const AdbSyncLimits& limits() const noexcept;

private:
    ServiceAdbTransport* transport_;
    AdbSyncLimits limits_;
};

}  // namespace baas::service::adb
