#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace baas::service::auth {

inline constexpr std::size_t sha256_bytes = 32;
inline constexpr std::size_t hmac_sha256_bytes = 32;
inline constexpr std::size_t x25519_key_bytes = 32;
inline constexpr std::size_t ed25519_seed_bytes = 32;
inline constexpr std::size_t ed25519_public_key_bytes = 32;
inline constexpr std::size_t ed25519_signature_bytes = 64;
inline constexpr std::size_t argon2id_salt_bytes = 16;
inline constexpr std::size_t argon2id_output_bytes = 32;
inline constexpr std::size_t chacha20poly1305_ietf_key_bytes = 32;
inline constexpr std::size_t chacha20poly1305_ietf_nonce_bytes = 12;
inline constexpr std::size_t chacha20poly1305_ietf_tag_bytes = 16;
inline constexpr std::uint64_t argon2id_v1_opslimit = 3;
inline constexpr std::size_t argon2id_v1_memlimit = 67'108'864;

using PublicBytes = std::vector<std::byte>;

enum class CryptoError : std::uint8_t {
    none,
    initialization_failed,
    invalid_input,
    invalid_length,
    invalid_base64,
    noncanonical_base64,
    derivation_failed,
    verification_failed,
    authentication_failed,
    resource_exhausted,
};

[[nodiscard]] std::string_view crypto_error_name(CryptoError error) noexcept;
[[nodiscard]] bool sodium_runtime_ready() noexcept;

// Move-only secret storage. Every live byte is overwritten before release;
// callers receive read-only views unless they explicitly request a mutable
// view for a cryptographic output operation.
class SecretBuffer final {
public:
    SecretBuffer() = default;
    explicit SecretBuffer(std::size_t size);
    explicit SecretBuffer(std::span<const std::byte> bytes);
    ~SecretBuffer();

    SecretBuffer(const SecretBuffer&) = delete;
    SecretBuffer& operator=(const SecretBuffer&) = delete;
    SecretBuffer(SecretBuffer&& other) noexcept;
    SecretBuffer& operator=(SecretBuffer&& other) noexcept;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] std::span<std::byte> mutable_bytes() noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }
    void clear() noexcept;

private:
    std::vector<std::byte> bytes_;
};

template <typename T>
struct CryptoResult {
    std::optional<T> value;
    CryptoError error{CryptoError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == CryptoError::none && value.has_value();
    }
};

using PublicBytesResult = CryptoResult<PublicBytes>;
using SecretBytesResult = CryptoResult<SecretBuffer>;
using StringResult = CryptoResult<std::string>;

[[nodiscard]] StringResult encode_base64url_padded(
    std::span<const std::byte> input);
[[nodiscard]] PublicBytesResult decode_base64url_canonical(
    std::string_view input,
    std::optional<std::size_t> exact_size = std::nullopt);

[[nodiscard]] PublicBytesResult sha256(std::span<const std::byte> input);
[[nodiscard]] PublicBytesResult hmac_sha256(
    std::span<const std::byte> key,
    std::span<const std::byte> input);
[[nodiscard]] SecretBytesResult hkdf_sha256(
    std::span<const std::byte> ikm,
    std::span<const std::byte> salt,
    std::span<const std::byte> info,
    std::size_t output_bytes);

[[nodiscard]] PublicBytesResult x25519_public_key(
    std::span<const std::byte> private_key);
[[nodiscard]] SecretBytesResult x25519_shared_secret(
    std::span<const std::byte> private_key,
    std::span<const std::byte> peer_public_key);

[[nodiscard]] PublicBytesResult ed25519_public_key_from_seed(
    std::span<const std::byte> seed);
[[nodiscard]] PublicBytesResult ed25519_sign(
    std::span<const std::byte> seed,
    std::span<const std::byte> message);
[[nodiscard]] CryptoError ed25519_verify(
    std::span<const std::byte> public_key,
    std::span<const std::byte> message,
    std::span<const std::byte> signature) noexcept;

[[nodiscard]] SecretBytesResult argon2id_v1(
    std::string_view password,
    std::span<const std::byte> salt);
[[nodiscard]] SecretBytesResult argon2id_v1(
    std::span<const std::byte> password,
    std::span<const std::byte> salt);

[[nodiscard]] PublicBytesResult chacha20poly1305_ietf_encrypt(
    std::span<const std::byte> key,
    std::span<const std::byte> nonce,
    std::span<const std::byte> plaintext,
    std::span<const std::byte> aad);
[[nodiscard]] SecretBytesResult chacha20poly1305_ietf_decrypt(
    std::span<const std::byte> key,
    std::span<const std::byte> nonce,
    std::span<const std::byte> ciphertext,
    std::span<const std::byte> aad);

[[nodiscard]] bool constant_time_equal(
    std::span<const std::byte> left,
    std::span<const std::byte> right) noexcept;
void secure_zero(std::span<std::byte> bytes) noexcept;

}  // namespace baas::service::auth
