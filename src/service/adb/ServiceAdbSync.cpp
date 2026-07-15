#include "service/adb/ServiceAdbSync.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace baas::service::adb {
namespace {

using Deadline = AdbServiceStream::Deadline;

template <typename T>
AdbTransportResult<T> fail(
    const AdbTransportError error, std::string message = {})
{
    return {std::nullopt, error, std::move(message)};
}

bool valid_utf8(const std::string_view value) noexcept
{
    std::size_t offset{};
    while (offset < value.size()) {
        const auto lead = static_cast<unsigned char>(value[offset++]);
        if (lead < 0x80U) continue;
        std::size_t continuation{};
        std::uint32_t codepoint{};
        if (lead >= 0xC2U && lead <= 0xDFU) {
            continuation = 1;
            codepoint = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            continuation = 2;
            codepoint = lead & 0x0FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            continuation = 3;
            codepoint = lead & 0x07U;
        } else {
            return false;
        }
        if (continuation > value.size() - offset) return false;
        for (std::size_t index{}; index < continuation; ++index) {
            const auto byte = static_cast<unsigned char>(value[offset++]);
            if ((byte & 0xC0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (byte & 0x3FU);
        }
        if ((continuation == 2 && codepoint < 0x800U)
            || (continuation == 3 && codepoint < 0x10000U)
            || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)
            || codepoint > 0x10FFFFU) {
            return false;
        }
    }
    return true;
}

bool safe_remote_path(
    const std::string_view path, const std::size_t maximum) noexcept
{
    if (path.empty() || path.size() > maximum || path.front() != '/'
        || path.back() == '/' || !valid_utf8(path)) {
        return false;
    }
    for (const unsigned char byte : path) {
        if (byte < 0x20U || byte == 0x7FU || byte == '\\' || byte == ',') {
            return false;
        }
    }
    std::size_t start = 1;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(
            start, (end == std::string_view::npos ? path.size() : end) - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

std::array<std::byte, 4> le32(const std::uint32_t value) noexcept
{
    return {std::byte(value & 0xFFU), std::byte((value >> 8U) & 0xFFU),
            std::byte((value >> 16U) & 0xFFU),
            std::byte((value >> 24U) & 0xFFU)};
}

std::uint32_t decode_le32(const std::span<const std::byte, 4> bytes) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[0])
        | (std::to_integer<std::uint32_t>(bytes[1]) << 8U)
        | (std::to_integer<std::uint32_t>(bytes[2]) << 16U)
        | (std::to_integer<std::uint32_t>(bytes[3]) << 24U);
}

std::array<std::byte, 8> header(
    const std::string_view id, const std::uint32_t value)
{
    std::array<std::byte, 8> bytes{};
    std::transform(id.begin(), id.end(), bytes.begin(),
        [](const char ch) { return std::byte(static_cast<unsigned char>(ch)); });
    const auto encoded = le32(value);
    std::copy(encoded.begin(), encoded.end(), bytes.begin() + 4);
    return bytes;
}

AdbTransportResult<std::vector<std::byte>> read_exact(
    AdbServiceStream& stream, const std::size_t count, const Deadline deadline,
    const std::stop_token stop)
{
    std::vector<std::byte> result;
    result.reserve(count);
    while (result.size() < count) {
        auto chunk = stream.read_some_until(count - result.size(), deadline, stop);
        if (!chunk) {
            return fail<std::vector<std::byte>>(
                chunk.error, std::move(chunk.message));
        }
        if (chunk->empty()) {
            return fail<std::vector<std::byte>>(
                AdbTransportError::protocol_error, "truncated ADB SYNC frame");
        }
        result.insert(result.end(), chunk->begin(), chunk->end());
    }
    return {std::move(result), AdbTransportError::none, {}};
}

AdbTransportResult<std::string> read_fail(
    AdbServiceStream& stream, const Deadline deadline,
    const std::stop_token stop, const AdbSyncLimits& limits)
{
    auto encoded_size = read_exact(stream, 4, deadline, stop);
    if (!encoded_size) {
        return fail<std::string>(encoded_size.error, std::move(encoded_size.message));
    }
    const auto size = decode_le32(std::span<const std::byte, 4>(
        encoded_size->data(), encoded_size->size()));
    if (size > limits.max_fail_message_bytes) {
        return fail<std::string>(
            AdbTransportError::capacity, "ADB SYNC FAIL exceeds limit");
    }
    auto bytes = read_exact(stream, size, deadline, stop);
    if (!bytes) return fail<std::string>(bytes.error, std::move(bytes.message));
    if (bytes->empty()) return {std::string{}, AdbTransportError::none, {}};
    return {std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
            AdbTransportError::none, {}};
}

AdbTransportResult<bool> expect_status(
    AdbServiceStream& stream, const Deadline deadline,
    const std::stop_token stop, const AdbSyncLimits& limits)
{
    auto id = read_exact(stream, 4, deadline, stop);
    if (!id) return fail<bool>(id.error, std::move(id.message));
    const std::string_view word(
        reinterpret_cast<const char*>(id->data()), id->size());
    if (word != "OKAY" && word != "FAIL") {
        return fail<bool>(
            AdbTransportError::protocol_error, "unexpected ADB SYNC status");
    }
    if (word == "FAIL") {
        auto message = read_fail(stream, deadline, stop, limits);
        if (!message) return fail<bool>(message.error, std::move(message.message));
        return fail<bool>(AdbTransportError::adb_fail, std::move(*message.value));
    }
    auto length = read_exact(stream, 4, deadline, stop);
    if (!length) return fail<bool>(length.error, std::move(length.message));
    if (decode_le32(std::span<const std::byte, 4>(
            length->data(), length->size())) != 0) {
        return fail<bool>(AdbTransportError::protocol_error,
                          "ADB SYNC OKAY payload must be empty");
    }
    return {true, AdbTransportError::none, {}};
}

AdbTransportError write(
    AdbServiceStream& stream, const std::span<const std::byte> bytes,
    const Deadline deadline, const std::stop_token stop, std::string& message)
{
    auto result = stream.write_all_until(bytes, deadline, stop);
    if (result) return AdbTransportError::none;
    message = std::move(result.message);
    return result.error;
}

std::uint32_t effective_mtime(const std::uint32_t requested) noexcept
{
    if (requested != 0) return requested;
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    if (now <= 0) return 1;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(now),
        std::numeric_limits<std::uint32_t>::max()));
}

template <typename Reader>
AdbTransportResult<std::uint64_t> push_impl(
    ServiceAdbTransport& transport, const AdbSyncLimits& limits,
    const std::string_view serial, const std::string_view remote_path,
    const std::uint64_t expected_size, const std::uint32_t permissions,
    const std::uint32_t modified_time, Reader&& reader,
    const std::stop_token stop)
{
    if (!safe_remote_path(remote_path, limits.max_path_bytes)
        || permissions > 0777U) {
        return fail<std::uint64_t>(AdbTransportError::invalid_argument);
    }
    if (expected_size > limits.max_file_bytes) {
        return fail<std::uint64_t>(
            AdbTransportError::capacity, "local source exceeds ADB SYNC limit");
    }
    const auto mode_path = std::string(remote_path) + ","
        + std::to_string(0100000U | permissions);
    if (mode_path.size() > limits.max_path_bytes) {
        return fail<std::uint64_t>(
            AdbTransportError::capacity, "ADB SYNC SEND path exceeds limit");
    }
    auto opened = transport.open_sync(serial, stop);
    if (!opened) {
        return fail<std::uint64_t>(opened.error, std::move(opened.message));
    }
    auto stream = std::move(*opened.value);
    const auto deadline = std::chrono::steady_clock::now()
        + limits.operation_timeout;
    std::string message;
    auto request = header("SEND", static_cast<std::uint32_t>(mode_path.size()));
    auto error = write(stream, request, deadline, stop, message);
    if (error != AdbTransportError::none) {
        return fail<std::uint64_t>(error, std::move(message));
    }
    error = write(stream, std::as_bytes(std::span(mode_path)), deadline, stop, message);
    if (error != AdbTransportError::none) {
        return fail<std::uint64_t>(error, std::move(message));
    }

    std::vector<std::byte> chunk(limits.data_chunk_bytes);
    std::uint64_t total{};
    while (total < expected_size) {
        const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(
            chunk.size(), expected_size - total));
        const auto got = reader(std::span<std::byte>(chunk.data(), wanted));
        if (got != wanted) {
            return fail<std::uint64_t>(
                AdbTransportError::local_io_error, "local source changed or read failed");
        }
        const auto data_header = header("DATA", static_cast<std::uint32_t>(got));
        error = write(stream, data_header, deadline, stop, message);
        if (error == AdbTransportError::none) {
            error = write(stream,
                std::span<const std::byte>(chunk.data(), got),
                deadline, stop, message);
        }
        if (error != AdbTransportError::none) {
            return fail<std::uint64_t>(error, std::move(message));
        }
        total += got;
    }
    const auto done = header("DONE", effective_mtime(modified_time));
    error = write(stream, done, deadline, stop, message);
    if (error != AdbTransportError::none) {
        return fail<std::uint64_t>(error, std::move(message));
    }
    auto status = expect_status(stream, deadline, stop, limits);
    if (!status) {
        return fail<std::uint64_t>(status.error, std::move(status.message));
    }
    return {total, AdbTransportError::none, {}};
}

}  // namespace

ServiceAdbSync::ServiceAdbSync(
    ServiceAdbTransport& transport, AdbSyncLimits limits)
    : transport_(&transport), limits_(limits)
{
    if (limits_.max_path_bytes == 0 || limits_.max_file_bytes == 0
        || limits_.data_chunk_bytes == 0 || limits_.data_chunk_bytes > 65'536
        || limits_.max_path_bytes > 1'024
        || limits_.max_fail_message_bytes == 0
        || limits_.operation_timeout <= std::chrono::milliseconds::zero()
        || limits_.operation_timeout > std::chrono::hours(1)) {
        throw std::invalid_argument("invalid ADB SYNC limits");
    }
}

AdbTransportResult<AdbSyncStat> ServiceAdbSync::stat(
    const std::string_view exact_serial, const std::string_view remote_path,
    const std::stop_token stop) try
{
    if (!safe_remote_path(remote_path, limits_.max_path_bytes)) {
        return fail<AdbSyncStat>(AdbTransportError::invalid_argument);
    }
    auto opened = transport_->open_sync(exact_serial, stop);
    if (!opened) {
        return fail<AdbSyncStat>(opened.error, std::move(opened.message));
    }
    auto stream = std::move(*opened.value);
    const auto deadline = std::chrono::steady_clock::now()
        + limits_.operation_timeout;
    const auto request = header("STAT", static_cast<std::uint32_t>(remote_path.size()));
    std::string message;
    auto error = write(stream, request, deadline, stop, message);
    if (error == AdbTransportError::none) {
        error = write(stream, std::as_bytes(std::span(remote_path)),
                      deadline, stop, message);
    }
    if (error != AdbTransportError::none) {
        return fail<AdbSyncStat>(error, std::move(message));
    }
    auto id = read_exact(stream, 4, deadline, stop);
    if (!id) return fail<AdbSyncStat>(id.error, std::move(id.message));
    const std::string_view word(
        reinterpret_cast<const char*>(id->data()), id->size());
    if (word == "FAIL") {
        auto fail_message = read_fail(stream, deadline, stop, limits_);
        if (!fail_message) {
            return fail<AdbSyncStat>(
                fail_message.error, std::move(fail_message.message));
        }
        return fail<AdbSyncStat>(
            AdbTransportError::adb_fail, std::move(*fail_message.value));
    }
    if (word != "STAT") {
        return fail<AdbSyncStat>(
            AdbTransportError::protocol_error, "unexpected ADB SYNC stat response");
    }
    auto payload = read_exact(stream, 12, deadline, stop);
    if (!payload) {
        return fail<AdbSyncStat>(payload.error, std::move(payload.message));
    }
    const auto decode = [&](const std::size_t offset) {
        return decode_le32(std::span<const std::byte, 4>(
            payload->data() + offset, 4));
    };
    return {AdbSyncStat{decode(0), decode(4), decode(8)},
            AdbTransportError::none, {}};
} catch (...) {
    return fail<AdbSyncStat>(AdbTransportError::internal_error);
}

AdbTransportResult<std::uint64_t> ServiceAdbSync::push(
    const std::string_view exact_serial, const std::string_view remote_path,
    const std::span<const std::byte> contents, const std::uint32_t permissions,
    const std::uint32_t modified_time, const std::stop_token stop) try
{
    std::size_t offset{};
    return push_impl(*transport_, limits_, exact_serial, remote_path,
        contents.size(), permissions, modified_time,
        [&](const std::span<std::byte> output) {
            std::copy_n(contents.data() + offset, output.size(), output.data());
            offset += output.size();
            return output.size();
        }, stop);
} catch (...) {
    return fail<std::uint64_t>(AdbTransportError::internal_error);
}

AdbTransportResult<std::uint64_t> ServiceAdbSync::push_file(
    const std::string_view exact_serial, const std::string_view remote_path,
    const std::filesystem::path& local_path, const std::uint32_t permissions,
    const std::uint32_t modified_time, const std::stop_token stop) try
{
    std::error_code filesystem_error;
    const auto size = std::filesystem::file_size(local_path, filesystem_error);
    if (filesystem_error || size > limits_.max_file_bytes) {
        return fail<std::uint64_t>(
            filesystem_error ? AdbTransportError::local_io_error
                             : AdbTransportError::capacity,
            filesystem_error ? "local source is not a readable regular file"
                             : "local source exceeds ADB SYNC limit");
    }
    std::ifstream input(local_path, std::ios::binary);
    if (!input) {
        return fail<std::uint64_t>(
            AdbTransportError::local_io_error, "failed to open local source");
    }
    auto result = push_impl(*transport_, limits_, exact_serial, remote_path,
        size, permissions, modified_time,
        [&](const std::span<std::byte> output) {
            input.read(reinterpret_cast<char*>(output.data()),
                       static_cast<std::streamsize>(output.size()));
            return static_cast<std::size_t>(input.gcount());
        }, stop);
    return result;
} catch (...) {
    return fail<std::uint64_t>(AdbTransportError::internal_error);
}

const AdbSyncLimits& ServiceAdbSync::limits() const noexcept { return limits_; }

}  // namespace baas::service::adb
