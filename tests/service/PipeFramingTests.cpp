#include "service/protocol/PipeFraming.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace bpip = baas::service::protocol::bpip;

namespace {

int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::uint8_t hex_digit(const char value)
{
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
    std::abort();
}

[[nodiscard]] bpip::Bytes from_hex(const std::string_view hex)
{
    check(hex.size() % 2 == 0, "test hex must contain complete bytes");
    bpip::Bytes output;
    output.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        output.push_back(std::byte{
            static_cast<std::uint8_t>((hex_digit(hex[i]) << 4U) | hex_digit(hex[i + 1]))
        });
    }
    return output;
}

[[nodiscard]] bpip::Bytes bytes(const std::string_view text)
{
    bpip::Bytes output;
    output.reserve(text.size());
    std::transform(text.begin(), text.end(), std::back_inserter(output), [](const char value) {
        return std::byte{static_cast<std::uint8_t>(static_cast<unsigned char>(value))};
    });
    return output;
}

struct GoldenFrame {
    std::string_view name;
    std::uint8_t kind;
    std::string_view frame_hex;
    std::string_view payload_hex;
};

// Byte-for-byte copies of tests/service_contract/v1_vectors.json. Keeping the
// protocol target dependency-free is intentional; JSON is opaque at this layer.
constexpr std::array golden_frames{
    GoldenFrame{
        "open", 1,
        "425049500101340000007b2274797065223a226f70656e222c226368616e6e656c223a2274726967676572222c226e616d65223a2274726967676572227d",
        "7b2274797065223a226f70656e222c226368616e6e656c223a2274726967676572222c226e616d65223a2274726967676572227d",
    },
    GoldenFrame{
        "open_ok", 1,
        "425049500101260000007b2274797065223a226f70656e5f6f6b222c226368616e6e656c223a2274726967676572227d",
        "7b2274797065223a226f70656e5f6f6b222c226368616e6e656c223a2274726967676572227d",
    },
    GoldenFrame{
        "json", 1,
        "4250495001011d0000007b2274797065223a2270696e67222c2276616c7565223a22e8939d227d",
        "7b2274797065223a2270696e67222c2276616c7565223a22e8939d227d",
    },
    GoldenFrame{"bytes", 2, "425049500102040000000001feff", "0001feff"},
    GoldenFrame{"close", 3, "42504950010300000000", ""},
    GoldenFrame{
        "error", 4,
        "4250495001040f000000696e76616c69642072657175657374",
        "696e76616c69642072657175657374",
    },
};

void test_constants_and_known_kinds()
{
    check(bpip::header_size == 10, "BPIP header must remain exactly 10 bytes");
    check(bpip::max_payload_size == 67'108'864, "payload limit must remain 64 MiB");
    for (std::uint8_t kind = 1; kind <= 4; ++kind) {
        check(bpip::is_known_kind(kind), "kinds 1 through 4 must be known");
    }
    check(!bpip::is_known_kind(0) && !bpip::is_known_kind(255),
          "unknown kind classification must not change transport acceptance");
    check(bpip::error_name(bpip::ErrorCode::invalid_magic) == "invalid_magic",
          "error names must be stable");
    check(bpip::error_name(bpip::ErrorCode::unsupported_version) == "unsupported_version",
          "error names must be stable");
    check(bpip::error_name(bpip::ErrorCode::payload_too_large) == "payload_too_large",
          "error names must be stable");
}

void test_all_golden_frames_encode_and_decode()
{
    for (const auto& golden : golden_frames) {
        const auto expected_frame = from_hex(golden.frame_hex);
        const auto expected_payload = from_hex(golden.payload_hex);
        const auto encoded = bpip::encode_frame(golden.kind, expected_payload);
        check(static_cast<bool>(encoded), "golden frame should encode");
        check(encoded.bytes == expected_frame, golden.name);

        bpip::Decoder decoder;
        const auto decoded = decoder.feed(expected_frame);
        check(!decoded.error, "golden frame should decode without an error");
        check(decoded.frames.size() == 1, "golden frame should decode exactly once");
        if (decoded.frames.size() == 1) {
            check(decoded.frames[0].kind == golden.kind, "golden kind must be retained");
            check(decoded.frames[0].payload == expected_payload, "golden payload must be byte exact");
        }
        check(decoder.buffered_bytes() == 0, "complete frame must leave no pending bytes");
    }
}

void test_open_payloads_remain_opaque_and_exact()
{
    check(from_hex(golden_frames[0].payload_hex)
              == bytes(R"({"type":"open","channel":"trigger","name":"trigger"})"),
          "open payload must match the committed golden vector");
    check(from_hex(golden_frames[1].payload_hex)
              == bytes(R"({"type":"open_ok","channel":"trigger"})"),
          "open_ok payload must match the committed golden vector");
}

void test_one_byte_fragmentation()
{
    const auto frame = from_hex(golden_frames[0].frame_hex);
    bpip::Decoder decoder;
    std::vector<bpip::Frame> frames;
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const auto result = decoder.feed(std::span<const std::byte>{frame.data() + i, 1});
        check(!result.error, "one-byte fragmentation must not fail");
        frames.insert(frames.end(), result.frames.begin(), result.frames.end());
        if (i + 1 < frame.size()) {
            check(frames.empty(), "fragmented frame must remain pending until its final byte");
        }
    }
    check(frames.size() == 1, "one-byte fragmented frame must emit exactly once");
    check(decoder.buffered_bytes() == 0, "fragmented completion must clear pending state");
}

void test_multiple_frames_in_one_feed()
{
    bpip::Bytes joined;
    for (const auto& golden : golden_frames) {
        const auto frame = from_hex(golden.frame_hex);
        joined.insert(joined.end(), frame.begin(), frame.end());
    }
    bpip::Decoder decoder;
    const auto result = decoder.feed(joined);
    check(!result.error, "coalesced frames must decode without error");
    check(result.frames.size() == golden_frames.size(), "all coalesced frames must be emitted");
    for (std::size_t i = 0; i < result.frames.size(); ++i) {
        check(result.frames[i].kind == golden_frames[i].kind, "coalesced frame order must be stable");
        check(result.frames[i].payload == from_hex(golden_frames[i].payload_hex),
              "coalesced payload order must be stable");
    }
}

void test_truncated_input_stays_pending()
{
    bpip::Decoder header_decoder;
    const auto short_header = from_hex("425049500101000000");
    auto result = header_decoder.feed(short_header);
    check(!result.error && result.frames.empty(), "nine-byte header must remain pending");
    check(header_decoder.buffered_bytes() == 9, "truncated header bytes must be retained");
    const std::array final_header_byte{std::byte{0}};
    result = header_decoder.feed(final_header_byte);
    check(!result.error && result.frames.size() == 1,
          "completing a zero-length fragmented header must emit its frame");

    bpip::Decoder payload_decoder;
    const auto short_payload = from_hex("425049500102040000000001");
    result = payload_decoder.feed(short_payload);
    check(!result.error && result.frames.empty(), "truncated payload must remain pending");
    check(payload_decoder.expected_payload_size() == 4, "declared payload size must be retained");
    check(payload_decoder.buffered_payload_bytes() == 2, "only received payload bytes may be buffered");
    const auto suffix = from_hex("feff");
    result = payload_decoder.feed(suffix);
    check(!result.error && result.frames.size() == 1, "payload suffix must complete the frame");
    check(result.frames[0].payload == from_hex("0001feff"), "fragmented payload must remain exact");
}

void test_malformed_headers_and_sticky_error()
{
    struct ErrorCase {
        std::string_view bytes_hex;
        bpip::ErrorCode code;
        std::uint8_t observed_version;
        std::uint64_t declared_size;
    };
    constexpr std::array cases{
        ErrorCase{"4e4f5045010100000000", bpip::ErrorCode::invalid_magic, 0, 0},
        ErrorCase{"42504950020100000000", bpip::ErrorCode::unsupported_version, 2, 0},
        // Big-endian 0x01020304 is decoded as little-endian 0x04030201.
        ErrorCase{"42504950010101020304", bpip::ErrorCode::payload_too_large, 0, 67'305'985},
        ErrorCase{"42504950010201000004", bpip::ErrorCode::payload_too_large, 0, 67'108'865},
    };

    for (const auto& malformed : cases) {
        bpip::Decoder decoder;
        const auto result = decoder.feed(from_hex(malformed.bytes_hex));
        check(result.error.has_value(), "malformed header must produce a stable error");
        if (result.error) {
            check(result.error->code == malformed.code, "malformed error category must be stable");
            check(result.error->observed_version == malformed.observed_version,
                  "observed version detail must be stable");
            check(result.error->declared_payload_size == malformed.declared_size,
                  "declared payload detail must be stable");
        }
        check(decoder.has_error() && decoder.buffered_bytes() == 0,
              "error must poison decoder and discard pending bytes");

        const auto ignored = decoder.feed(from_hex(golden_frames[4].frame_hex));
        check(ignored.frames.empty() && ignored.error == result.error,
              "poisoned decoder must consume nothing and repeat its first error");
        decoder.reset();
        const auto recovered = decoder.feed(from_hex(golden_frames[4].frame_hex));
        check(!recovered.error && recovered.frames.size() == 1,
              "explicit reset must make decoder reusable");
    }
}

void test_complete_frames_before_error_are_returned()
{
    auto bytes = from_hex(golden_frames[4].frame_hex);
    const auto invalid = from_hex("4e4f5045010100000000");
    bytes.insert(bytes.end(), invalid.begin(), invalid.end());
    bpip::Decoder decoder;
    const auto result = decoder.feed(bytes);
    check(result.frames.size() == 1, "valid frames before a malformed header must be returned");
    check(result.frames[0].kind == 3, "frame preceding an error must stay intact");
    check(result.error && result.error->code == bpip::ErrorCode::invalid_magic,
          "error after a valid frame must still poison the decoder");
}

void test_unknown_kind_is_preserved()
{
    const auto frame = from_hex("4250495001ff00000000");
    bpip::Decoder decoder;
    const auto result = decoder.feed(frame);
    check(!result.error && result.frames.size() == 1,
          "unknown kind must remain accepted at the transport layer");
    check(result.frames[0].kind == 255 && result.frames[0].payload.empty(),
          "unknown kind value must be retained verbatim");
    const auto encoded = bpip::encode_frame(255, {});
    check(static_cast<bool>(encoded) && encoded.bytes == frame,
          "unknown kind must also round-trip through the encoder");
}

void test_known_kind_overload()
{
    const auto encoded = bpip::encode_frame(bpip::FrameKind::close, {});
    check(static_cast<bool>(encoded) && encoded.bytes == from_hex(golden_frames[4].frame_hex),
          "known frame enum overload must encode the same wire kind");
}

void test_exact_payload_boundary_without_materializing_64_mib()
{
    const auto maximum = bpip::encode_header(2, bpip::max_payload_size);
    check(static_cast<bool>(maximum), "exactly 64 MiB must be accepted");
    check(maximum.header == [] {
        bpip::Header expected{};
        const auto bytes = from_hex("42504950010200000004");
        std::copy(bytes.begin(), bytes.end(), expected.begin());
        return expected;
    }(), "maximum header must match the golden little-endian bytes");

    bpip::Decoder decoder;
    const auto pending = decoder.feed(maximum.header);
    check(!pending.error && pending.frames.empty(), "maximum header alone must remain pending");
    check(decoder.expected_payload_size() == bpip::max_payload_size,
          "decoder must accept the inclusive maximum declaration");
    check(decoder.buffered_payload_bytes() == 0,
          "maximum declaration must not eagerly allocate/copy payload bytes");

    const auto oversized = bpip::encode_header(2, static_cast<std::uint64_t>(bpip::max_payload_size) + 1U);
    check(!static_cast<bool>(oversized) && oversized.error,
          "64 MiB plus one must be rejected by the encoder");
    check(oversized.error->code == bpip::ErrorCode::payload_too_large
              && oversized.error->declared_payload_size == 67'108'865,
          "oversize encoder details must be stable");
}

}  // namespace

int main()
{
    test_constants_and_known_kinds();
    test_all_golden_frames_encode_and_decode();
    test_open_payloads_remain_opaque_and_exact();
    test_one_byte_fragmentation();
    test_multiple_frames_in_one_feed();
    test_truncated_input_stays_pending();
    test_malformed_headers_and_sticky_error();
    test_complete_frames_before_error_are_returned();
    test_unknown_kind_is_preserved();
    test_known_kind_overload();
    test_exact_payload_boundary_without_materializing_64_mib();

    if (failures != 0) {
        std::cerr << failures << " service pipe framing test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All service pipe framing tests passed\n";
    return EXIT_SUCCESS;
}
