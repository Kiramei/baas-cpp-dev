#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace baas::service::protocol::bpip {

inline constexpr std::array<std::byte, 4> magic{
    std::byte{0x42}, std::byte{0x50}, std::byte{0x49}, std::byte{0x50},
};
inline constexpr std::uint8_t version = 1;
inline constexpr std::size_t header_size = 10;
inline constexpr std::uint32_t max_payload_size = 64U * 1024U * 1024U;

enum class FrameKind : std::uint8_t {
    json = 1,
    bytes = 2,
    close = 3,
    error = 4,
};

[[nodiscard]] constexpr std::uint8_t kind_value(const FrameKind kind) noexcept
{
    return static_cast<std::uint8_t>(kind);
}

[[nodiscard]] constexpr bool is_known_kind(const std::uint8_t kind) noexcept
{
    return kind >= static_cast<std::uint8_t>(FrameKind::json)
        && kind <= static_cast<std::uint8_t>(FrameKind::error);
}

enum class ErrorCode {
    invalid_magic,
    unsupported_version,
    payload_too_large,
};

struct Error {
    ErrorCode code{ErrorCode::invalid_magic};
    std::uint8_t observed_version{};
    std::uint64_t declared_payload_size{};

    friend bool operator==(const Error&, const Error&) = default;
};

[[nodiscard]] std::string_view error_name(ErrorCode code) noexcept;

using Header = std::array<std::byte, header_size>;
using Bytes = std::vector<std::byte>;

struct HeaderResult {
    Header header{};
    std::optional<Error> error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error.has_value(); }
};

struct EncodeResult {
    Bytes bytes;
    std::optional<Error> error;

    [[nodiscard]] explicit operator bool() const noexcept { return !error.has_value(); }
};

struct Frame {
    // Kept as an integer because v1 transport framing accepts unknown kinds.
    std::uint8_t kind{};
    Bytes payload;

    friend bool operator==(const Frame&, const Frame&) = default;
};

struct DecodeResult {
    std::vector<Frame> frames;
    std::optional<Error> error;
};

// Builds a header without allocating or materializing the payload. This is also
// the preferred way to validate the exact 64 MiB boundary in callers/tests.
[[nodiscard]] HeaderResult encode_header(std::uint8_t kind, std::uint64_t payload_size) noexcept;
[[nodiscard]] inline HeaderResult encode_header(
    const FrameKind kind, const std::uint64_t payload_size
) noexcept
{
    return encode_header(kind_value(kind), payload_size);
}

// Treats payload as opaque bytes. JSON validation/encoding belongs above this
// transport layer.
[[nodiscard]] EncodeResult encode_frame(std::uint8_t kind, std::span<const std::byte> payload);
[[nodiscard]] inline EncodeResult encode_frame(
    const FrameKind kind, const std::span<const std::byte> payload
)
{
    return encode_frame(kind_value(kind), payload);
}

class Decoder {
public:
    // A protocol error poisons the decoder. The feed call that detects it may
    // still return complete frames preceding the invalid header. Later feeds
    // repeat the same error and consume nothing until reset() is called.
    [[nodiscard]] DecodeResult feed(std::span<const std::byte> input);

    void reset() noexcept;

    [[nodiscard]] bool has_error() const noexcept { return error_.has_value(); }
    [[nodiscard]] const std::optional<Error>& error() const noexcept { return error_; }
    [[nodiscard]] std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] std::size_t buffered_payload_bytes() const noexcept { return payload_.size(); }
    [[nodiscard]] std::optional<std::uint32_t> expected_payload_size() const noexcept
    {
        return expected_payload_size_;
    }

private:
    void clear_pending_frame() noexcept;
    void poison(Error error) noexcept;

    Header header_{};
    std::size_t header_bytes_{};
    std::uint8_t current_kind_{};
    std::optional<std::uint32_t> expected_payload_size_;
    Bytes payload_;
    std::optional<Error> error_;
};

}  // namespace baas::service::protocol::bpip
