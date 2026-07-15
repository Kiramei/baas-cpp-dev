#include "service/auth/CanonicalJson.h"
#include "service/auth/Crypto.h"
#include "service/auth/SecureEnvelope.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef BAAS_SERVICE_CONTRACT_VECTOR_PATH
#error BAAS_SERVICE_CONTRACT_VECTOR_PATH must identify the checked-in v1 fixture
#endif

namespace service_auth = baas::service::auth;

namespace {

int failures = 0;

template <typename Condition>
void check(const Condition& condition, const std::string_view message)
{
    if (static_cast<bool>(condition)) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value) noexcept
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

[[nodiscard]] std::string read_fixture()
{
    std::ifstream input{BAAS_SERVICE_CONTRACT_VECTOR_PATH, std::ios::binary};
    if (!input) throw std::runtime_error("could not open v1_vectors.json");
    std::string fixture{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    // The fixture container has one uint64 stream-sequence metadata value that
    // is intentionally not itself protocol JSON. Keep the production parser's
    // safe-integer boundary strict and tag this one test-only metadata token.
    constexpr std::string_view unsafe_metadata = "\"seq\": 72623859790382856";
    constexpr std::string_view tagged_metadata =
        "\"seq\": \"@fixture-u64:72623859790382856\"";
    const auto position = fixture.find(unsafe_metadata);
    if (position == std::string::npos
        || fixture.find(unsafe_metadata, position + unsafe_metadata.size())
            != std::string::npos) {
        throw std::runtime_error("unexpected unsafe-integer fixture shape");
    }
    fixture.replace(position, unsafe_metadata.size(), tagged_metadata);
    return fixture;
}

[[nodiscard]] const service_auth::CanonicalJsonValue& member(
    const service_auth::CanonicalJsonValue& value,
    const std::string_view key)
{
    const auto* found = value.find(key);
    if (found == nullptr) throw std::runtime_error("fixture member is missing");
    return *found;
}

[[nodiscard]] const std::string& string_value(
    const service_auth::CanonicalJsonValue& value)
{
    const auto* result = value.as_string();
    if (result == nullptr) throw std::runtime_error("fixture value is not a string");
    return *result;
}

[[nodiscard]] std::int64_t integer_value(
    const service_auth::CanonicalJsonValue& value)
{
    const auto* result = value.as_integer();
    if (result == nullptr) throw std::runtime_error("fixture value is not an integer");
    return *result;
}

[[nodiscard]] std::uint64_t fixture_uint64_value(
    const service_auth::CanonicalJsonValue& value)
{
    if (const auto* integer = value.as_integer()) {
        if (*integer < 0) throw std::runtime_error("fixture value is negative");
        return static_cast<std::uint64_t>(*integer);
    }
    const auto* string = value.as_string();
    constexpr std::string_view prefix = "@fixture-u64:";
    if (string == nullptr || !string->starts_with(prefix))
        throw std::runtime_error("fixture value is not an unsigned integer");
    std::uint64_t output = 0;
    const auto digits = std::string_view{*string}.substr(prefix.size());
    const auto result = std::from_chars(
        digits.data(), digits.data() + digits.size(), output);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size())
        throw std::runtime_error("fixture uint64 token is malformed");
    return output;
}

[[nodiscard]] bool boolean_value(const service_auth::CanonicalJsonValue& value)
{
    const auto* result = value.as_boolean();
    if (result == nullptr) throw std::runtime_error("fixture value is not a boolean");
    return *result;
}

[[nodiscard]] const service_auth::CanonicalJsonValue::Array& array_value(
    const service_auth::CanonicalJsonValue& value)
{
    const auto* result = value.as_array();
    if (result == nullptr) throw std::runtime_error("fixture value is not an array");
    return *result;
}

[[nodiscard]] const service_auth::CanonicalJsonValue& named_case(
    const service_auth::CanonicalJsonValue::Array& values,
    const std::string_view name)
{
    const auto found = std::find_if(values.begin(), values.end(), [&](const auto& value) {
        const auto* candidate = value.find("name");
        return candidate != nullptr && candidate->as_string() != nullptr
            && *candidate->as_string() == name;
    });
    if (found == values.end()) throw std::runtime_error("named fixture case is missing");
    return *found;
}

[[nodiscard]] int hex_digit(const char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    throw std::runtime_error("invalid fixture hex");
}

[[nodiscard]] service_auth::PublicBytes from_hex(const std::string_view input)
{
    if (input.size() % 2 != 0) throw std::runtime_error("odd fixture hex");
    service_auth::PublicBytes output(input.size() / 2);
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::byte>(
            (hex_digit(input[index * 2]) << 4) | hex_digit(input[index * 2 + 1]));
    }
    return output;
}

[[nodiscard]] std::string to_hex(const std::span<const std::byte> input)
{
    constexpr std::array<char, 16> hex{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string output(input.size() * 2, '0');
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto value = std::to_integer<unsigned char>(input[index]);
        output[index * 2] = hex[value >> 4U];
        output[index * 2 + 1] = hex[value & 0x0FU];
    }
    return output;
}

[[nodiscard]] std::string canonical(const service_auth::CanonicalJsonValue& value)
{
    const auto encoded = service_auth::encode_canonical_json_value(value);
    if (!encoded) throw std::runtime_error("fixture value is outside canonical domain");
    return encoded.text;
}

[[nodiscard]] const service_auth::CanonicalJsonValue& derivation(
    const service_auth::CanonicalJsonValue& root,
    const std::string_view name)
{
    return named_case(
        array_value(member(member(root, "hkdf_sha256"), "derivations")), name);
}

void test_runtime_and_canonical_json(const service_auth::CanonicalJsonValue& root)
{
    check(service_auth::sodium_runtime_ready(), "sodium_init must succeed before any operation");
    check(service_auth::crypto_error_name(service_auth::CryptoError::authentication_failed)
              == "authentication_failed",
          "crypto error names must be stable");
    check(service_auth::canonical_json_error_name(
              service_auth::CanonicalJsonError::duplicate_key) == "duplicate_key",
          "canonical JSON error names must be stable");

    for (const auto& item : array_value(member(root, "canonical_json"))) {
        const auto expected = string_value(member(item, "utf8"));
        check(canonical(member(item, "value")) == expected,
              "canonical JSON fixture bytes must match C++ serialization");
        check(to_hex(bytes(expected)) == string_value(member(item, "hex")),
              "canonical JSON fixture hex must match serialized UTF-8");
        const auto reparsed = service_auth::parse_canonical_json_value(expected);
        check(reparsed && canonical(*reparsed.value) == expected,
              "canonical fixture must round-trip through the bounded parser");
    }

    const auto duplicate = service_auth::parse_canonical_json_value(R"({"a":1,"a":2})");
    check(duplicate.error == service_auth::CanonicalJsonError::duplicate_key,
          "literal duplicate JSON keys must be rejected");
    const auto escaped_duplicate = service_auth::parse_canonical_json_value(
        R"({"a":1,"\u0061":2})");
    check(escaped_duplicate.error == service_auth::CanonicalJsonError::duplicate_key,
          "escaped duplicate JSON keys must be rejected after decoding");
    std::string invalid_utf8{"{\"value\":\""};
    invalid_utf8.push_back(static_cast<char>(0xC0));
    invalid_utf8.push_back(static_cast<char>(0xAF));
    invalid_utf8 += "\"}";
    check(service_auth::parse_canonical_json_value(invalid_utf8).error
              == service_auth::CanonicalJsonError::invalid_utf8,
          "invalid UTF-8 must be rejected before schema parsing");
    check(service_auth::parse_canonical_json_value("9007199254740992").error
              == service_auth::CanonicalJsonError::unsafe_integer,
          "integers above the JavaScript safe range must be rejected");
    check(service_auth::parse_canonical_json_value("-9007199254740992").error
              == service_auth::CanonicalJsonError::unsafe_integer,
          "integers below the JavaScript safe range must be rejected");
    check(service_auth::parse_canonical_json_value("1.0").error
              == service_auth::CanonicalJsonError::unsupported_number,
          "floating point JSON must be outside the protocol-safe domain");
    check(service_auth::parse_canonical_json_value("1e0").error
              == service_auth::CanonicalJsonError::unsupported_number,
          "exponent JSON must be outside the protocol-safe domain");
    check(service_auth::parse_canonical_json_value("01").error
              == service_auth::CanonicalJsonError::invalid_json,
          "noncanonical leading-zero integers must be rejected");
    check(service_auth::parse_canonical_json_value(R"("\uD800")").error
              == service_auth::CanonicalJsonError::invalid_json,
          "unpaired UTF-16 surrogates must be rejected");

    service_auth::CanonicalJsonValue programmatic_duplicate{
        service_auth::CanonicalJsonValue::Object{
            {"x", service_auth::CanonicalJsonValue{std::int64_t{1}}},
            {"x", service_auth::CanonicalJsonValue{std::int64_t{2}}}}};
    check(service_auth::encode_canonical_json_value(programmatic_duplicate).error
              == service_auth::CanonicalJsonError::duplicate_key,
          "programmatic duplicate keys must fail closed at serialization");

    service_auth::CanonicalJsonLimits limits;
    limits.max_input_bytes = 3;
    check(service_auth::parse_canonical_json_value("null", limits).error
              == service_auth::CanonicalJsonError::input_too_large,
          "canonical JSON input byte limit must be enforced");
    limits = {};
    limits.max_depth = 2;
    check(service_auth::parse_canonical_json_value("[[0]]", limits).error
              == service_auth::CanonicalJsonError::depth_exceeded,
          "canonical JSON parse depth must be bounded");
    limits = {};
    limits.max_values = 2;
    check(service_auth::parse_canonical_json_value("[0,1]", limits).error
              == service_auth::CanonicalJsonError::value_limit_exceeded,
          "canonical JSON parse value count must be bounded");
    limits = {};
    limits.max_output_bytes = 3;
    check(service_auth::encode_canonical_json_value(
              service_auth::CanonicalJsonValue{}, limits).error
              == service_auth::CanonicalJsonError::output_too_large,
          "canonical JSON output byte limit must be enforced");
}

void test_base64_and_sequence_contract(const service_auth::CanonicalJsonValue& root)
{
    const auto& base64 = member(root, "base64url");
    check(boolean_value(member(base64, "encoder_retains_rfc4648_padding")),
          "fixture must retain RFC 4648 padding");
    for (const auto& item : array_value(member(base64, "cases"))) {
        const auto raw = from_hex(string_value(member(item, "raw_hex")));
        const auto encoded = service_auth::encode_base64url_padded(raw);
        check(encoded && *encoded.value == string_value(member(item, "encoded")),
              "padded URL-safe base64 encoding must match fixture");
        const auto decoded = service_auth::decode_base64url_canonical(
            string_value(member(item, "encoded")), raw.size());
        check(decoded && service_auth::constant_time_equal(*decoded.value, raw),
              "canonical URL-safe base64 must decode exact fixture bytes");
    }
    check(service_auth::decode_base64url_canonical("AA").error
              == service_auth::CryptoError::invalid_base64,
          "missing canonical padding must be rejected");
    check(service_auth::decode_base64url_canonical("AA===").error
              == service_auth::CryptoError::invalid_base64,
          "excess padding must be rejected");
    check(service_auth::decode_base64url_canonical("AA+/ ").error
              == service_auth::CryptoError::invalid_base64,
          "standard alphabet and ignored whitespace must be rejected");
    check(service_auth::decode_base64url_canonical("AB==").error
              == service_auth::CryptoError::invalid_base64
              || service_auth::decode_base64url_canonical("AB==").error
                  == service_auth::CryptoError::noncanonical_base64,
          "non-zero unused base64 bits must be rejected");
    check(service_auth::decode_base64url_canonical("AA==", 2).error
              == service_auth::CryptoError::invalid_length,
          "exact decoded widths must be enforced before use");

    for (const auto& item : array_value(member(member(root, "control"), "sequences"))) {
        const auto sequence = static_cast<std::uint64_t>(integer_value(member(item, "seq")));
        check(to_hex(service_auth::control_sequence_nonce(sequence))
                  == string_value(member(item, "nonce_hex")),
              "control nonce must be 12-byte big-endian sequence");
        const auto aad = service_auth::control_sequence_aad(sequence);
        check(aad && *aad.value == string_value(member(item, "aad_utf8")),
              "control AAD must be exact canonical JSON");
    }
    check(service_auth::control_sequence_aad(
              static_cast<std::uint64_t>(service_auth::maximum_safe_json_integer)),
          "maximum JSON-safe control sequence must be accepted");
    check(service_auth::control_sequence_aad(
              static_cast<std::uint64_t>(service_auth::maximum_safe_json_integer) + 1).error
              == service_auth::CryptoError::invalid_input,
          "control sequence above the JSON-safe range must be rejected");
}

void test_hash_hmac_hkdf(const service_auth::CanonicalJsonValue& root)
{
    for (const auto& item : array_value(member(member(root, "hkdf_sha256"), "derivations"))) {
        const auto ikm = from_hex(string_value(member(item, "ikm_hex")));
        const auto salt = from_hex(string_value(member(item, "salt_hex")));
        const auto& info_text = string_value(member(item, "info_utf8"));
        const auto output_size = static_cast<std::size_t>(integer_value(member(item, "length")));
        const auto derived = service_auth::hkdf_sha256(ikm, salt, bytes(info_text), output_size);
        check(derived && to_hex(derived.value->bytes())
                  == string_value(member(item, "output_hex")),
              "every v1 HKDF label/context must match exact fixture bytes");
    }
    check(service_auth::hkdf_sha256({}, {}, {}, 0).error
              == service_auth::CryptoError::invalid_length,
          "zero-length HKDF output must be rejected");
    check(service_auth::hkdf_sha256({}, {}, {}, 255 * service_auth::sha256_bytes + 1).error
              == service_auth::CryptoError::invalid_length,
          "HKDF output beyond RFC 5869 limit must be rejected");

    const auto& handshake = member(root, "handshake_crypto");
    const auto& transcript = string_value(member(handshake, "transcript_utf8"));
    const auto digest = service_auth::sha256(bytes(transcript));
    check(digest && to_hex(*digest.value) == string_value(member(handshake, "transcript_sha256")),
          "SHA-256 transcript digest must match fixture");

    const auto& contexts = member(root, "contexts");
    const auto resume_secret = from_hex(string_value(member(
        derivation(root, "resume_secret"), "output_hex")));
    const auto& remember = string_value(member(contexts, "remember_session_utf8"));
    const auto remember_hmac = service_auth::hmac_sha256(resume_secret, bytes(remember));
    check(remember_hmac && to_hex(*remember_hmac.value)
              == string_value(member(contexts, "remember_proof_hmac_sha256_hex")),
          "remember HMAC context must match fixture");
    const auto& business = string_value(member(contexts, "business_resume_utf8"));
    const auto business_hmac = service_auth::hmac_sha256(resume_secret, bytes(business));
    check(business_hmac && to_hex(*business_hmac.value)
              == string_value(member(contexts, "business_resume_hmac_sha256_hex")),
          "business resume HMAC context must match fixture");

    const auto& aad_prefix = string_value(member(contexts, "stream_aad_prefix_utf8"));
    for (const auto& item : array_value(member(contexts, "stream_aad"))) {
        const auto sequence = fixture_uint64_value(member(item, "seq"));
        std::string expected = aad_prefix;
        for (std::size_t offset = 0; offset < sizeof(sequence); ++offset) {
            expected.push_back(static_cast<char>(
                (sequence >> ((sizeof(sequence) - 1 - offset) * 8U)) & 0xFFU));
        }
        check(to_hex(bytes(expected)) == string_value(member(item, "hex")),
              "secretstream AAD prefix and uint64 suffix must match fixture bytes");
    }
}

void test_asymmetric_and_argon2(const service_auth::CanonicalJsonValue& root)
{
    const auto& handshake = member(root, "handshake_crypto");
    const auto& transcript_object = member(handshake, "transcript_object");
    const auto& transcript_client = member(transcript_object, "client");
    const auto& transcript_server = member(transcript_object, "server");
    for (const auto* encoded : {
             &string_value(member(transcript_client, "client_nonce")),
             &string_value(member(transcript_client, "client_kx_pub")),
             &string_value(member(transcript_server, "server_nonce")),
             &string_value(member(transcript_server, "server_kx_pub")),
             &string_value(member(handshake, "production_pinned_signing_public_b64url")),
             &string_value(member(handshake, "synthetic_test_signing_public_b64url"))}) {
        check(service_auth::decode_base64url_canonical(*encoded, 32),
              "nonce, X25519, and Ed25519 public fields must decode to exactly 32 bytes");
    }
    const auto client_private = from_hex(string_value(member(handshake, "client_private_hex")));
    const auto server_private = from_hex(string_value(member(handshake, "server_private_hex")));
    const auto client_public = service_auth::x25519_public_key(client_private);
    const auto server_public = service_auth::x25519_public_key(server_private);
    check(client_public && to_hex(*client_public.value)
              == string_value(member(handshake, "client_public_hex")),
          "X25519 public key must match fixture");
    const auto shared = service_auth::x25519_shared_secret(
        client_private, *server_public.value);
    check(shared && to_hex(shared.value->bytes())
              == string_value(member(handshake, "shared_secret_hex")),
          "X25519 shared secret must match fixture");
    check(service_auth::x25519_public_key(
              std::span<const std::byte>{client_private}.first(31)).error
              == service_auth::CryptoError::invalid_length,
          "X25519 private keys must be exactly 32 bytes");
    service_auth::PublicBytes zero_public(service_auth::x25519_key_bytes);
    check(service_auth::x25519_shared_secret(client_private, zero_public).error
              == service_auth::CryptoError::verification_failed,
          "X25519 low-order/all-zero peer keys must fail closed");

    const auto seed = from_hex(string_value(member(
        handshake, "synthetic_test_signing_seed_hex")));
    const auto public_key = service_auth::ed25519_public_key_from_seed(seed);
    check(public_key, "Ed25519 public key derivation must succeed");
    const auto encoded_public = service_auth::encode_base64url_padded(*public_key.value);
    check(encoded_public && *encoded_public.value
              == string_value(member(handshake, "synthetic_test_signing_public_b64url")),
          "Ed25519 public key must match fixture");
    const auto& transcript = string_value(member(handshake, "transcript_utf8"));
    const auto signature = service_auth::ed25519_sign(seed, bytes(transcript));
    const auto fixture_signature = service_auth::decode_base64url_canonical(
        string_value(member(handshake, "ed25519_signature_b64url")),
        service_auth::ed25519_signature_bytes);
    check(signature && fixture_signature
              && service_auth::constant_time_equal(*signature.value, *fixture_signature.value),
          "Ed25519 deterministic signature must match fixture");
    check(service_auth::ed25519_verify(
              *public_key.value, bytes(transcript), *signature.value)
              == service_auth::CryptoError::none,
          "fixture Ed25519 signature must verify");
    auto wrong_signature = *signature.value;
    wrong_signature.front() ^= std::byte{1};
    check(service_auth::ed25519_verify(
              *public_key.value, bytes(transcript), wrong_signature)
              == service_auth::CryptoError::verification_failed,
          "wrong Ed25519 signature must fail verification");
    check(service_auth::ed25519_verify(
              *public_key.value, bytes(transcript),
              std::span<const std::byte>{wrong_signature}.first(63))
              == service_auth::CryptoError::invalid_length,
          "Ed25519 signatures must be exactly 64 bytes");

    const auto& argon = member(handshake, "argon2id");
    check(integer_value(member(argon, "opslimit"))
              == static_cast<std::int64_t>(service_auth::argon2id_v1_opslimit)
              && integer_value(member(argon, "memlimit"))
                  == static_cast<std::int64_t>(service_auth::argon2id_v1_memlimit)
              && integer_value(member(argon, "output_bytes"))
                  == static_cast<std::int64_t>(service_auth::argon2id_output_bytes),
          "Argon2id v1 constants must match the fixture");
    const auto salt = from_hex(string_value(member(argon, "salt_hex")));
    const auto password_hash = service_auth::argon2id_v1(
        string_value(member(argon, "password")), salt);
    check(password_hash && to_hex(password_hash.value->bytes())
              == string_value(member(argon, "output_hex")),
          "Argon2id output must match fixture bytes");
    check(service_auth::argon2id_v1(
              "password", std::span<const std::byte>{salt}.first(15)).error
              == service_auth::CryptoError::invalid_length,
          "Argon2id salts must be exactly 16 bytes");
}

void test_secure_envelopes(const service_auth::CanonicalJsonValue& root)
{
    const auto transmit_key = from_hex(string_value(member(
        derivation(root, "preauth_server_tx"), "output_hex")));
    const auto receive_key = from_hex(string_value(member(
        derivation(root, "preauth_server_rx"), "output_hex")));
    auto sender_result = service_auth::SecureEnvelopeCipher::create(
        transmit_key, receive_key);
    auto receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    check(sender_result && receiver_result,
          "secure envelope ciphers must accept exact directional keys");
    auto sender = std::move(*sender_result.value);
    auto receiver = std::move(*receiver_result.value);
    const auto& cases = array_value(member(member(root, "control"),
                                                "chacha20poly1305_envelopes"));
    for (const auto& item : cases) {
        const auto encrypted = sender.encrypt(member(item, "payload"));
        check(encrypted && encrypted.envelope == canonical(member(item, "envelope")),
              "secure envelope ciphertext must match deterministic fixture bytes");
        const auto decrypted = receiver.decrypt(canonical(member(item, "envelope")));
        check(decrypted && canonical(*decrypted.plaintext)
                  == canonical(member(item, "payload")),
              "secure envelope fixture must decrypt to canonical payload");
    }

    check(service_auth::SecureEnvelopeCipher::create(
              std::span<const std::byte>{transmit_key}.first(31), receive_key).error
              == service_auth::CryptoError::invalid_length,
          "secure envelope transmit key width must be exact");
    check(service_auth::SecureEnvelopeCipher::create(
              transmit_key, std::span<const std::byte>{receive_key}.first(31)).error
              == service_auth::CryptoError::invalid_length,
          "secure envelope receive key width must be exact");
    check(service_auth::SecureEnvelopeCipher::create(
              transmit_key, receive_key,
              static_cast<std::uint64_t>(service_auth::maximum_safe_json_integer) + 1).error
              == service_auth::CryptoError::invalid_input,
          "secure envelope sequence must remain JSON-safe");
    check(service_auth::SecureEnvelopeCipher::create(
              transmit_key, transmit_key).error
              == service_auth::CryptoError::invalid_input,
          "secure envelope directions must not reuse a key/nonce space");

    const auto maximum_sequence = static_cast<std::uint64_t>(
        service_auth::maximum_safe_json_integer);
    auto maximum_sender_result = service_auth::SecureEnvelopeCipher::create(
        transmit_key, receive_key, maximum_sequence, 0);
    auto maximum_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key, 0, maximum_sequence);
    auto maximum_sender = std::move(*maximum_sender_result.value);
    auto maximum_receiver = std::move(*maximum_receiver_result.value);
    const auto maximum_envelope = maximum_sender.encrypt(
        member(cases.front(), "payload"));
    check(maximum_envelope
              && maximum_receiver.decrypt(maximum_envelope.envelope),
          "maximum JSON-safe secure sequence must complete once");
    check(maximum_sender.encrypt(member(cases.front(), "payload")).error
              == service_auth::SecureEnvelopeError::sequence_exhausted
              && maximum_receiver.decrypt(maximum_envelope.envelope).error
                  == service_auth::SecureEnvelopeError::sequence_exhausted,
          "secure sequence must fail closed after the maximum safe value");

    const auto first_envelope = canonical(member(cases.front(), "envelope"));
    auto replay_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    auto replay_receiver = std::move(*replay_receiver_result.value);
    check(replay_receiver.decrypt(first_envelope), "first sequence must be accepted once");
    check(replay_receiver.decrypt(first_envelope).error
              == service_auth::SecureEnvelopeError::sequence_mismatch,
          "duplicate secure sequence must be rejected");
    check(replay_receiver.next_receive_sequence() == 1,
          "rejected replay must not advance receive sequence");

    auto wrong_sequence_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key, 0, 1);
    auto wrong_sequence = std::move(*wrong_sequence_result.value);
    check(wrong_sequence.decrypt(first_envelope).error
              == service_auth::SecureEnvelopeError::sequence_mismatch,
          "backward secure sequence must be rejected");
    auto gap_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    auto gap_receiver = std::move(*gap_receiver_result.value);
    check(gap_receiver.decrypt(canonical(member(cases[1], "envelope"))).error
              == service_auth::SecureEnvelopeError::sequence_mismatch,
          "skipped/forward secure sequence must be rejected");

    auto tampered_value = service_auth::parse_canonical_json_value(first_envelope);
    auto tampered_ciphertext = string_value(member(*tampered_value.value, "ciphertext"));
    tampered_ciphertext.front() = tampered_ciphertext.front() == 'A' ? 'B' : 'A';
    const auto tampered = std::string{R"({"ciphertext":")"} + tampered_ciphertext
        + R"(","seq":0,"type":"secure"})";
    auto tag_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    auto tag_receiver = std::move(*tag_receiver_result.value);
    check(tag_receiver.decrypt(tampered).error
              == service_auth::SecureEnvelopeError::authentication_failed,
          "wrong ChaCha20-Poly1305 tag/ciphertext must fail authentication");
    check(tag_receiver.next_receive_sequence() == 0,
          "failed authentication must not advance receive sequence");
    auto wrong_key = transmit_key;
    wrong_key.front() ^= std::byte{1};
    auto wrong_key_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, wrong_key);
    auto wrong_key_receiver = std::move(*wrong_key_receiver_result.value);
    check(wrong_key_receiver.decrypt(first_envelope).error
              == service_auth::SecureEnvelopeError::authentication_failed,
          "wrong but correctly sized receive keys must fail authentication");

    auto duplicate_outer_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    auto duplicate_outer = std::move(*duplicate_outer_result.value);
    check(duplicate_outer.decrypt(
              R"({"ciphertext":"AA==","seq":0,"seq":0,"type":"secure"})").error
              == service_auth::SecureEnvelopeError::invalid_envelope,
          "duplicate secure envelope fields must be rejected");
    check(duplicate_outer.decrypt(
              R"({"ciphertext":"AA==","seq":9007199254740992,"type":"secure"})").error
              == service_auth::SecureEnvelopeError::invalid_envelope,
          "unsafe secure envelope sequence values must be rejected");

    const auto nonce = service_auth::control_sequence_nonce(0);
    const auto aad = service_auth::control_sequence_aad(0);
    check(aad, "control AAD construction must succeed for sequence zero");
    const std::string noncanonical_plaintext = R"({ "a":1})";
    const auto noncanonical_ciphertext = service_auth::chacha20poly1305_ietf_encrypt(
        transmit_key, nonce, bytes(noncanonical_plaintext), bytes(*aad.value));
    const auto noncanonical_b64 = service_auth::encode_base64url_padded(
        *noncanonical_ciphertext.value);
    const auto noncanonical_envelope = std::string{R"({"ciphertext":")"}
        + *noncanonical_b64.value + R"(","seq":0,"type":"secure"})";
    auto canonical_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    auto canonical_receiver = std::move(*canonical_receiver_result.value);
    check(canonical_receiver.decrypt(noncanonical_envelope).error
              == service_auth::SecureEnvelopeError::invalid_plaintext,
          "authenticated but noncanonical plaintext JSON must be rejected");
    check(canonical_receiver.next_receive_sequence() == 0,
          "invalid plaintext must not advance receive sequence");

    const std::string duplicate_plaintext = R"({"a":1,"a":2})";
    const auto duplicate_ciphertext = service_auth::chacha20poly1305_ietf_encrypt(
        transmit_key, nonce, bytes(duplicate_plaintext), bytes(*aad.value));
    const auto duplicate_b64 = service_auth::encode_base64url_padded(*duplicate_ciphertext.value);
    const auto duplicate_envelope = std::string{R"({"ciphertext":")"}
        + *duplicate_b64.value + R"(","seq":0,"type":"secure"})";
    auto duplicate_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    auto duplicate_receiver = std::move(*duplicate_receiver_result.value);
    check(duplicate_receiver.decrypt(duplicate_envelope).error
              == service_auth::SecureEnvelopeError::invalid_plaintext,
          "authenticated duplicate-key plaintext must be rejected");

    std::string invalid_plaintext{"{\"x\":\""};
    invalid_plaintext.push_back(static_cast<char>(0xC0));
    invalid_plaintext.push_back(static_cast<char>(0xAF));
    invalid_plaintext += "\"}";
    const auto invalid_ciphertext = service_auth::chacha20poly1305_ietf_encrypt(
        transmit_key, nonce, bytes(invalid_plaintext), bytes(*aad.value));
    const auto invalid_b64 = service_auth::encode_base64url_padded(*invalid_ciphertext.value);
    const auto invalid_envelope = std::string{R"({"ciphertext":")"}
        + *invalid_b64.value + R"(","seq":0,"type":"secure"})";
    auto invalid_receiver_result = service_auth::SecureEnvelopeCipher::create(
        receive_key, transmit_key);
    auto invalid_receiver = std::move(*invalid_receiver_result.value);
    check(invalid_receiver.decrypt(invalid_envelope).error
              == service_auth::SecureEnvelopeError::invalid_plaintext,
          "authenticated invalid UTF-8 plaintext must be rejected");
}

void test_low_level_aead_and_secret_hardening(const service_auth::CanonicalJsonValue& root)
{
    const auto key = from_hex(string_value(member(
        derivation(root, "preauth_server_tx"), "output_hex")));
    const auto nonce = service_auth::control_sequence_nonce(0);
    const auto aad = service_auth::control_sequence_aad(0);
    check(aad, "control AAD construction must succeed for low-level AEAD test");
    const std::string plaintext = "secret";
    const auto ciphertext = service_auth::chacha20poly1305_ietf_encrypt(
        key, nonce, bytes(plaintext), bytes(*aad.value));
    check(ciphertext, "IETF ChaCha20-Poly1305 encryption must succeed");
    const auto decrypted = service_auth::chacha20poly1305_ietf_decrypt(
        key, nonce, *ciphertext.value, bytes(*aad.value));
    check(decrypted && service_auth::constant_time_equal(
              decrypted.value->bytes(), bytes(plaintext)),
          "IETF ChaCha20-Poly1305 decryption must recover exact bytes");
    auto wrong_tag = *ciphertext.value;
    wrong_tag.back() ^= std::byte{1};
    check(service_auth::chacha20poly1305_ietf_decrypt(
              key, nonce, wrong_tag, bytes(*aad.value)).error
              == service_auth::CryptoError::authentication_failed,
          "wrong AEAD tag must fail closed");
    check(service_auth::chacha20poly1305_ietf_encrypt(
              std::span<const std::byte>{key}.first(31), nonce,
              bytes(plaintext), bytes(*aad.value)).error
              == service_auth::CryptoError::invalid_length,
          "ChaCha key width must be exact");
    check(service_auth::chacha20poly1305_ietf_encrypt(
              key, std::span<const std::byte>{nonce}.first(11),
              bytes(plaintext), bytes(*aad.value)).error
              == service_auth::CryptoError::invalid_length,
          "ChaCha nonce width must be exact");
    check(service_auth::chacha20poly1305_ietf_decrypt(
              key, nonce, std::span<const std::byte>{wrong_tag}.first(15),
              bytes(*aad.value)).error == service_auth::CryptoError::invalid_length,
          "ciphertexts shorter than an AEAD tag must be rejected");

    service_auth::PublicBytes left{std::byte{1}, std::byte{2}, std::byte{3}};
    auto right = left;
    check(service_auth::constant_time_equal(left, right),
          "constant-time equality must accept equal bytes");
    right.back() ^= std::byte{1};
    check(!service_auth::constant_time_equal(left, right),
          "constant-time equality must reject different bytes");
    check(!service_auth::constant_time_equal(left,
              std::span<const std::byte>{right}.first(2)),
          "constant-time equality must reject different lengths");
    service_auth::secure_zero(left);
    check(std::all_of(left.begin(), left.end(), [](const auto value) {
              return value == std::byte{0};
          }),
          "explicit secret zeroization must overwrite every live byte");
    service_auth::SecretBuffer owned_secret{right};
    owned_secret.clear();
    check(owned_secret.empty(), "move-only secret storage must clear live bytes and size");
}

}  // namespace

int main()
{
    try {
        const auto fixture_text = read_fixture();
        service_auth::CanonicalJsonLimits limits;
        limits.max_input_bytes = 4U * 1'024U * 1'024U;
        limits.max_output_bytes = 4U * 1'024U * 1'024U;
        limits.max_values = 262'144;
        const auto fixture = service_auth::parse_canonical_json_value(fixture_text, limits);
        if (!fixture) {
            std::cerr << "fixture parse failed: "
                      << service_auth::canonical_json_error_name(fixture.error)
                      << " at " << fixture.error_offset << '\n';
            return 1;
        }
        test_runtime_and_canonical_json(*fixture.value);
        test_base64_and_sequence_contract(*fixture.value);
        test_hash_hmac_hkdf(*fixture.value);
        test_asymmetric_and_argon2(*fixture.value);
        test_secure_envelopes(*fixture.value);
        test_low_level_aead_and_secret_hardening(*fixture.value);
    } catch (const std::exception& error) {
        std::cerr << "unexpected test exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " service auth crypto test(s) failed\n";
        return 1;
    }
    std::cout << "service auth crypto tests passed\n";
    return 0;
}
