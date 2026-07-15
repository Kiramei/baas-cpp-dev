#pragma once

#include "service/auth/CanonicalJson.h"
#include "service/auth/Crypto.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace baas::service::auth {

enum class SecureEnvelopeError : std::uint8_t {
    none,
    crypto_initialization_failed,
    invalid_key,
    invalid_envelope,
    invalid_plaintext,
    sequence_mismatch,
    sequence_exhausted,
    authentication_failed,
    resource_exhausted,
};

[[nodiscard]] std::string_view secure_envelope_error_name(
    SecureEnvelopeError error) noexcept;

struct SecureEnvelopeEncodeResult {
    std::string envelope;
    SecureEnvelopeError error{SecureEnvelopeError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SecureEnvelopeError::none;
    }
};

struct SecureEnvelopeDecodeResult {
    std::optional<CanonicalJsonValue> plaintext;
    SecureEnvelopeError error{SecureEnvelopeError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SecureEnvelopeError::none && plaintext.has_value();
    }
};

using ControlSequenceNonce =
    std::array<std::byte, chacha20poly1305_ietf_nonce_bytes>;

[[nodiscard]] ControlSequenceNonce control_sequence_nonce(
    std::uint64_t sequence) noexcept;
[[nodiscard]] StringResult control_sequence_aad(std::uint64_t sequence);

// Owns distinct directional keys and independent monotonic send/receive
// sequences. A sequence advances only after a complete authenticated operation
// succeeds. Construction rejects equal TX/RX keys so the two directions cannot
// reuse the same key/nonce pair. The mutable sequence state is intentionally
// single-owner and must be called only from its serialized session executor.
class SecureEnvelopeCipher final {
public:
    [[nodiscard]] static CryptoResult<SecureEnvelopeCipher> create(
        std::span<const std::byte> transmit_key,
        std::span<const std::byte> receive_key,
        std::uint64_t next_send_sequence = 0,
        std::uint64_t next_receive_sequence = 0);

    SecureEnvelopeCipher(const SecureEnvelopeCipher&) = delete;
    SecureEnvelopeCipher& operator=(const SecureEnvelopeCipher&) = delete;
    SecureEnvelopeCipher(SecureEnvelopeCipher&&) noexcept = default;
    SecureEnvelopeCipher& operator=(SecureEnvelopeCipher&&) noexcept = default;

    [[nodiscard]] SecureEnvelopeEncodeResult encrypt(
        const CanonicalJsonValue& plaintext);
    [[nodiscard]] SecureEnvelopeDecodeResult decrypt(
        std::string_view envelope);

    [[nodiscard]] std::uint64_t next_send_sequence() const noexcept
    {
        return next_send_sequence_;
    }
    [[nodiscard]] std::uint64_t next_receive_sequence() const noexcept
    {
        return next_receive_sequence_;
    }

private:
    SecureEnvelopeCipher(
        SecretBuffer transmit_key,
        SecretBuffer receive_key,
        std::uint64_t next_send_sequence,
        std::uint64_t next_receive_sequence) noexcept;

    SecretBuffer transmit_key_;
    SecretBuffer receive_key_;
    std::uint64_t next_send_sequence_{};
    std::uint64_t next_receive_sequence_{};
};

}  // namespace baas::service::auth
