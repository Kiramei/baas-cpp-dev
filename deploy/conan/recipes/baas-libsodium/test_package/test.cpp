#include <sodium.h>

#include <array>
#include <cstdlib>
#include <string_view>

namespace {

bool exercise_required_primitives()
{
    static_assert(crypto_pwhash_ALG_ARGON2ID13 != 0);
    static_assert(crypto_scalarmult_curve25519_BYTES == 32);
    static_assert(crypto_sign_ed25519_BYTES == 64);
    static_assert(crypto_aead_chacha20poly1305_ietf_KEYBYTES == 32);
    static_assert(crypto_secretstream_xchacha20poly1305_HEADERBYTES == 24);

    std::array<unsigned char, 32> password_hash{};
    std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
    if (crypto_pwhash_argon2id(
            password_hash.data(),
            password_hash.size(),
            "password",
            8,
            salt.data(),
            crypto_pwhash_argon2id_OPSLIMIT_MIN,
            crypto_pwhash_argon2id_MEMLIMIT_MIN,
            crypto_pwhash_argon2id_ALG_ARGON2ID13) != 0) {
        return false;
    }

    std::array<unsigned char, crypto_scalarmult_curve25519_SCALARBYTES> scalar{};
    std::array<unsigned char, crypto_scalarmult_curve25519_BYTES> public_key{};
    scalar.front() = 1;
    if (crypto_scalarmult_curve25519_base(public_key.data(), scalar.data()) != 0) {
        return false;
    }

    std::array<unsigned char, crypto_sign_ed25519_SEEDBYTES> signing_seed{};
    std::array<unsigned char, crypto_sign_ed25519_PUBLICKEYBYTES> signing_public{};
    std::array<unsigned char, crypto_sign_ed25519_SECRETKEYBYTES> signing_secret{};
    std::array<unsigned char, crypto_sign_ed25519_BYTES> signature{};
    unsigned long long signature_size = 0;
    constexpr std::string_view message = "baas";
    crypto_sign_ed25519_seed_keypair(
        signing_public.data(), signing_secret.data(), signing_seed.data());
    if (crypto_sign_ed25519_detached(
            signature.data(),
            &signature_size,
            reinterpret_cast<const unsigned char*>(message.data()),
            message.size(),
            signing_secret.data()) != 0 ||
        signature_size != signature.size() ||
        crypto_sign_ed25519_verify_detached(
            signature.data(),
            reinterpret_cast<const unsigned char*>(message.data()),
            message.size(),
            signing_public.data()) != 0) {
        return false;
    }

    std::array<unsigned char, crypto_aead_chacha20poly1305_ietf_KEYBYTES> aead_key{};
    std::array<unsigned char, crypto_aead_chacha20poly1305_ietf_NPUBBYTES> nonce{};
    std::array<unsigned char, 4 + crypto_aead_chacha20poly1305_ietf_ABYTES> encrypted{};
    std::array<unsigned char, 4> decrypted{};
    unsigned long long encrypted_size = 0;
    unsigned long long decrypted_size = 0;
    if (crypto_aead_chacha20poly1305_ietf_encrypt(
            encrypted.data(),
            &encrypted_size,
            reinterpret_cast<const unsigned char*>(message.data()),
            message.size(),
            nullptr,
            0,
            nullptr,
            nonce.data(),
            aead_key.data()) != 0 ||
        crypto_aead_chacha20poly1305_ietf_decrypt(
            decrypted.data(),
            &decrypted_size,
            nullptr,
            encrypted.data(),
            encrypted_size,
            nullptr,
            0,
            nonce.data(),
            aead_key.data()) != 0 ||
        decrypted_size != message.size() ||
        sodium_memcmp(decrypted.data(), message.data(), message.size()) != 0) {
        return false;
    }

    crypto_secretstream_xchacha20poly1305_state push_state{};
    crypto_secretstream_xchacha20poly1305_state pull_state{};
    std::array<unsigned char, crypto_secretstream_xchacha20poly1305_KEYBYTES> stream_key{};
    std::array<unsigned char, crypto_secretstream_xchacha20poly1305_HEADERBYTES> stream_header{};
    std::array<unsigned char, 4 + crypto_secretstream_xchacha20poly1305_ABYTES> stream_ciphertext{};
    std::array<unsigned char, 4> stream_plaintext{};
    unsigned long long stream_ciphertext_size = 0;
    unsigned long long stream_plaintext_size = 0;
    unsigned char stream_tag = 0;
    if (crypto_secretstream_xchacha20poly1305_init_push(
            &push_state, stream_header.data(), stream_key.data()) != 0 ||
        crypto_secretstream_xchacha20poly1305_push(
            &push_state,
            stream_ciphertext.data(),
            &stream_ciphertext_size,
            reinterpret_cast<const unsigned char*>(message.data()),
            message.size(),
            nullptr,
            0,
            crypto_secretstream_xchacha20poly1305_TAG_FINAL) != 0 ||
        crypto_secretstream_xchacha20poly1305_init_pull(
            &pull_state, stream_header.data(), stream_key.data()) != 0 ||
        crypto_secretstream_xchacha20poly1305_pull(
            &pull_state,
            stream_plaintext.data(),
            &stream_plaintext_size,
            &stream_tag,
            stream_ciphertext.data(),
            stream_ciphertext_size,
            nullptr,
            0) != 0 ||
        stream_plaintext_size != message.size() ||
        stream_tag != crypto_secretstream_xchacha20poly1305_TAG_FINAL ||
        sodium_memcmp(stream_plaintext.data(), message.data(), message.size()) != 0) {
        return false;
    }

    sodium_memzero(password_hash.data(), password_hash.size());
    sodium_memzero(signing_secret.data(), signing_secret.size());
    return true;
}

}  // namespace

int main()
{
    if (sodium_init() < 0) return EXIT_FAILURE;
    if (!exercise_required_primitives()) return EXIT_FAILURE;

    constexpr std::string_view input = "abc";
    constexpr std::array<unsigned char, crypto_hash_sha256_BYTES> expected{
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    if (crypto_hash_sha256(
            digest.data(),
            reinterpret_cast<const unsigned char*>(input.data()),
            input.size()) != 0) {
        return EXIT_FAILURE;
    }
    if (sodium_memcmp(digest.data(), expected.data(), digest.size()) != 0) {
        return EXIT_FAILURE;
    }
    sodium_memzero(digest.data(), digest.size());
    return EXIT_SUCCESS;
}
