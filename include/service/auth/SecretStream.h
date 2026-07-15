#pragma once

#include "service/auth/Crypto.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace baas::service::auth {

inline constexpr std::size_t secretstream_key_bytes = 32;
inline constexpr std::size_t secretstream_header_bytes = 24;
inline constexpr std::size_t secretstream_overhead_bytes = 17;
inline constexpr std::size_t secretstream_sequence_bytes = 8;

using SecretStreamHeader =
    std::array<std::byte, secretstream_header_bytes>;

// Service protocol v1 deliberately exposes only data and terminal records.
// PUSH and REKEY records have no v1 wire meaning and poison a receiver.
enum class SecretStreamTag : std::uint8_t {
    message,
    final,
};

enum class SecretStreamError : std::uint8_t {
    none,
    initialization_failed,
    invalid_key,
    invalid_header,
    invalid_input,
    message_too_large,
    authentication_failed,
    unexpected_tag,
    sequence_exhausted,
    stream_closed,
    poisoned,
    resource_exhausted,
};

[[nodiscard]] std::string_view secretstream_error_name(
    SecretStreamError error) noexcept;

template <typename T>
struct SecretStreamCreateResult {
    std::optional<T> value;
    SecretStreamError error{SecretStreamError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SecretStreamError::none && value.has_value();
    }
};

struct SecretStreamEncryptResult {
    PublicBytes ciphertext;
    std::uint64_t sequence{};
    SecretStreamTag tag{SecretStreamTag::message};
    SecretStreamError error{SecretStreamError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SecretStreamError::none;
    }
};

struct SecretStreamDecryptResult {
    SecretBuffer plaintext;
    std::uint64_t sequence{};
    SecretStreamTag tag{SecretStreamTag::message};
    SecretStreamError error{SecretStreamError::none};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == SecretStreamError::none;
    }
};

// Owns one transmit direction. AAD is always aad_prefix followed by the
// current uint64 sequence in network byte order. The object is single-owner
// and callers must serialize access from their session executor.
class SecretStreamPush final {
public:
    [[nodiscard]] static SecretStreamCreateResult<SecretStreamPush> create(
        std::span<const std::byte> key,
        std::span<const std::byte> aad_prefix);

    ~SecretStreamPush();

    SecretStreamPush(const SecretStreamPush&) = delete;
    SecretStreamPush& operator=(const SecretStreamPush&) = delete;
    SecretStreamPush(SecretStreamPush&&) noexcept;
    SecretStreamPush& operator=(SecretStreamPush&&) noexcept;

    [[nodiscard]] const SecretStreamHeader& header() const noexcept;
    [[nodiscard]] SecretStreamEncryptResult push(
        std::span<const std::byte> plaintext,
        SecretStreamTag tag = SecretStreamTag::message);

    [[nodiscard]] std::uint64_t next_sequence() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;

private:
    struct Impl;
    explicit SecretStreamPush(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

// Owns one receive direction. Authentication, framing, ordering, replay, and
// tag failures permanently poison the direction; FINAL permanently closes it.
class SecretStreamPull final {
public:
    [[nodiscard]] static SecretStreamCreateResult<SecretStreamPull> create(
        std::span<const std::byte> key,
        std::span<const std::byte> header,
        std::span<const std::byte> aad_prefix);

    ~SecretStreamPull();

    SecretStreamPull(const SecretStreamPull&) = delete;
    SecretStreamPull& operator=(const SecretStreamPull&) = delete;
    SecretStreamPull(SecretStreamPull&&) noexcept;
    SecretStreamPull& operator=(SecretStreamPull&&) noexcept;

    [[nodiscard]] SecretStreamDecryptResult pull(
        std::span<const std::byte> ciphertext);

    [[nodiscard]] std::uint64_t next_sequence() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;

private:
    struct Impl;
    explicit SecretStreamPull(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

#if defined(BAAS_SECRETSTREAM_TEST_HOOKS)
namespace detail {
void fail_next_secretstream_output_allocation_for_test() noexcept;
}
#endif

}  // namespace baas::service::auth
