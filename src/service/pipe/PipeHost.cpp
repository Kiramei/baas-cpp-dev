#include "service/pipe/PipeHost.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace baas::service::pipe {
namespace {

thread_local PipeHost* active_worker_host = nullptr;

[[nodiscard]] constexpr std::uint8_t byte_value(const std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint32_t declared_payload_size(
    const bpip::Header& header) noexcept
{
    return static_cast<std::uint32_t>(byte_value(header[6]))
        | (static_cast<std::uint32_t>(byte_value(header[7])) << 8U)
        | (static_cast<std::uint32_t>(byte_value(header[8])) << 16U)
        | (static_cast<std::uint32_t>(byte_value(header[9])) << 24U);
}

[[nodiscard]] std::span<const std::byte> as_bytes(const std::string_view value) noexcept
{
    return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

[[nodiscard]] bool valid_limits(const PipeHostLimits& limits) noexcept
{
    return limits.max_connections > 0 && limits.max_connections <= 64
        && limits.max_open_json_bytes > 0
        && limits.max_open_json_bytes <= bpip::max_payload_size
        && limits.max_name_bytes > 0
        && limits.max_name_bytes <= limits.max_open_json_bytes
        && limits.max_read_chunk_bytes > 0
        && limits.max_read_chunk_bytes <= 1U * 1'024U * 1'024U
        && limits.max_atomic_write_bytes >= bpip::header_size
        && limits.max_atomic_write_bytes <= 128U * 1'024U * 1'024U
        && limits.max_total_ingress_retained_bytes >= bpip::header_size
        && limits.max_total_ingress_retained_bytes <= 256U * 1'024U * 1'024U
        && limits.max_total_egress_retained_bytes >= 2U * bpip::header_size
        && limits.max_total_egress_retained_bytes <= 256U * 1'024U * 1'024U
        && limits.max_open_json_depth > 0 && limits.max_open_json_depth <= 64
        && limits.max_open_json_nodes > 0 && limits.max_open_json_nodes <= 4'096
        && limits.open_timeout.count() > 0 && limits.open_timeout.count() <= 60'000
        && limits.idle_read_timeout.count() > 0
        && limits.idle_read_timeout.count() <= 600'000
        && limits.write_timeout.count() > 0 && limits.write_timeout.count() <= 60'000;
}

[[nodiscard]] bool valid_utf8(const std::string_view input) noexcept
{
    std::size_t index = 0;
    while (index < input.size()) {
        const auto lead = static_cast<unsigned char>(input[index]);
        if (lead <= 0x7F) {
            ++index;
            continue;
        }
        std::size_t length{};
        std::uint32_t value{};
        std::uint32_t minimum{};
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2; value = lead & 0x1FU; minimum = 0x80;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3; value = lead & 0x0FU; minimum = 0x800;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4; value = lead & 0x07U; minimum = 0x10000;
        } else {
            return false;
        }
        if (index + length > input.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto byte = static_cast<unsigned char>(input[index + offset]);
            if ((byte & 0xC0U) != 0x80U) return false;
            value = (value << 6U) | (byte & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFF
            || (value >= 0xD800 && value <= 0xDFFF)) return false;
        index += length;
    }
    return true;
}

void append_utf8(std::string& output, const std::uint32_t value)
{
    if (value <= 0x7F) output.push_back(static_cast<char>(value));
    else if (value <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (value <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

class OpenParser final {
public:
    OpenParser(const std::string_view input, const PipeHostLimits& limits)
        : input_(input), limits_(limits)
    {}

    [[nodiscard]] PipeOpenResult parse()
    {
        if (!valid_utf8(input_)) return {{}, PipeHostError::malformed_open_json};
        skip_space();
        if (!consume('{')) return {{}, PipeHostError::malformed_open_json};
        std::vector<std::string> keys;
        std::optional<std::string> type;
        std::optional<std::string> channel;
        std::optional<std::string> name;
        skip_space();
        if (!consume('}')) {
            while (true) {
                auto key = parse_string();
                if (!key) return failure();
                if (std::find(keys.begin(), keys.end(), *key) != keys.end()) {
                    duplicate_ = true;
                    return failure();
                }
                keys.push_back(*key);
                skip_space();
                if (!consume(':')) return failure();
                skip_space();
                if (*key == "type" || *key == "channel" || *key == "name") {
                    auto value = parse_string();
                    if (!value) return failure();
                    if (*key == "type") type = std::move(*value);
                    else if (*key == "channel") channel = std::move(*value);
                    else name = std::move(*value);
                } else if (!skip_value(1)) {
                    return failure();
                }
                skip_space();
                if (consume('}')) break;
                if (!consume(',')) return failure();
                skip_space();
            }
        }
        skip_space();
        if (offset_ != input_.size()) return failure();
        if (!type || *type != "open") return {{}, PipeHostError::invalid_open_type};
        if (!channel) return {{}, PipeHostError::unsupported_channel};
        const auto decoded_channel = channel_from(*channel);
        if (!decoded_channel) return {{}, PipeHostError::unsupported_channel};
        if (!name || name->empty() || name->size() > limits_.max_name_bytes
            || std::any_of(name->begin(), name->end(), [](const unsigned char c) {
                return c < 0x20U || c == 0x7FU;
            })) {
            return {{}, PipeHostError::invalid_open_name};
        }
        return {PipeOpenRequest{*decoded_channel, std::move(*name)}, PipeHostError::none};
    }

private:
    [[nodiscard]] PipeOpenResult failure() const
    {
        return {{}, duplicate_ ? PipeHostError::duplicate_open_field
                               : PipeHostError::malformed_open_json};
    }

    void skip_space() noexcept
    {
        while (offset_ < input_.size()
               && (input_[offset_] == ' ' || input_[offset_] == '\t'
                   || input_[offset_] == '\r' || input_[offset_] == '\n')) ++offset_;
    }

    [[nodiscard]] bool consume(const char value) noexcept
    {
        if (offset_ >= input_.size() || input_[offset_] != value) return false;
        ++offset_;
        return true;
    }

    [[nodiscard]] std::optional<std::uint32_t> hex4()
    {
        if (offset_ + 4 > input_.size()) return std::nullopt;
        std::uint32_t value{};
        for (int index = 0; index < 4; ++index) {
            const char c = input_[offset_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::optional<std::string> parse_string()
    {
        if (!consume('"')) return std::nullopt;
        std::string output;
        while (offset_ < input_.size()) {
            const auto c = static_cast<unsigned char>(input_[offset_++]);
            if (c == '"') return output;
            if (c < 0x20U) return std::nullopt;
            if (c != '\\') {
                output.push_back(static_cast<char>(c));
            } else {
                if (offset_ >= input_.size()) return std::nullopt;
                switch (input_[offset_++]) {
                    case '"': output.push_back('"'); break;
                    case '\\': output.push_back('\\'); break;
                    case '/': output.push_back('/'); break;
                    case 'b': output.push_back('\b'); break;
                    case 'f': output.push_back('\f'); break;
                    case 'n': output.push_back('\n'); break;
                    case 'r': output.push_back('\r'); break;
                    case 't': output.push_back('\t'); break;
                    case 'u': {
                        auto scalar = hex4();
                        if (!scalar) return std::nullopt;
                        if (*scalar >= 0xD800 && *scalar <= 0xDBFF) {
                            if (offset_ + 2 > input_.size() || input_[offset_] != '\\'
                                || input_[offset_ + 1] != 'u') return std::nullopt;
                            offset_ += 2;
                            auto low = hex4();
                            if (!low || *low < 0xDC00 || *low > 0xDFFF) return std::nullopt;
                            *scalar = 0x10000U + ((*scalar - 0xD800U) << 10U)
                                + (*low - 0xDC00U);
                        } else if (*scalar >= 0xDC00 && *scalar <= 0xDFFF) {
                            return std::nullopt;
                        }
                        append_utf8(output, *scalar);
                        break;
                    }
                    default: return std::nullopt;
                }
            }
            if (output.size() > limits_.max_open_json_bytes) return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool bump_node() noexcept
    {
        return ++nodes_ <= limits_.max_open_json_nodes;
    }

    [[nodiscard]] bool skip_value(const std::size_t depth)
    {
        if (depth > limits_.max_open_json_depth || !bump_node()) return false;
        skip_space();
        if (offset_ >= input_.size()) return false;
        if (input_[offset_] == '"') return parse_string().has_value();
        if (input_[offset_] == '{') return skip_object(depth);
        if (input_[offset_] == '[') return skip_array(depth);
        for (const auto literal : {"true", "false", "null"}) {
            if (input_.substr(offset_, std::strlen(literal)) == literal) {
                offset_ += std::strlen(literal);
                return true;
            }
        }
        return skip_number();
    }

    [[nodiscard]] bool skip_object(const std::size_t depth)
    {
        if (!consume('{')) return false;
        std::vector<std::string> keys;
        skip_space();
        if (consume('}')) return true;
        while (true) {
            auto key = parse_string();
            if (!key) return false;
            if (std::find(keys.begin(), keys.end(), *key) != keys.end()) {
                duplicate_ = true;
                return false;
            }
            keys.push_back(std::move(*key));
            skip_space();
            if (!consume(':') || !skip_value(depth + 1)) return false;
            skip_space();
            if (consume('}')) return true;
            if (!consume(',')) return false;
            skip_space();
        }
    }

    [[nodiscard]] bool skip_array(const std::size_t depth)
    {
        if (!consume('[')) return false;
        skip_space();
        if (consume(']')) return true;
        while (true) {
            if (!skip_value(depth + 1)) return false;
            skip_space();
            if (consume(']')) return true;
            if (!consume(',')) return false;
            skip_space();
        }
    }

    [[nodiscard]] bool skip_number()
    {
        const auto begin = offset_;
        if (offset_ < input_.size() && input_[offset_] == '-') ++offset_;
        if (offset_ >= input_.size()) return false;
        if (input_[offset_] == '0') ++offset_;
        else if (input_[offset_] >= '1' && input_[offset_] <= '9') {
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') ++offset_;
        } else return false;
        if (offset_ < input_.size() && input_[offset_] == '.') {
            ++offset_;
            const auto digits = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') ++offset_;
            if (digits == offset_) return false;
        }
        if (offset_ < input_.size() && (input_[offset_] == 'e' || input_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < input_.size() && (input_[offset_] == '+' || input_[offset_] == '-'))
                ++offset_;
            const auto digits = offset_;
            while (offset_ < input_.size() && input_[offset_] >= '0'
                   && input_[offset_] <= '9') ++offset_;
            if (digits == offset_) return false;
        }
        return offset_ > begin;
    }

    [[nodiscard]] static std::optional<PipeChannel> channel_from(
        const std::string_view value) noexcept
    {
        if (value == "provider") return PipeChannel::provider;
        if (value == "sync") return PipeChannel::sync;
        if (value == "trigger") return PipeChannel::trigger;
        if (value == "remote") return PipeChannel::remote;
        return std::nullopt;
    }

    std::string_view input_;
    const PipeHostLimits& limits_;
    std::size_t offset_{};
    std::size_t nodes_{};
    bool duplicate_{};
};

[[nodiscard]] PipeHostError terminal_error(
    PipeConnectionWriter& writer, const PipeHostError error) noexcept
{
    try {
    const auto text = pipe_host_error_name(error);
    const std::array frames{
        bpip::Frame{bpip::kind_value(bpip::FrameKind::error),
                    bpip::Bytes{as_bytes(text).begin(), as_bytes(text).end()}},
        bpip::Frame{bpip::kind_value(bpip::FrameKind::close), {}},
    };
    const auto write = writer.write_batch(frames);
    return write == PipeHostError::none ? error : write;
    } catch (...) {
        return PipeHostError::write_failed;
    }
}

}  // namespace

std::string_view pipe_channel_name(const PipeChannel channel) noexcept
{
    switch (channel) {
        case PipeChannel::provider: return "provider";
        case PipeChannel::sync: return "sync";
        case PipeChannel::trigger: return "trigger";
        case PipeChannel::remote: return "remote";
    }
    return "unknown";
}

std::string_view pipe_host_error_name(const PipeHostError error) noexcept
{
    switch (error) {
        case PipeHostError::none: return "none";
        case PipeHostError::invalid_limits: return "invalid_limits";
        case PipeHostError::listener_closed: return "listener_closed";
        case PipeHostError::connection_limit: return "connection_limit";
        case PipeHostError::read_failed: return "read_failed";
        case PipeHostError::truncated_frame: return "truncated_frame";
        case PipeHostError::framing_error: return "framing_error";
        case PipeHostError::first_frame_not_json: return "first_frame_not_json";
        case PipeHostError::open_json_too_large: return "open_json_too_large";
        case PipeHostError::malformed_open_json: return "malformed_open_json";
        case PipeHostError::duplicate_open_field: return "duplicate_open_field";
        case PipeHostError::invalid_open_type: return "invalid_open_type";
        case PipeHostError::unsupported_channel: return "unsupported_channel";
        case PipeHostError::invalid_open_name: return "invalid_open_name";
        case PipeHostError::unsupported_frame_kind: return "unsupported_frame_kind";
        case PipeHostError::nonempty_close: return "nonempty_close";
        case PipeHostError::channel_unavailable: return "channel_unavailable";
        case PipeHostError::handler_failed: return "handler_failed";
        case PipeHostError::atomic_write_too_large: return "atomic_write_too_large";
        case PipeHostError::ingress_budget_exhausted: return "ingress_budget_exhausted";
        case PipeHostError::egress_budget_exhausted: return "egress_budget_exhausted";
        case PipeHostError::write_failed: return "write_failed";
        case PipeHostError::open_timeout: return "open_timeout";
        case PipeHostError::read_timeout: return "read_timeout";
    }
    return "unknown";
}

PipeOpenResult decode_pipe_open(
    const std::span<const std::byte> payload, const PipeHostLimits& limits)
{
    if (!valid_limits(limits)) return {{}, PipeHostError::invalid_limits};
    if (payload.size() > limits.max_open_json_bytes)
        return {{}, PipeHostError::open_json_too_large};
    const std::string_view input{
        reinterpret_cast<const char*>(payload.data()), payload.size()};
    return OpenParser{input, limits}.parse();
}

bpip::EncodeResult encode_pipe_open_ok(const PipeChannel channel)
{
    const auto json = std::string{"{\"type\":\"open_ok\",\"channel\":\""}
        + std::string{pipe_channel_name(channel)} + "\"}";
    return bpip::encode_frame(bpip::FrameKind::json, as_bytes(json));
}

PipeHostError PipeConnectionWriter::write_frame(
    const bpip::FrameKind kind, const std::span<const std::byte> payload)
{
    const auto wire_size = bpip::header_size + payload.size();
    bool reserved{};
    try {
        std::lock_guard lock(mutex_);
        if (close_requested_.load(std::memory_order_acquire)
            || transport_poisoned_) return PipeHostError::write_failed;
        if (kind == bpip::FrameKind::close && !payload.empty())
            return PipeHostError::nonempty_close;
        if (kind == bpip::FrameKind::error && !payload.empty()
            && !valid_utf8(std::string_view{
                reinterpret_cast<const char*>(payload.data()), payload.size()}))
            return PipeHostError::write_failed;
        if (payload.size() > bpip::max_payload_size
            || wire_size > max_atomic_write_bytes_)
            return PipeHostError::atomic_write_too_large;
        const auto header = bpip::encode_header(kind, payload.size());
        if (!header) return PipeHostError::atomic_write_too_large;
        if (!host_.try_reserve_egress(wire_size))
            return PipeHostError::egress_budget_exhausted;
        reserved = true;
        bpip::Bytes output;
        output.reserve(wire_size);
        output.insert(output.end(), header.header.begin(), header.header.end());
        output.insert(output.end(), payload.begin(), payload.end());
        // Pre-poison while holding mutex_: if virtual I/O throws, another
        // caller can never enter during stack unwinding with a writable state.
        transport_poisoned_ = true;
        const auto result = stream_.write_all(output, write_timeout_);
        host_.release_egress(wire_size);
        reserved = false;
        if (close_requested_.load(std::memory_order_acquire)
            || result.error || result.eof || result.timed_out
            || result.bytes != output.size()) {
            return PipeHostError::write_failed;
        }
        transport_poisoned_ = false;
        return PipeHostError::none;
    } catch (...) {
        if (reserved) host_.release_egress(wire_size);
        return PipeHostError::write_failed;
    }
}

PipeHostError PipeConnectionWriter::write_batch(const std::span<const bpip::Frame> frames)
{
    std::size_t wire_size{};
    bool reserved{};
    try {
        std::lock_guard lock(mutex_);
        if (close_requested_.load(std::memory_order_acquire)
            || transport_poisoned_) return PipeHostError::write_failed;
        for (const auto& frame : frames) {
            if (!bpip::is_known_kind(frame.kind))
                return PipeHostError::unsupported_frame_kind;
            if (frame.kind == bpip::kind_value(bpip::FrameKind::close)
                && !frame.payload.empty()) return PipeHostError::nonempty_close;
            if (frame.kind == bpip::kind_value(bpip::FrameKind::error)
                && !frame.payload.empty()
                && !valid_utf8(std::string_view{
                    reinterpret_cast<const char*>(frame.payload.data()),
                    frame.payload.size()})) return PipeHostError::write_failed;
            const auto frame_size = bpip::header_size + frame.payload.size();
            if (wire_size > max_atomic_write_bytes_
                || frame_size > max_atomic_write_bytes_ - wire_size) {
                return PipeHostError::atomic_write_too_large;
            }
            wire_size += frame_size;
        }
        if (wire_size == 0) return PipeHostError::none;
        if (!host_.try_reserve_egress(wire_size))
            return PipeHostError::egress_budget_exhausted;
        reserved = true;
        bpip::Bytes output;
        output.reserve(wire_size);
        for (const auto& frame : frames) {
            const auto header = bpip::encode_header(frame.kind, frame.payload.size());
            if (!header) {
                host_.release_egress(wire_size);
                reserved = false;
                return PipeHostError::atomic_write_too_large;
            }
            output.insert(output.end(), header.header.begin(), header.header.end());
            output.insert(output.end(), frame.payload.begin(), frame.payload.end());
        }
        // See write_frame(): poison before invoking virtual I/O so throwing
        // implementations cannot expose a writable gap during unwinding.
        transport_poisoned_ = true;
        const auto result = stream_.write_all(output, write_timeout_);
        host_.release_egress(wire_size);
        reserved = false;
        if (close_requested_.load(std::memory_order_acquire)
            || result.error || result.eof || result.timed_out
            || result.bytes != output.size()) {
            return PipeHostError::write_failed;
        }
        transport_poisoned_ = false;
        return PipeHostError::none;
    } catch (...) {
        if (reserved) host_.release_egress(wire_size);
        return PipeHostError::write_failed;
    }
}

bool PipeConnectionWriter::transport_poisoned() noexcept
{
    std::lock_guard lock(mutex_);
    return close_requested_.load(std::memory_order_acquire)
        || transport_poisoned_;
}

void PipeConnectionWriter::close_connection() noexcept
{
    close_requested_.store(true, std::memory_order_release);
    stream_.close();
}

PipeHost::PipeHost(
    std::unique_ptr<PipeListener> listener,
    std::shared_ptr<PipeChannelFactory> factory,
    const PipeHostLimits limits)
    : listener_(std::move(listener)), factory_(std::move(factory)), limits_(limits)
{
    if (!listener_ || !factory_ || !valid_limits(limits_))
        throw std::invalid_argument("pipe host configuration is invalid");
}

PipeHost::~PipeHost()
{
    // A callback destroying its own host would release storage while this
    // worker still executes with a captured `this`. Fail closed on that
    // explicit contract violation instead of allowing a worker use-after-free.
    if (active_worker_host == this) std::terminate();
    stop();
    join();
}

bool PipeHost::start()
{
    {
        std::lock_guard lock(mutex_);
        if (start_attempted_ || state_ != PipeHostState::stopped
            || accept_thread_.joinable()) return false;
        start_attempted_ = true;
        state_ = PipeHostState::running;
    }
    try {
        workers_.reserve(limits_.max_connections);
        for (std::size_t index = 0; index < limits_.max_connections; ++index)
            workers_.emplace_back([this] { worker_loop(); });
        accept_thread_ = std::thread([this] { accept_loop(); });
        return true;
    } catch (...) {
        stop();
        join();
        return false;
    }
}

void PipeHost::stop() noexcept
{
    stop_source_.request_stop();
    {
        std::lock_guard lock(mutex_);
        if (state_ == PipeHostState::stopped) {
            if (start_attempted_) return;
            start_attempted_ = true;
        } else {
            state_ = PipeHostState::stopping;
            for (auto& stream : pending_streams_) stream->close();
            pending_streams_.clear();
            for (auto* stream : active_streams_) stream->close();
        }
    }
    listener_->close();
    queue_cv_.notify_all();
}

void PipeHost::join() noexcept
{
    if (active_worker_host == this) {
        // A handler may request stop+join from its worker. Returning here
        // avoids self-join. The external owner must retain this host and later
        // perform the real join from a non-worker thread.
        return;
    }
    std::lock_guard join_lock(join_mutex_);
    if (accept_thread_.joinable()) accept_thread_.join();
    queue_cv_.notify_all();
    for (auto& worker : workers_) if (worker.joinable()) worker.join();
    workers_.clear();
    std::lock_guard lock(mutex_);
    state_ = PipeHostState::stopped;
    stopped_cv_.notify_all();
}

PipeHostState PipeHost::state() const noexcept
{
    std::lock_guard lock(mutex_);
    return state_;
}

PipeHostStats PipeHost::stats() const noexcept
{
    std::lock_guard lock(mutex_);
    return {accepted_, rejected_, completed_, active_, peak_active_,
        ingress_retained_bytes_, peak_ingress_retained_bytes_,
        egress_retained_bytes_, peak_egress_retained_bytes_,
        ingress_budget_rejections_, egress_budget_rejections_, state_};
}

bool PipeHost::try_reserve_ingress(const std::size_t bytes) noexcept
{
    std::lock_guard lock(mutex_);
    if (bytes > limits_.max_total_ingress_retained_bytes - ingress_retained_bytes_) {
        ++ingress_budget_rejections_;
        return false;
    }
    ingress_retained_bytes_ += bytes;
    peak_ingress_retained_bytes_ = std::max(
        peak_ingress_retained_bytes_, ingress_retained_bytes_);
    return true;
}

void PipeHost::release_ingress(const std::size_t bytes) noexcept
{
    std::lock_guard lock(mutex_);
    ingress_retained_bytes_ -= std::min(bytes, ingress_retained_bytes_);
}

bool PipeHost::try_reserve_egress(const std::size_t bytes) noexcept
{
    std::lock_guard lock(mutex_);
    if (bytes > limits_.max_total_egress_retained_bytes - egress_retained_bytes_) {
        ++egress_budget_rejections_;
        return false;
    }
    egress_retained_bytes_ += bytes;
    peak_egress_retained_bytes_ = std::max(
        peak_egress_retained_bytes_, egress_retained_bytes_);
    return true;
}

void PipeHost::release_egress(const std::size_t bytes) noexcept
{
    std::lock_guard lock(mutex_);
    egress_retained_bytes_ -= std::min(bytes, egress_retained_bytes_);
}

void PipeHost::accept_loop() noexcept
{
    try {
    while (true) {
        {
            std::lock_guard lock(mutex_);
            if (state_ != PipeHostState::running) break;
        }
        auto stream = listener_->accept();
        if (!stream) {
            stop_source_.request_stop();
            {
                std::lock_guard lock(mutex_);
                if (state_ == PipeHostState::running) {
                    state_ = PipeHostState::stopping;
                    for (auto& pending : pending_streams_) pending->close();
                    pending_streams_.clear();
                    for (auto* active : active_streams_) active->close();
                }
            }
            listener_->close();
            queue_cv_.notify_all();
            break;
        }
        bool rejected = false;
        {
            std::lock_guard lock(mutex_);
            if (state_ != PipeHostState::running
                || active_ + pending_streams_.size() >= limits_.max_connections) {
                ++rejected_;
                rejected = true;
            } else {
                pending_streams_.push_back(std::move(stream));
                ++accepted_;
            }
        }
        if (rejected) {
            PipeConnectionWriter writer{
                *this, *stream, limits_.max_atomic_write_bytes, limits_.write_timeout};
            static_cast<void>(terminal_error(writer, PipeHostError::connection_limit));
            stream->close();
        } else {
            queue_cv_.notify_one();
        }
    }
    } catch (...) {
        stop_source_.request_stop();
        std::lock_guard lock(mutex_);
        state_ = PipeHostState::stopping;
        for (auto& pending : pending_streams_) pending->close();
        pending_streams_.clear();
        for (auto* active : active_streams_) active->close();
        listener_->close();
        queue_cv_.notify_all();
    }
}

void PipeHost::worker_loop() noexcept
{
    active_worker_host = this;
    try {
    while (true) {
        std::unique_ptr<PipeStream> stream;
        {
            std::unique_lock lock(mutex_);
            queue_cv_.wait(lock, [this] {
                return state_ != PipeHostState::running || !pending_streams_.empty();
            });
            if (state_ != PipeHostState::running) {
                for (auto& pending : pending_streams_) pending->close();
                pending_streams_.clear();
                active_worker_host = nullptr;
                return;
            }
            stream = std::move(pending_streams_.front());
            pending_streams_.pop_front();
            active_streams_.push_back(stream.get());
            ++active_;
            peak_active_ = std::max(peak_active_, active_);
        }
        connection_loop(std::move(stream));
    }
    } catch (...) {
        stop_source_.request_stop();
        std::lock_guard lock(mutex_);
        state_ = PipeHostState::stopping;
        for (auto& pending : pending_streams_) pending->close();
        pending_streams_.clear();
        for (auto* active : active_streams_) active->close();
        listener_->close();
        queue_cv_.notify_all();
    }
    active_worker_host = nullptr;
}

void PipeHost::connection_loop(std::unique_ptr<PipeStream> stream) noexcept
{
    bpip::Decoder decoder;
    PipeConnectionWriter writer{
        *this, *stream, limits_.max_atomic_write_bytes, limits_.write_timeout};
    std::unique_ptr<PipeChannelHandler> handler;
    bool opened = false;
    bpip::Header open_header{};
    std::size_t open_header_bytes{};
    std::optional<std::uint32_t> open_expected;
    bpip::Bytes open_payload;
    std::size_t open_reservation{};
    std::size_t decoder_reservation{};
    const auto open_deadline = std::chrono::steady_clock::now()
        + limits_.open_timeout;
    try {
        std::vector<std::byte> buffer(limits_.max_read_chunk_bytes);
        bool keep_running = true;
        const auto fail_connection = [&](const PipeHostError error) {
            if (!writer.transport_poisoned())
                static_cast<void>(terminal_error(writer, error));
            keep_running = false;
        };
        const auto process_decoded = [&](const bpip::DecodeResult& decoded) {
            for (const auto& frame : decoded.frames) {
                const auto error = process_frame(
                    frame, opened, keep_running, handler, writer);
                if (error != PipeHostError::none) {
                    fail_connection(error);
                    return;
                }
                if (!keep_running) return;
            }
        };
        while (keep_running) {
            auto read_timeout = limits_.idle_read_timeout;
            if (!opened) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= open_deadline) {
                    static_cast<void>(terminal_error(writer, PipeHostError::open_timeout));
                    break;
                }
                read_timeout = std::chrono::ceil<std::chrono::milliseconds>(
                    open_deadline - now);
            }
            const auto read = stream->read(buffer, read_timeout);
            if (read.timed_out) {
                static_cast<void>(terminal_error(
                    writer, opened ? PipeHostError::read_timeout
                                   : PipeHostError::open_timeout));
                break;
            }
            if (read.error) {
                static_cast<void>(terminal_error(writer, PipeHostError::read_failed));
                break;
            }
            if (read.eof) {
                if (decoder.buffered_bytes() != 0 || open_header_bytes != 0
                    || open_expected.has_value())
                    static_cast<void>(terminal_error(
                        writer, PipeHostError::truncated_frame));
                break;
            }
            if (read.bytes == 0 || read.bytes > buffer.size()) {
                static_cast<void>(terminal_error(writer, PipeHostError::read_failed));
                break;
            }
            // A stream implementation may return bytes at its timeout edge.
            // Do not let drip-fed fragments extend the absolute open deadline.
            if (!opened && std::chrono::steady_clock::now() >= open_deadline) {
                static_cast<void>(terminal_error(writer, PipeHostError::open_timeout));
                break;
            }
            std::size_t offset{};
            while (offset < read.bytes && keep_running) {
                if (!opened) {
                    if (open_header_bytes < bpip::header_size) {
                        const auto count = std::min(
                            bpip::header_size - open_header_bytes, read.bytes - offset);
                        std::copy_n(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                            count, open_header.begin()
                                + static_cast<std::ptrdiff_t>(open_header_bytes));
                        open_header_bytes += count;
                        offset += count;
                        if (open_header_bytes < bpip::header_size) continue;
                        if (!std::equal(
                                bpip::magic.begin(), bpip::magic.end(), open_header.begin())
                            || byte_value(open_header[4]) != bpip::version) {
                            fail_connection(PipeHostError::framing_error);
                            break;
                        }
                        if (byte_value(open_header[5])
                            != bpip::kind_value(bpip::FrameKind::json)) {
                            fail_connection(PipeHostError::first_frame_not_json);
                            break;
                        }
                        const auto declared = declared_payload_size(open_header);
                        if (declared > limits_.max_open_json_bytes) {
                            fail_connection(PipeHostError::open_json_too_large);
                            break;
                        }
                        open_expected = declared;
                        if (declared != 0) {
                            if (!try_reserve_ingress(declared)) {
                                fail_connection(PipeHostError::ingress_budget_exhausted);
                                break;
                            }
                            open_reservation = declared;
                        }
                    }
                    const auto remaining = static_cast<std::size_t>(*open_expected)
                        - open_payload.size();
                    const auto count = std::min(remaining, read.bytes - offset);
                    open_payload.insert(open_payload.end(),
                        buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                        buffer.begin() + static_cast<std::ptrdiff_t>(offset + count));
                    offset += count;
                    if (open_payload.size() == *open_expected) {
                        const bpip::Frame first{
                            bpip::kind_value(bpip::FrameKind::json),
                            std::move(open_payload)};
                        const auto error = process_frame(
                            first, opened, keep_running, handler, writer);
                        if (open_reservation != 0) {
                            release_ingress(open_reservation);
                            open_reservation = 0;
                        }
                        open_expected.reset();
                        open_header_bytes = 0;
                        if (error != PipeHostError::none) fail_connection(error);
                    }
                    continue;
                }

                if (!decoder.expected_payload_size()) {
                    const auto header_needed = bpip::header_size - decoder.buffered_bytes();
                    const auto count = std::min(header_needed, read.bytes - offset);
                    const auto decoded = decoder.feed(
                        {buffer.data() + offset, count});
                    offset += count;
                    if (decoded.error) {
                        fail_connection(PipeHostError::framing_error);
                        break;
                    }
                    process_decoded(decoded);
                    if (!keep_running) break;
                    if (const auto expected = decoder.expected_payload_size();
                        expected && *expected != 0) {
                        if (!try_reserve_ingress(*expected)) {
                            fail_connection(PipeHostError::ingress_budget_exhausted);
                            break;
                        }
                        decoder_reservation = *expected;
                    }
                    continue;
                }

                const auto remaining = static_cast<std::size_t>(
                    *decoder.expected_payload_size()) - decoder.buffered_payload_bytes();
                const auto count = std::min(remaining, read.bytes - offset);
                const auto decoded = decoder.feed({buffer.data() + offset, count});
                offset += count;
                if (decoded.error) {
                    fail_connection(PipeHostError::framing_error);
                } else {
                    process_decoded(decoded);
                }
                if (!decoder.expected_payload_size() && decoder_reservation != 0) {
                    release_ingress(decoder_reservation);
                    decoder_reservation = 0;
                }
            }
        }
    } catch (...) {
        if (!writer.transport_poisoned())
            static_cast<void>(terminal_error(writer, PipeHostError::handler_failed));
    }
    if (open_reservation != 0) release_ingress(open_reservation);
    if (decoder_reservation != 0) release_ingress(decoder_reservation);
    if (handler) handler->on_close(stop_source_.get_token());
    stream->close();
    {
        std::lock_guard lock(mutex_);
        active_streams_.erase(
            std::remove(active_streams_.begin(), active_streams_.end(), stream.get()),
            active_streams_.end());
        --active_;
        ++completed_;
    }
    stopped_cv_.notify_all();
}

PipeHostError PipeHost::process_frame(
    const bpip::Frame& frame,
    bool& opened,
    bool& continue_connection,
    std::unique_ptr<PipeChannelHandler>& handler,
    PipeConnectionWriter& writer)
{
    const auto apply_handler_result = [&](const PipeHandlerResult& result) {
        if (writer.transport_poisoned()) return PipeHostError::write_failed;
        if (result.error != PipeHostError::none) return result.error;
        if (result.action == PipeHandlerAction::close_connection) {
            const auto write = writer.write_frame(bpip::FrameKind::close, {});
            if (write != PipeHostError::none) return write;
            continue_connection = false;
        }
        return PipeHostError::none;
    };

    if (!opened) {
        if (frame.kind != bpip::kind_value(bpip::FrameKind::json))
            return PipeHostError::first_frame_not_json;
        const auto open = decode_pipe_open(frame.payload, limits_);
        if (!open) return open.error;
        try {
            handler = factory_->create(*open.request, stop_source_.get_token());
        } catch (...) {
            return PipeHostError::channel_unavailable;
        }
        if (!handler) return PipeHostError::channel_unavailable;
        const auto response = encode_pipe_open_ok(open.request->channel);
        if (!response) return PipeHostError::atomic_write_too_large;
        const auto write = writer.write_frame(bpip::FrameKind::json,
            {response.bytes.data() + bpip::header_size,
             response.bytes.size() - bpip::header_size});
        if (write != PipeHostError::none) return write;
        opened = true;
        try {
            return apply_handler_result(handler->on_open(
                writer, stop_source_.get_token()));
        } catch (...) {
            return PipeHostError::handler_failed;
        }
    }

    if (frame.kind == bpip::kind_value(bpip::FrameKind::close)) {
        if (!frame.payload.empty()) return PipeHostError::nonempty_close;
        continue_connection = false;
        return PipeHostError::none;
    }
    if (frame.kind == bpip::kind_value(bpip::FrameKind::error)) {
        continue_connection = false;
        return PipeHostError::none;
    }
    if (frame.kind != bpip::kind_value(bpip::FrameKind::json)
        && frame.kind != bpip::kind_value(bpip::FrameKind::bytes)) {
        return PipeHostError::unsupported_frame_kind;
    }
    try {
        return apply_handler_result(handler->on_frame(
            frame, writer, stop_source_.get_token()));
    } catch (...) {
        return PipeHostError::handler_failed;
    }
}

}  // namespace baas::service::pipe
