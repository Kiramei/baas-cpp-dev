#include "service/auth/SecretStream.h"

#include <sodium.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace baas::service::auth {
namespace {

static_assert(secretstream_key_bytes
              == crypto_secretstream_xchacha20poly1305_KEYBYTES);
static_assert(secretstream_header_bytes
              == crypto_secretstream_xchacha20poly1305_HEADERBYTES);
static_assert(secretstream_overhead_bytes
              == crypto_secretstream_xchacha20poly1305_ABYTES);
static_assert(crypto_secretstream_xchacha20poly1305_TAG_MESSAGE == 0U);
static_assert(crypto_secretstream_xchacha20poly1305_TAG_PUSH == 1U);
static_assert(crypto_secretstream_xchacha20poly1305_TAG_REKEY == 2U);
static_assert(crypto_secretstream_xchacha20poly1305_TAG_FINAL == 3U);

enum class StreamPhase : std::uint8_t {
    active,
    finalized,
    exhausted,
    poisoned,
};

[[nodiscard]] const unsigned char* raw_const(
    const std::span<const std::byte> value) noexcept
{
    return reinterpret_cast<const unsigned char*>(value.data());
}

[[nodiscard]] unsigned char* raw_mutable(
    const std::span<std::byte> value) noexcept
{
    return reinterpret_cast<unsigned char*>(value.data());
}

[[nodiscard]] bool valid_prefix_size(const std::size_t size) noexcept
{
    return size <= std::numeric_limits<std::size_t>::max()
            - secretstream_sequence_bytes
        && size <= static_cast<std::size_t>(
            std::numeric_limits<unsigned long long>::max()
            - secretstream_sequence_bytes);
}

void write_sequence(
    const std::span<std::byte> aad, const std::uint64_t sequence) noexcept
{
    for (std::size_t offset = 0; offset < secretstream_sequence_bytes; ++offset) {
        aad[aad.size() - 1U - offset] = static_cast<std::byte>(
            (sequence >> (offset * 8U)) & 0xFFU);
    }
}

[[nodiscard]] SecretStreamError phase_error(const StreamPhase phase) noexcept
{
    switch (phase) {
        case StreamPhase::active: return SecretStreamError::none;
        case StreamPhase::finalized: return SecretStreamError::stream_closed;
        case StreamPhase::exhausted:
            return SecretStreamError::sequence_exhausted;
        case StreamPhase::poisoned: return SecretStreamError::poisoned;
    }
    return SecretStreamError::poisoned;
}

void advance_phase(
    StreamPhase& phase,
    std::uint64_t& sequence,
    const SecretStreamTag tag) noexcept
{
    const bool at_maximum =
        sequence == std::numeric_limits<std::uint64_t>::max();
    if (!at_maximum)
        ++sequence;
    if (tag == SecretStreamTag::final)
        phase = StreamPhase::finalized;
    else if (at_maximum)
        phase = StreamPhase::exhausted;
}

[[nodiscard]] unsigned char native_tag(const SecretStreamTag tag) noexcept
{
    switch (tag) {
        case SecretStreamTag::message:
            return crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
        case SecretStreamTag::final:
            return crypto_secretstream_xchacha20poly1305_TAG_FINAL;
    }
    return 0xFFU;
}

[[nodiscard]] SecretStreamEncryptResult encrypt_error(
    const SecretStreamError error) noexcept
{
    SecretStreamEncryptResult result;
    result.error = error;
    return result;
}

[[nodiscard]] SecretStreamDecryptResult decrypt_error(
    const SecretStreamError error) noexcept
{
    SecretStreamDecryptResult result;
    result.error = error;
    return result;
}

struct StateStorage {
    crypto_secretstream_xchacha20poly1305_state native{};
    PublicBytes aad;
    std::uint64_t sequence{};
    StreamPhase phase{StreamPhase::active};

    explicit StateStorage(const std::span<const std::byte> aad_prefix)
        : aad(aad_prefix.size() + secretstream_sequence_bytes)
    {
        std::copy(aad_prefix.begin(), aad_prefix.end(), aad.begin());
    }

    ~StateStorage()
    {
        sodium_memzero(&native, sizeof(native));
    }

    StateStorage(const StateStorage&) = delete;
    StateStorage& operator=(const StateStorage&) = delete;
};

#if defined(BAAS_SECRETSTREAM_TEST_HOOKS)
std::atomic_bool fail_next_output_allocation{false};

void fail_output_allocation_if_requested()
{
    if (fail_next_output_allocation.exchange(false, std::memory_order_relaxed))
        throw std::bad_alloc{};
}
#else
void fail_output_allocation_if_requested() noexcept {}
#endif

}  // namespace

struct SecretStreamPush::Impl final : StateStorage {
    SecretStreamHeader stream_header{};

    explicit Impl(const std::span<const std::byte> aad_prefix)
        : StateStorage(aad_prefix)
    {}
};

struct SecretStreamPull::Impl final : StateStorage {
    explicit Impl(const std::span<const std::byte> aad_prefix)
        : StateStorage(aad_prefix)
    {}
};

std::string_view secretstream_error_name(const SecretStreamError error) noexcept
{
    using enum SecretStreamError;
    switch (error) {
        case none: return "none";
        case initialization_failed: return "initialization_failed";
        case invalid_key: return "invalid_key";
        case invalid_header: return "invalid_header";
        case invalid_input: return "invalid_input";
        case message_too_large: return "message_too_large";
        case authentication_failed: return "authentication_failed";
        case unexpected_tag: return "unexpected_tag";
        case sequence_exhausted: return "sequence_exhausted";
        case stream_closed: return "stream_closed";
        case poisoned: return "poisoned";
        case resource_exhausted: return "resource_exhausted";
    }
    return "unknown";
}

#if defined(BAAS_SECRETSTREAM_TEST_HOOKS)
namespace detail {

void fail_next_secretstream_output_allocation_for_test() noexcept
{
    fail_next_output_allocation.store(true, std::memory_order_relaxed);
}

}  // namespace detail
#endif

SecretStreamPush::SecretStreamPush(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{}

SecretStreamPush::~SecretStreamPush() = default;
SecretStreamPush::SecretStreamPush(SecretStreamPush&&) noexcept = default;
SecretStreamPush& SecretStreamPush::operator=(SecretStreamPush&&) noexcept = default;

SecretStreamCreateResult<SecretStreamPush> SecretStreamPush::create(
    const std::span<const std::byte> key,
    const std::span<const std::byte> aad_prefix)
{
    if (!sodium_runtime_ready())
        return {std::nullopt, SecretStreamError::initialization_failed};
    if (key.size() != secretstream_key_bytes)
        return {std::nullopt, SecretStreamError::invalid_key};
    if (!valid_prefix_size(aad_prefix.size()))
        return {std::nullopt, SecretStreamError::invalid_input};
    try {
        auto impl = std::make_unique<Impl>(aad_prefix);
        if (crypto_secretstream_xchacha20poly1305_init_push(
                &impl->native,
                raw_mutable(impl->stream_header),
                raw_const(key)) != 0) {
            return {std::nullopt, SecretStreamError::initialization_failed};
        }
        SecretStreamPush stream{std::move(impl)};
        return {
            std::optional<SecretStreamPush>{std::move(stream)},
            SecretStreamError::none};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, SecretStreamError::resource_exhausted};
    } catch (const std::length_error&) {
        return {std::nullopt, SecretStreamError::invalid_input};
    } catch (...) {
        return {std::nullopt, SecretStreamError::resource_exhausted};
    }
}

const SecretStreamHeader& SecretStreamPush::header() const noexcept
{
    static constexpr SecretStreamHeader empty_header{};
    return impl_ == nullptr ? empty_header : impl_->stream_header;
}

SecretStreamEncryptResult SecretStreamPush::push(
    const std::span<const std::byte> plaintext,
    const SecretStreamTag tag)
{
    if (impl_ == nullptr) return encrypt_error(SecretStreamError::poisoned);
    if (impl_->phase != StreamPhase::active)
        return encrypt_error(phase_error(impl_->phase));
    const auto tag_value = native_tag(tag);
    if (tag_value == 0xFFU) {
        impl_->phase = StreamPhase::poisoned;
        return encrypt_error(SecretStreamError::invalid_input);
    }
    if (static_cast<unsigned long long>(plaintext.size())
            > crypto_secretstream_xchacha20poly1305_MESSAGEBYTES_MAX
        || plaintext.size() > std::numeric_limits<std::size_t>::max()
                - secretstream_overhead_bytes) {
        impl_->phase = StreamPhase::poisoned;
        return encrypt_error(SecretStreamError::message_too_large);
    }

    PublicBytes ciphertext;
    try {
        fail_output_allocation_if_requested();
        ciphertext.resize(plaintext.size() + secretstream_overhead_bytes);
    } catch (const std::bad_alloc&) {
        return encrypt_error(SecretStreamError::resource_exhausted);
    } catch (const std::length_error&) {
        return encrypt_error(SecretStreamError::message_too_large);
    } catch (...) {
        return encrypt_error(SecretStreamError::resource_exhausted);
    }

    write_sequence(impl_->aad, impl_->sequence);
    unsigned long long ciphertext_size = 0;
    if (crypto_secretstream_xchacha20poly1305_push(
            &impl_->native,
            raw_mutable(ciphertext),
            &ciphertext_size,
            raw_const(plaintext),
            static_cast<unsigned long long>(plaintext.size()),
            raw_const(impl_->aad),
            static_cast<unsigned long long>(impl_->aad.size()),
            tag_value) != 0
        || ciphertext_size != ciphertext.size()) {
        impl_->phase = StreamPhase::poisoned;
        return encrypt_error(SecretStreamError::initialization_failed);
    }
    const auto sequence = impl_->sequence;
    advance_phase(impl_->phase, impl_->sequence, tag);
    return {
        std::move(ciphertext), sequence, tag, SecretStreamError::none};
}

std::uint64_t SecretStreamPush::next_sequence() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->sequence;
}

bool SecretStreamPush::finalized() const noexcept
{
    return impl_ != nullptr && impl_->phase == StreamPhase::finalized;
}

bool SecretStreamPush::poisoned() const noexcept
{
    return impl_ == nullptr || impl_->phase == StreamPhase::poisoned;
}

SecretStreamPull::SecretStreamPull(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{}

SecretStreamPull::~SecretStreamPull() = default;
SecretStreamPull::SecretStreamPull(SecretStreamPull&&) noexcept = default;
SecretStreamPull& SecretStreamPull::operator=(SecretStreamPull&&) noexcept = default;

SecretStreamCreateResult<SecretStreamPull> SecretStreamPull::create(
    const std::span<const std::byte> key,
    const std::span<const std::byte> header,
    const std::span<const std::byte> aad_prefix)
{
    if (!sodium_runtime_ready())
        return {std::nullopt, SecretStreamError::initialization_failed};
    if (key.size() != secretstream_key_bytes)
        return {std::nullopt, SecretStreamError::invalid_key};
    if (header.size() != secretstream_header_bytes)
        return {std::nullopt, SecretStreamError::invalid_header};
    if (!valid_prefix_size(aad_prefix.size()))
        return {std::nullopt, SecretStreamError::invalid_input};
    try {
        auto impl = std::make_unique<Impl>(aad_prefix);
        if (crypto_secretstream_xchacha20poly1305_init_pull(
                &impl->native,
                raw_const(header),
                raw_const(key)) != 0) {
            return {std::nullopt, SecretStreamError::invalid_header};
        }
        SecretStreamPull stream{std::move(impl)};
        return {
            std::optional<SecretStreamPull>{std::move(stream)},
            SecretStreamError::none};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, SecretStreamError::resource_exhausted};
    } catch (const std::length_error&) {
        return {std::nullopt, SecretStreamError::invalid_input};
    } catch (...) {
        return {std::nullopt, SecretStreamError::resource_exhausted};
    }
}

SecretStreamDecryptResult SecretStreamPull::pull(
    const std::span<const std::byte> ciphertext)
{
    if (impl_ == nullptr) return decrypt_error(SecretStreamError::poisoned);
    if (impl_->phase != StreamPhase::active)
        return decrypt_error(phase_error(impl_->phase));
    if (ciphertext.size() < secretstream_overhead_bytes) {
        impl_->phase = StreamPhase::poisoned;
        return decrypt_error(SecretStreamError::invalid_input);
    }
    const auto plaintext_capacity = ciphertext.size() - secretstream_overhead_bytes;
    if (static_cast<unsigned long long>(plaintext_capacity)
        > crypto_secretstream_xchacha20poly1305_MESSAGEBYTES_MAX) {
        impl_->phase = StreamPhase::poisoned;
        return decrypt_error(SecretStreamError::message_too_large);
    }

    SecretBuffer plaintext;
    try {
        fail_output_allocation_if_requested();
        plaintext = SecretBuffer{plaintext_capacity};
    } catch (const std::bad_alloc&) {
        return decrypt_error(SecretStreamError::resource_exhausted);
    } catch (const std::length_error&) {
        return decrypt_error(SecretStreamError::message_too_large);
    } catch (...) {
        return decrypt_error(SecretStreamError::resource_exhausted);
    }

    write_sequence(impl_->aad, impl_->sequence);
    unsigned long long plaintext_size = 0;
    unsigned char tag_value = 0xFFU;
    if (crypto_secretstream_xchacha20poly1305_pull(
            &impl_->native,
            raw_mutable(plaintext.mutable_bytes()),
            &plaintext_size,
            &tag_value,
            raw_const(ciphertext),
            static_cast<unsigned long long>(ciphertext.size()),
            raw_const(impl_->aad),
            static_cast<unsigned long long>(impl_->aad.size())) != 0) {
        impl_->phase = StreamPhase::poisoned;
        return decrypt_error(SecretStreamError::authentication_failed);
    }
    if (plaintext_size != plaintext_capacity) {
        impl_->phase = StreamPhase::poisoned;
        return decrypt_error(SecretStreamError::authentication_failed);
    }

    SecretStreamTag tag{};
    if (tag_value == crypto_secretstream_xchacha20poly1305_TAG_MESSAGE) {
        tag = SecretStreamTag::message;
    } else if (tag_value == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
        tag = SecretStreamTag::final;
    } else {
        impl_->phase = StreamPhase::poisoned;
        return decrypt_error(SecretStreamError::unexpected_tag);
    }

    const auto sequence = impl_->sequence;
    advance_phase(impl_->phase, impl_->sequence, tag);
    return {
        std::move(plaintext), sequence, tag, SecretStreamError::none};
}

std::uint64_t SecretStreamPull::next_sequence() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->sequence;
}

bool SecretStreamPull::finalized() const noexcept
{
    return impl_ != nullptr && impl_->phase == StreamPhase::finalized;
}

bool SecretStreamPull::poisoned() const noexcept
{
    return impl_ == nullptr || impl_->phase == StreamPhase::poisoned;
}

}  // namespace baas::service::auth
