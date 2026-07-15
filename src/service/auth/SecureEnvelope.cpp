#include "service/auth/SecureEnvelope.h"

#include <array>
#include <charconv>
#include <limits>
#include <new>

namespace baas::service::auth {
namespace {

[[nodiscard]] std::span<const std::byte> bytes(const std::string_view value) noexcept
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

[[nodiscard]] std::string_view text(const std::span<const std::byte> value) noexcept
{
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] SecureEnvelopeError map_crypto_error(const CryptoError error) noexcept
{
    switch (error) {
        case CryptoError::none: return SecureEnvelopeError::none;
        case CryptoError::initialization_failed:
            return SecureEnvelopeError::crypto_initialization_failed;
        case CryptoError::invalid_length:
        case CryptoError::invalid_input:
        case CryptoError::invalid_base64:
        case CryptoError::noncanonical_base64:
            return SecureEnvelopeError::invalid_envelope;
        case CryptoError::verification_failed:
        case CryptoError::authentication_failed:
            return SecureEnvelopeError::authentication_failed;
        case CryptoError::resource_exhausted:
            return SecureEnvelopeError::resource_exhausted;
        case CryptoError::derivation_failed:
            return SecureEnvelopeError::authentication_failed;
    }
    return SecureEnvelopeError::authentication_failed;
}

[[nodiscard]] std::string sequence_text(const std::uint64_t sequence)
{
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), sequence);
    return {buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())};
}

[[nodiscard]] SecureEnvelopeEncodeResult encode_error(
    const SecureEnvelopeError error)
{
    return {{}, error};
}

[[nodiscard]] SecureEnvelopeDecodeResult decode_error(
    const SecureEnvelopeError error)
{
    return {std::nullopt, error};
}

}  // namespace

std::string_view secure_envelope_error_name(const SecureEnvelopeError error) noexcept
{
    using enum SecureEnvelopeError;
    switch (error) {
        case none: return "none";
        case crypto_initialization_failed: return "crypto_initialization_failed";
        case invalid_key: return "invalid_key";
        case invalid_envelope: return "invalid_envelope";
        case invalid_plaintext: return "invalid_plaintext";
        case sequence_mismatch: return "sequence_mismatch";
        case sequence_exhausted: return "sequence_exhausted";
        case authentication_failed: return "authentication_failed";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

ControlSequenceNonce control_sequence_nonce(const std::uint64_t sequence) noexcept
{
    ControlSequenceNonce nonce{};
    for (std::size_t offset = 0; offset < sizeof(sequence); ++offset) {
        nonce[nonce.size() - 1 - offset] = static_cast<std::byte>(
            (sequence >> (offset * 8U)) & 0xFFU);
    }
    return nonce;
}

StringResult control_sequence_aad(const std::uint64_t sequence)
{
    if (sequence > static_cast<std::uint64_t>(maximum_safe_json_integer))
        return {std::nullopt, CryptoError::invalid_input};
    try {
        return {
            std::string{R"({"seq":)"} + sequence_text(sequence)
                + R"(,"type":"secure"})",
            CryptoError::none};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, CryptoError::resource_exhausted};
    }
}

SecureEnvelopeCipher::SecureEnvelopeCipher(
    SecretBuffer transmit_key,
    SecretBuffer receive_key,
    const std::uint64_t next_send_sequence,
    const std::uint64_t next_receive_sequence) noexcept
    : transmit_key_(std::move(transmit_key)),
      receive_key_(std::move(receive_key)),
      next_send_sequence_(next_send_sequence),
      next_receive_sequence_(next_receive_sequence)
{}

CryptoResult<SecureEnvelopeCipher> SecureEnvelopeCipher::create(
    const std::span<const std::byte> transmit_key,
    const std::span<const std::byte> receive_key,
    const std::uint64_t next_send_sequence,
    const std::uint64_t next_receive_sequence)
{
    if (!sodium_runtime_ready())
        return {std::nullopt, CryptoError::initialization_failed};
    if (transmit_key.size() != chacha20poly1305_ietf_key_bytes
        || receive_key.size() != chacha20poly1305_ietf_key_bytes) {
        return {std::nullopt, CryptoError::invalid_length};
    }
    if (constant_time_equal(transmit_key, receive_key))
        return {std::nullopt, CryptoError::invalid_input};
    if (next_send_sequence > static_cast<std::uint64_t>(maximum_safe_json_integer)
        || next_receive_sequence > static_cast<std::uint64_t>(maximum_safe_json_integer)) {
        return {std::nullopt, CryptoError::invalid_input};
    }
    try {
        SecureEnvelopeCipher cipher{
            SecretBuffer{transmit_key}, SecretBuffer{receive_key},
            next_send_sequence, next_receive_sequence};
        return {
            std::optional<SecureEnvelopeCipher>{std::move(cipher)},
            CryptoError::none};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, CryptoError::resource_exhausted};
    }
}

SecureEnvelopeEncodeResult SecureEnvelopeCipher::encrypt(
    const CanonicalJsonValue& plaintext)
{
    if (next_send_sequence_ > static_cast<std::uint64_t>(maximum_safe_json_integer))
        return encode_error(SecureEnvelopeError::sequence_exhausted);
    try {
        const auto encoded_plaintext = encode_canonical_json_value(plaintext);
        if (!encoded_plaintext)
            return encode_error(SecureEnvelopeError::invalid_plaintext);
        const auto nonce = control_sequence_nonce(next_send_sequence_);
        const auto aad = control_sequence_aad(next_send_sequence_);
        if (!aad) return encode_error(map_crypto_error(aad.error));
        const auto ciphertext = chacha20poly1305_ietf_encrypt(
            transmit_key_.bytes(), nonce, bytes(encoded_plaintext.text),
            bytes(*aad.value));
        if (!ciphertext) return encode_error(map_crypto_error(ciphertext.error));
        const auto encoded_ciphertext = encode_base64url_padded(*ciphertext.value);
        if (!encoded_ciphertext)
            return encode_error(map_crypto_error(encoded_ciphertext.error));
        auto envelope = std::string{R"({"ciphertext":")"}
            + *encoded_ciphertext.value + R"(","seq":)"
            + sequence_text(next_send_sequence_) + R"(,"type":"secure"})";
        ++next_send_sequence_;
        return {std::move(envelope), SecureEnvelopeError::none};
    } catch (const std::bad_alloc&) {
        return encode_error(SecureEnvelopeError::resource_exhausted);
    }
}

SecureEnvelopeDecodeResult SecureEnvelopeCipher::decrypt(
    const std::string_view envelope)
{
    if (next_receive_sequence_ > static_cast<std::uint64_t>(maximum_safe_json_integer))
        return decode_error(SecureEnvelopeError::sequence_exhausted);
    try {
        const auto parsed = parse_canonical_json_value(envelope);
        if (!parsed) return decode_error(SecureEnvelopeError::invalid_envelope);
        const auto* object = parsed.value->as_object();
        if (object == nullptr || object->size() != 3)
            return decode_error(SecureEnvelopeError::invalid_envelope);
        const auto* type_value = parsed.value->find("type");
        const auto* sequence_value = parsed.value->find("seq");
        const auto* ciphertext_value = parsed.value->find("ciphertext");
        const auto* type = type_value == nullptr ? nullptr : type_value->as_string();
        const auto* sequence = sequence_value == nullptr ? nullptr : sequence_value->as_integer();
        const auto* ciphertext_text = ciphertext_value == nullptr
            ? nullptr : ciphertext_value->as_string();
        if (type == nullptr || *type != "secure" || sequence == nullptr
            || *sequence < 0 || ciphertext_text == nullptr) {
            return decode_error(SecureEnvelopeError::invalid_envelope);
        }
        if (static_cast<std::uint64_t>(*sequence) != next_receive_sequence_)
            return decode_error(SecureEnvelopeError::sequence_mismatch);
        const auto ciphertext = decode_base64url_canonical(*ciphertext_text);
        if (!ciphertext) return decode_error(map_crypto_error(ciphertext.error));
        const auto nonce = control_sequence_nonce(next_receive_sequence_);
        const auto aad = control_sequence_aad(next_receive_sequence_);
        if (!aad) return decode_error(map_crypto_error(aad.error));
        const auto plaintext = chacha20poly1305_ietf_decrypt(
            receive_key_.bytes(), nonce, *ciphertext.value, bytes(*aad.value));
        if (!plaintext) return decode_error(map_crypto_error(plaintext.error));
        const auto plaintext_text = text(plaintext.value->bytes());
        auto parsed_plaintext = parse_canonical_json_value(plaintext_text);
        if (!parsed_plaintext)
            return decode_error(SecureEnvelopeError::invalid_plaintext);
        const auto canonical = encode_canonical_json_value(*parsed_plaintext.value);
        if (!canonical || canonical.text != plaintext_text)
            return decode_error(SecureEnvelopeError::invalid_plaintext);
        ++next_receive_sequence_;
        return {std::move(parsed_plaintext.value), SecureEnvelopeError::none};
    } catch (const std::bad_alloc&) {
        return decode_error(SecureEnvelopeError::resource_exhausted);
    }
}

}  // namespace baas::service::auth
