#include "service/auth/Crypto.h"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <limits>
#include <new>

namespace baas::service::auth {
namespace {

[[nodiscard]] const unsigned char* raw_const(
    const std::span<const std::byte> bytes) noexcept
{
    return reinterpret_cast<const unsigned char*>(bytes.data());
}

[[nodiscard]] unsigned char* raw_mutable(
    const std::span<std::byte> bytes) noexcept
{
    return reinterpret_cast<unsigned char*>(bytes.data());
}

[[nodiscard]] bool initialize_sodium() noexcept
{
    static const bool initialized = sodium_init() >= 0;
    return initialized;
}

[[nodiscard]] PublicBytesResult public_error(const CryptoError error)
{
    return {std::nullopt, error};
}

[[nodiscard]] SecretBytesResult secret_error(const CryptoError error)
{
    return {std::nullopt, error};
}

[[nodiscard]] StringResult string_error(const CryptoError error)
{
    return {std::nullopt, error};
}

[[nodiscard]] bool valid_base64_alphabet(const std::string_view input) noexcept
{
    bool padding_started = false;
    std::size_t padding = 0;
    for (const auto value : input) {
        if (value == '=') {
            padding_started = true;
            if (++padding > 2) return false;
            continue;
        }
        if (padding_started) return false;
        const bool valid = (value >= 'A' && value <= 'Z')
            || (value >= 'a' && value <= 'z')
            || (value >= '0' && value <= '9') || value == '-' || value == '_';
        if (!valid) return false;
    }
    return true;
}

[[nodiscard]] bool hmac_into(
    const std::span<const std::byte> key,
    const std::span<const std::byte> first,
    const std::span<const std::byte> second,
    const std::span<const std::byte> third,
    const std::span<std::byte> output) noexcept
{
    if (output.size() != hmac_sha256_bytes) return false;
    crypto_auth_hmacsha256_state state{};
    if (crypto_auth_hmacsha256_init(&state, raw_const(key), key.size()) != 0) {
        sodium_memzero(&state, sizeof(state));
        return false;
    }
    const bool update_failed =
        (!first.empty()
         && crypto_auth_hmacsha256_update(
                &state, raw_const(first), first.size()) != 0)
        || (!second.empty()
            && crypto_auth_hmacsha256_update(
                   &state, raw_const(second), second.size()) != 0)
        || (!third.empty()
            && crypto_auth_hmacsha256_update(
                   &state, raw_const(third), third.size()) != 0);
    if (update_failed
        || crypto_auth_hmacsha256_final(&state, raw_mutable(output)) != 0) {
        sodium_memzero(&state, sizeof(state));
        return false;
    }
    sodium_memzero(&state, sizeof(state));
    return true;
}

}  // namespace

std::string_view crypto_error_name(const CryptoError error) noexcept
{
    using enum CryptoError;
    switch (error) {
        case none: return "none";
        case initialization_failed: return "initialization_failed";
        case invalid_input: return "invalid_input";
        case invalid_length: return "invalid_length";
        case invalid_base64: return "invalid_base64";
        case noncanonical_base64: return "noncanonical_base64";
        case derivation_failed: return "derivation_failed";
        case verification_failed: return "verification_failed";
        case authentication_failed: return "authentication_failed";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

bool sodium_runtime_ready() noexcept
{
    return initialize_sodium();
}

SecretBuffer::SecretBuffer(const std::size_t size) : bytes_(size) {}

SecretBuffer::SecretBuffer(const std::span<const std::byte> bytes)
    : bytes_(bytes.begin(), bytes.end())
{}

SecretBuffer::~SecretBuffer()
{
    clear();
}

SecretBuffer::SecretBuffer(SecretBuffer&& other) noexcept
    : bytes_(std::move(other.bytes_))
{
    other.clear();
}

SecretBuffer& SecretBuffer::operator=(SecretBuffer&& other) noexcept
{
    if (this == &other) return *this;
    clear();
    bytes_ = std::move(other.bytes_);
    other.clear();
    return *this;
}

std::span<const std::byte> SecretBuffer::bytes() const noexcept
{
    return bytes_;
}

std::span<std::byte> SecretBuffer::mutable_bytes() noexcept
{
    return bytes_;
}

void SecretBuffer::clear() noexcept
{
    if (!bytes_.empty()) sodium_memzero(bytes_.data(), bytes_.size());
    bytes_.clear();
}

StringResult encode_base64url_padded(const std::span<const std::byte> input)
{
    if (!initialize_sodium()) return string_error(CryptoError::initialization_failed);
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return string_error(CryptoError::invalid_length);
    try {
        const auto capacity = sodium_base64_ENCODED_LEN(
            input.size(), sodium_base64_VARIANT_URLSAFE);
        std::string encoded(capacity, '\0');
        sodium_bin2base64(
            encoded.data(), encoded.size(), raw_const(input), input.size(),
            sodium_base64_VARIANT_URLSAFE);
        encoded.resize(std::char_traits<char>::length(encoded.c_str()));
        return {std::move(encoded), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return string_error(CryptoError::resource_exhausted);
    }
}

PublicBytesResult decode_base64url_canonical(
    const std::string_view input, const std::optional<std::size_t> exact_size)
{
    if (!initialize_sodium()) return public_error(CryptoError::initialization_failed);
    if (input.size() % 4 != 0 || !valid_base64_alphabet(input))
        return public_error(CryptoError::invalid_base64);
    const auto padding = input.empty() ? 0U
        : input.back() == '=' ? (input.size() >= 2 && input[input.size() - 2] == '=' ? 2U : 1U)
                              : 0U;
    if (input.size() / 4 > std::numeric_limits<std::size_t>::max() / 3)
        return public_error(CryptoError::invalid_length);
    const auto decoded_capacity = (input.size() / 4) * 3 - padding;
    if (exact_size && decoded_capacity != *exact_size)
        return public_error(CryptoError::invalid_length);
    try {
        PublicBytes decoded(decoded_capacity);
        std::size_t decoded_size = 0;
        const char* end = nullptr;
        if (sodium_base642bin(
                raw_mutable(decoded), decoded.size(), input.data(), input.size(), nullptr,
                &decoded_size, &end, sodium_base64_VARIANT_URLSAFE) != 0
            || end != input.data() + input.size() || decoded_size != decoded_capacity) {
            return public_error(CryptoError::invalid_base64);
        }
        const auto canonical = encode_base64url_padded(decoded);
        if (!canonical) return public_error(canonical.error);
        if (*canonical.value != input)
            return public_error(CryptoError::noncanonical_base64);
        return {std::move(decoded), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return public_error(CryptoError::resource_exhausted);
    }
}

PublicBytesResult sha256(const std::span<const std::byte> input)
{
    if (!initialize_sodium()) return public_error(CryptoError::initialization_failed);
    try {
        PublicBytes output(sha256_bytes);
        if (crypto_hash_sha256(raw_mutable(output), raw_const(input), input.size()) != 0)
            return public_error(CryptoError::derivation_failed);
        return {std::move(output), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return public_error(CryptoError::resource_exhausted);
    }
}

PublicBytesResult hmac_sha256(
    const std::span<const std::byte> key, const std::span<const std::byte> input)
{
    if (!initialize_sodium()) return public_error(CryptoError::initialization_failed);
    try {
        PublicBytes output(hmac_sha256_bytes);
        if (!hmac_into(key, input, {}, {}, output))
            return public_error(CryptoError::derivation_failed);
        return {std::move(output), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return public_error(CryptoError::resource_exhausted);
    }
}

SecretBytesResult hkdf_sha256(
    const std::span<const std::byte> ikm,
    const std::span<const std::byte> salt,
    const std::span<const std::byte> info,
    const std::size_t output_bytes)
{
    if (!initialize_sodium()) return secret_error(CryptoError::initialization_failed);
    if (output_bytes == 0 || output_bytes > 255U * hmac_sha256_bytes)
        return secret_error(CryptoError::invalid_length);
    try {
        // Allocate before deriving the PRK so an allocation failure cannot
        // unwind past live key material on the stack.
        SecretBuffer output{output_bytes};
        std::array<std::byte, hmac_sha256_bytes> zero_salt{};
        std::array<std::byte, hmac_sha256_bytes> prk{};
        std::array<std::byte, hmac_sha256_bytes> previous{};
        const auto effective_salt = salt.empty()
            ? std::span<const std::byte>{zero_salt}
            : salt;
        if (!hmac_into(effective_salt, ikm, {}, {}, prk)) {
            sodium_memzero(prk.data(), prk.size());
            return secret_error(CryptoError::derivation_failed);
        }
        std::size_t written = 0;
        std::size_t previous_size = 0;
        for (std::uint16_t counter = 1; written < output_bytes; ++counter) {
            const auto counter_byte = static_cast<std::byte>(counter);
            if (!hmac_into(
                    prk,
                    std::span<const std::byte>{previous}.first(previous_size),
                    info,
                    std::span<const std::byte>{&counter_byte, 1},
                    previous)) {
                sodium_memzero(prk.data(), prk.size());
                sodium_memzero(previous.data(), previous.size());
                return secret_error(CryptoError::derivation_failed);
            }
            previous_size = previous.size();
            const auto amount = std::min(previous.size(), output_bytes - written);
            std::copy_n(previous.begin(), amount, output.mutable_bytes().begin() + written);
            written += amount;
        }
        sodium_memzero(prk.data(), prk.size());
        sodium_memzero(previous.data(), previous.size());
        return {std::move(output), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return secret_error(CryptoError::resource_exhausted);
    }
}

PublicBytesResult x25519_public_key(const std::span<const std::byte> private_key)
{
    if (!initialize_sodium()) return public_error(CryptoError::initialization_failed);
    if (private_key.size() != x25519_key_bytes)
        return public_error(CryptoError::invalid_length);
    try {
        PublicBytes output(x25519_key_bytes);
        if (crypto_scalarmult_curve25519_base(
                raw_mutable(output), raw_const(private_key)) != 0)
            return public_error(CryptoError::derivation_failed);
        return {std::move(output), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return public_error(CryptoError::resource_exhausted);
    }
}

SecretBytesResult x25519_shared_secret(
    const std::span<const std::byte> private_key,
    const std::span<const std::byte> peer_public_key)
{
    if (!initialize_sodium()) return secret_error(CryptoError::initialization_failed);
    if (private_key.size() != x25519_key_bytes || peer_public_key.size() != x25519_key_bytes)
        return secret_error(CryptoError::invalid_length);
    try {
        SecretBuffer output{x25519_key_bytes};
        if (crypto_scalarmult_curve25519(
                raw_mutable(output.mutable_bytes()), raw_const(private_key),
                raw_const(peer_public_key)) != 0) {
            return secret_error(CryptoError::verification_failed);
        }
        return {std::move(output), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return secret_error(CryptoError::resource_exhausted);
    }
}

PublicBytesResult ed25519_public_key_from_seed(const std::span<const std::byte> seed)
{
    if (!initialize_sodium()) return public_error(CryptoError::initialization_failed);
    if (seed.size() != ed25519_seed_bytes) return public_error(CryptoError::invalid_length);
    try {
        PublicBytes public_key(ed25519_public_key_bytes);
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret_key{};
        const auto result = crypto_sign_seed_keypair(
            raw_mutable(public_key), secret_key.data(), raw_const(seed));
        sodium_memzero(secret_key.data(), secret_key.size());
        if (result != 0) return public_error(CryptoError::derivation_failed);
        return {std::move(public_key), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return public_error(CryptoError::resource_exhausted);
    }
}

PublicBytesResult ed25519_sign(
    const std::span<const std::byte> seed, const std::span<const std::byte> message)
{
    if (!initialize_sodium()) return public_error(CryptoError::initialization_failed);
    if (seed.size() != ed25519_seed_bytes) return public_error(CryptoError::invalid_length);
    try {
        std::array<unsigned char, crypto_sign_PUBLICKEYBYTES> public_key{};
        std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret_key{};
        PublicBytes signature(ed25519_signature_bytes);
        if (crypto_sign_seed_keypair(
                public_key.data(), secret_key.data(), raw_const(seed)) != 0) {
            sodium_memzero(secret_key.data(), secret_key.size());
            return public_error(CryptoError::derivation_failed);
        }
        unsigned long long signature_size = 0;
        const auto result = crypto_sign_detached(
            raw_mutable(signature), &signature_size, raw_const(message),
            message.size(), secret_key.data());
        sodium_memzero(secret_key.data(), secret_key.size());
        if (result != 0 || signature_size != ed25519_signature_bytes)
            return public_error(CryptoError::derivation_failed);
        return {std::move(signature), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return public_error(CryptoError::resource_exhausted);
    }
}

CryptoError ed25519_verify(
    const std::span<const std::byte> public_key,
    const std::span<const std::byte> message,
    const std::span<const std::byte> signature) noexcept
{
    if (!initialize_sodium()) return CryptoError::initialization_failed;
    if (public_key.size() != ed25519_public_key_bytes
        || signature.size() != ed25519_signature_bytes) {
        return CryptoError::invalid_length;
    }
    return crypto_sign_verify_detached(
               raw_const(signature), raw_const(message), message.size(),
               raw_const(public_key)) == 0
        ? CryptoError::none : CryptoError::verification_failed;
}

SecretBytesResult argon2id_v1(
    const std::string_view password, const std::span<const std::byte> salt)
{
    if (!initialize_sodium()) return secret_error(CryptoError::initialization_failed);
    if (salt.size() != argon2id_salt_bytes) return secret_error(CryptoError::invalid_length);
    try {
        SecretBuffer output{argon2id_output_bytes};
        if (crypto_pwhash(
                raw_mutable(output.mutable_bytes()), output.size(), password.data(),
                password.size(), raw_const(salt), argon2id_v1_opslimit,
                argon2id_v1_memlimit,
                crypto_pwhash_ALG_ARGON2ID13) != 0) {
            // With the fixed, valid v1 parameters libsodium reports failure
            // when the requested memory cannot be allocated.
            return secret_error(CryptoError::resource_exhausted);
        }
        return {std::move(output), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return secret_error(CryptoError::resource_exhausted);
    }
}

PublicBytesResult chacha20poly1305_ietf_encrypt(
    const std::span<const std::byte> key,
    const std::span<const std::byte> nonce,
    const std::span<const std::byte> plaintext,
    const std::span<const std::byte> aad)
{
    if (!initialize_sodium()) return public_error(CryptoError::initialization_failed);
    if (key.size() != chacha20poly1305_ietf_key_bytes
        || nonce.size() != chacha20poly1305_ietf_nonce_bytes) {
        return public_error(CryptoError::invalid_length);
    }
    if (plaintext.size() > std::numeric_limits<std::size_t>::max()
            - chacha20poly1305_ietf_tag_bytes) {
        return public_error(CryptoError::invalid_length);
    }
    try {
        PublicBytes ciphertext(plaintext.size() + chacha20poly1305_ietf_tag_bytes);
        unsigned long long ciphertext_size = 0;
        if (crypto_aead_chacha20poly1305_ietf_encrypt(
                raw_mutable(ciphertext), &ciphertext_size, raw_const(plaintext),
                plaintext.size(), raw_const(aad), aad.size(), nullptr,
                raw_const(nonce), raw_const(key)) != 0
            || ciphertext_size != ciphertext.size()) {
            return public_error(CryptoError::derivation_failed);
        }
        return {std::move(ciphertext), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return public_error(CryptoError::resource_exhausted);
    }
}

SecretBytesResult chacha20poly1305_ietf_decrypt(
    const std::span<const std::byte> key,
    const std::span<const std::byte> nonce,
    const std::span<const std::byte> ciphertext,
    const std::span<const std::byte> aad)
{
    if (!initialize_sodium()) return secret_error(CryptoError::initialization_failed);
    if (key.size() != chacha20poly1305_ietf_key_bytes
        || nonce.size() != chacha20poly1305_ietf_nonce_bytes) {
        return secret_error(CryptoError::invalid_length);
    }
    if (ciphertext.size() < chacha20poly1305_ietf_tag_bytes)
        return secret_error(CryptoError::invalid_length);
    try {
        SecretBuffer plaintext{ciphertext.size() - chacha20poly1305_ietf_tag_bytes};
        unsigned long long plaintext_size = 0;
        if (crypto_aead_chacha20poly1305_ietf_decrypt(
                raw_mutable(plaintext.mutable_bytes()), &plaintext_size, nullptr,
                raw_const(ciphertext), ciphertext.size(), raw_const(aad), aad.size(),
                raw_const(nonce), raw_const(key)) != 0) {
            return secret_error(CryptoError::authentication_failed);
        }
        if (plaintext_size != plaintext.size())
            return secret_error(CryptoError::authentication_failed);
        return {std::move(plaintext), CryptoError::none};
    } catch (const std::bad_alloc&) {
        return secret_error(CryptoError::resource_exhausted);
    }
}

bool constant_time_equal(
    const std::span<const std::byte> left,
    const std::span<const std::byte> right) noexcept
{
    if (left.size() != right.size()) return false;
    if (left.empty()) return true;
    return sodium_memcmp(left.data(), right.data(), left.size()) == 0;
}

void secure_zero(const std::span<std::byte> bytes) noexcept
{
    if (!bytes.empty()) sodium_memzero(bytes.data(), bytes.size());
}

}  // namespace baas::service::auth
