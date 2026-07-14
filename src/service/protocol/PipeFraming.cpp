#include "service/protocol/PipeFraming.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace baas::service::protocol::bpip {
namespace {

[[nodiscard]] constexpr std::uint8_t as_u8(const std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t decode_u32_le(const std::span<const std::byte, 4> bytes) noexcept
{
    return static_cast<std::uint32_t>(as_u8(bytes[0]))
        | (static_cast<std::uint32_t>(as_u8(bytes[1])) << 8U)
        | (static_cast<std::uint32_t>(as_u8(bytes[2])) << 16U)
        | (static_cast<std::uint32_t>(as_u8(bytes[3])) << 24U);
}

}  // namespace

std::string_view error_name(const ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::invalid_magic:
        return "invalid_magic";
    case ErrorCode::unsupported_version:
        return "unsupported_version";
    case ErrorCode::payload_too_large:
        return "payload_too_large";
    }
    return "unknown_error";
}

HeaderResult encode_header(const std::uint8_t kind, const std::uint64_t payload_size) noexcept
{
    HeaderResult result;
    if (payload_size > max_payload_size) {
        result.error = Error{ErrorCode::payload_too_large, 0, payload_size};
        return result;
    }

    std::copy(magic.begin(), magic.end(), result.header.begin());
    result.header[4] = std::byte{version};
    result.header[5] = std::byte{kind};
    const auto length = static_cast<std::uint32_t>(payload_size);
    result.header[6] = std::byte{static_cast<std::uint8_t>(length & 0xFFU)};
    result.header[7] = std::byte{static_cast<std::uint8_t>((length >> 8U) & 0xFFU)};
    result.header[8] = std::byte{static_cast<std::uint8_t>((length >> 16U) & 0xFFU)};
    result.header[9] = std::byte{static_cast<std::uint8_t>((length >> 24U) & 0xFFU)};
    return result;
}

EncodeResult encode_frame(const std::uint8_t kind, const std::span<const std::byte> payload)
{
    const auto encoded_header = encode_header(kind, payload.size());
    if (!encoded_header) {
        return EncodeResult{{}, encoded_header.error};
    }

    EncodeResult result;
    result.bytes.reserve(header_size + payload.size());
    result.bytes.insert(result.bytes.end(), encoded_header.header.begin(), encoded_header.header.end());
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());
    return result;
}

DecodeResult Decoder::feed(const std::span<const std::byte> input)
{
    DecodeResult result;
    if (error_) {
        result.error = error_;
        return result;
    }

    std::size_t offset = 0;
    while (offset < input.size() && !error_) {
        if (header_bytes_ < header_size) {
            const auto count = std::min(header_size - header_bytes_, input.size() - offset);
            std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(offset), count,
                        header_.begin() + static_cast<std::ptrdiff_t>(header_bytes_));
            header_bytes_ += count;
            offset += count;
            if (header_bytes_ < header_size) {
                break;
            }

            if (!std::equal(magic.begin(), magic.end(), header_.begin())) {
                poison(Error{ErrorCode::invalid_magic});
                break;
            }
            const auto observed_version = as_u8(header_[4]);
            if (observed_version != version) {
                poison(Error{ErrorCode::unsupported_version, observed_version});
                break;
            }

            current_kind_ = as_u8(header_[5]);
            const auto declared = decode_u32_le(std::span<const std::byte, 4>{header_.data() + 6, 4});
            if (declared > max_payload_size) {
                poison(Error{ErrorCode::payload_too_large, 0, declared});
                break;
            }
            expected_payload_size_ = declared;
            if (declared == 0) {
                result.frames.push_back(Frame{current_kind_, {}});
                clear_pending_frame();
                continue;
            }
        }

        const auto remaining = static_cast<std::size_t>(*expected_payload_size_) - payload_.size();
        const auto count = std::min(remaining, input.size() - offset);
        payload_.insert(
            payload_.end(),
            input.begin() + static_cast<std::ptrdiff_t>(offset),
            input.begin() + static_cast<std::ptrdiff_t>(offset + count)
        );
        offset += count;
        if (payload_.size() == *expected_payload_size_) {
            result.frames.push_back(Frame{current_kind_, std::move(payload_)});
            clear_pending_frame();
        }
    }

    result.error = error_;
    return result;
}

void Decoder::reset() noexcept
{
    error_.reset();
    clear_pending_frame();
}

std::size_t Decoder::buffered_bytes() const noexcept
{
    return header_bytes_ + payload_.size();
}

void Decoder::clear_pending_frame() noexcept
{
    header_.fill(std::byte{});
    header_bytes_ = 0;
    current_kind_ = 0;
    expected_payload_size_.reset();
    payload_.clear();
}

void Decoder::poison(const Error error) noexcept
{
    clear_pending_frame();
    error_ = error;
}

}  // namespace baas::service::protocol::bpip
