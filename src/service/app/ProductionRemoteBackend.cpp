#include "service/app/ProductionRemoteBackend.h"

#include "service/auth/CanonicalJson.h"
#include "service/adb/ServiceAdbSync.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

namespace baas::service::app {
namespace {

using Json = nlohmann::json;
using adb::AdbTransportError;
using channels::RemoteBackendError;
using channels::RemoteIoStatus;
using channels::RemoteOpenResult;
using channels::RemoteSessionCallbacks;
using channels::RemoteSessionEnd;

constexpr std::uint16_t server_port = 8886;
constexpr std::string_view remote_jar =
    "/data/local/tmp/baas-ws-scrcpy-server.jar";
constexpr std::string_view package = "com.genymobile.scrcpy.Server";
constexpr std::string_view version = "1.19-ws7";
constexpr std::string_view server_type = "web";
constexpr std::string_view log_level = "ERROR";
constexpr std::string_view all_interfaces = "true";
constexpr std::string_view owner_environment = "BAAS_WS_SCRCPY_OWNER=";
constexpr std::size_t owner_token_bytes = 32;

struct ServerOwnership final {
    std::string token;
    unsigned supervisor_pid{};
    std::uint64_t supervisor_start_time{};
    unsigned child_pid{};
    std::uint64_t child_start_time{};
};

std::optional<std::string> make_owner_token()
{
    if (sodium_init() < 0) return std::nullopt;
    std::array<unsigned char, owner_token_bytes> bytes{};
    randombytes_buf(bytes.data(), bytes.size());
    constexpr std::array<char, 16> hex{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string token(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        token[index * 2] = hex[bytes[index] >> 4U];
        token[index * 2 + 1] = hex[bytes[index] & 0x0fU];
    }
    sodium_memzero(bytes.data(), bytes.size());
    return token;
}

bool valid_owner_token(const std::string_view token) noexcept
{
    return token.size() == owner_token_bytes * 2
        && std::all_of(token.begin(), token.end(), [](const char value) {
            return (value >= '0' && value <= '9')
                || (value >= 'a' && value <= 'f');
        });
}

std::optional<std::string> encode_shell_script(
    const std::string_view script)
{
    if (script.empty() || script.find('\0') != std::string_view::npos
        || sodium_init() < 0) {
        return std::nullopt;
    }
    std::string normalized;
    normalized.reserve(script.size());
    for (std::size_t index = 0; index < script.size(); ++index) {
        if (script[index] != '\r') {
            normalized.push_back(script[index]);
            continue;
        }
        if (index + 1 >= script.size() || script[index + 1] != '\n') {
            return std::nullopt;
        }
    }
    const auto encoded_size = sodium_base64_encoded_len(
        normalized.size(), sodium_base64_VARIANT_ORIGINAL);
    if (encoded_size == 0) return std::nullopt;
    std::string encoded(encoded_size, '\0');
    if (sodium_bin2base64(
            encoded.data(), encoded.size(),
            reinterpret_cast<const unsigned char*>(normalized.data()),
            normalized.size(), sodium_base64_VARIANT_ORIGINAL) == nullptr) {
        return std::nullopt;
    }
    encoded.resize(std::char_traits<char>::length(encoded.c_str()));
    return "printf %s '" + encoded
        + "' | /system/bin/base64 -d | /system/bin/sh";
}

class ConcreteAdbClient final : public RemoteAdbClient {
public:
    explicit ConcreteAdbClient(std::shared_ptr<adb::ServiceAdbTransport> transport)
        : transport_(std::move(transport)), sync_(*transport_)
    {}

    adb::AdbTransportResult<std::string> get_state(
        const std::string_view serial, const std::stop_token stop) override
    { return transport_->get_state(serial, stop); }

    adb::AdbTransportResult<std::string> shell(
        const std::string_view serial, const std::string_view command,
        const std::stop_token stop) override
    {
        if (command.find_first_of("\r\n") == std::string_view::npos) {
            return transport_->shell_legacy(serial, command, stop);
        }
        const auto encoded = encode_shell_script(command);
        if (!encoded) {
            return {std::nullopt, adb::AdbTransportError::internal_error, {}};
        }
        return transport_->shell_legacy(serial, *encoded, stop);
    }

    adb::AdbTransportResult<std::uint64_t> push_file(
        const std::string_view serial, const std::string_view path,
        const std::filesystem::path& local, const std::stop_token stop) override
    { return sync_.push_file(serial, path, local, 0644, 0, stop); }

    adb::AdbTransportResult<std::vector<adb::AdbForwardItem>> list_forwards(
        const std::stop_token stop) override
    { return transport_->list_forwards(stop); }

    adb::AdbTransportResult<std::uint16_t> forward_tcp_zero(
        const std::string_view serial, const std::uint16_t port,
        const std::stop_token stop) override
    { return transport_->forward_tcp_zero(serial, port, stop); }

    adb::AdbTransportResult<bool> remove_tcp_forward(
        const std::string_view serial, const std::uint16_t port,
        const std::stop_token stop) override
    { return transport_->remove_tcp_forward(serial, port, stop); }

    void stop() noexcept override { transport_->stop(); }

private:
    std::shared_ptr<adb::ServiceAdbTransport> transport_;
    adb::ServiceAdbSync sync_;
};

void set_timeout(httplib::ws::WebSocketClient& client,
                 const std::chrono::milliseconds timeout,
                 void (httplib::ws::WebSocketClient::*setter)(time_t, time_t))
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        timeout - seconds);
    (client.*setter)(static_cast<time_t>(seconds.count()),
                     static_cast<time_t>(micros.count()));
}

class HttplibRemoteWebSocketClient final : public RemoteWebSocketClient {
public:
    HttplibRemoteWebSocketClient(
        std::string url, const std::chrono::milliseconds connect_timeout,
        const std::chrono::milliseconds write_timeout)
        : client_(std::move(url))
    {
        set_timeout(client_, connect_timeout,
                    &httplib::ws::WebSocketClient::set_connection_timeout);
        set_timeout(client_, write_timeout,
                    &httplib::ws::WebSocketClient::set_write_timeout);
        client_.set_websocket_ping_interval(0);
        client_.set_tcp_nodelay(true);
    }

    bool connect() override { return client_.is_valid() && client_.connect(); }

    RemoteWebSocketReadResult read() override
    {
        std::string payload;
        const auto result = client_.read(payload);
        if (result == httplib::ws::Binary)
            return {RemoteWebSocketReadKind::binary, std::move(payload)};
        if (result == httplib::ws::Text)
            return {RemoteWebSocketReadKind::text, std::move(payload)};
        return {client_.is_open() ? RemoteWebSocketReadKind::error
                                  : RemoteWebSocketReadKind::closed, {}};
    }

    bool send_binary(const std::span<const std::byte> bytes) override
    {
        return client_.send(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    bool request_close() noexcept override { return client_.request_close(); }
    void interrupt() noexcept override { client_.interrupt(); }

private:
    httplib::ws::WebSocketClient client_;
};

std::optional<std::uint16_t> parse_port(const std::string_view value)
{
    if (value.empty()) return std::nullopt;
    unsigned port{};
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size()
        || port == 0 || port > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(port);
}

bool valid_serial_component(const std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 256
        && value.find('\0') == std::string_view::npos
        && value.find(';') == std::string_view::npos
        && std::all_of(value.begin(), value.end(), [](const unsigned char byte) {
            return byte >= 0x21U && byte <= 0x7eU;
        });
}

struct DeviceConfig { std::string serial; };

std::optional<DeviceConfig> parse_device_config(
    const std::string_view bytes, const std::size_t maximum_bytes)
{
    if (bytes.empty() || bytes.size() > maximum_bytes
        || !auth::is_valid_utf8(bytes)) return std::nullopt;
    bool duplicate{};
    std::size_t nodes{};
    std::vector<std::unordered_set<std::string>> keys;
    const auto callback = [&](int depth, const Json::parse_event_t event,
                              Json& parsed) {
        if (depth > 64 || ++nodes > 65'536) return false;
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.empty()) {
            if (!keys.back().insert(parsed.get<std::string>()).second)
                duplicate = true;
        }
        else if (event == Json::parse_event_t::object_end && !keys.empty())
            keys.pop_back();
        return !duplicate;
    };
    const auto value = Json::parse(bytes, callback, false);
    if (duplicate || value.is_discarded() || !value.is_object()) return std::nullopt;
    const auto ip_it = value.find("adbIP");
    const auto port_it = value.find("adbPort");
    if (ip_it == value.end() || port_it == value.end()
        || !ip_it->is_string()
        || !(port_it->is_string() || port_it->is_number_unsigned()
             || port_it->is_number_integer())) return std::nullopt;
    const auto ip = ip_it->get<std::string>();
    std::string port;
    try {
        if (port_it->is_string()) port = port_it->get<std::string>();
        else if (port_it->is_number_unsigned()) {
            const auto number = port_it->get<std::uint64_t>();
            if (number > 65'535) return std::nullopt;
            port = std::to_string(number);
        }
        else {
            const auto number = port_it->get<std::int64_t>();
            if (number < 0 || number > 65'535) return std::nullopt;
            port = std::to_string(number);
        }
    }
    catch (...) { return std::nullopt; }
    if (ip.empty() && port.empty()) return std::nullopt;
    std::string serial;
    if (ip.empty()) serial = port;
    else if (port.empty()) serial = ip;
    else {
        if (!parse_port(port)) return std::nullopt;
        serial = ip + ':' + port;
    }
    if (!valid_serial_component(serial)) return std::nullopt;
    return DeviceConfig{std::move(serial)};
}

std::optional<unsigned> parse_pid(const std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return std::nullopt;
    const auto end = text.find_first_of(" \t\r\n", begin);
    const auto token = text.substr(begin, end == std::string_view::npos
        ? text.size() - begin : end - begin);
    unsigned pid{};
    const auto [parsed, error] = std::from_chars(
        token.data(), token.data() + token.size(), pid);
    if (error != std::errc{} || parsed != token.data() + token.size() || pid == 0)
        return std::nullopt;
    return pid;
}

std::optional<std::uint64_t> parse_u64(const std::string_view token)
{
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(
        token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size() || value == 0)
        return std::nullopt;
    return value;
}

std::optional<std::uint64_t> parse_process_start_time(
    const std::string_view stat)
{
    const auto comm_end = stat.rfind(") ");
    if (comm_end == std::string_view::npos) return std::nullopt;
    auto rest = stat.substr(comm_end + 2);
    for (unsigned field = 3; field <= 22; ++field) {
        const auto begin = rest.find_first_not_of(' ');
        if (begin == std::string_view::npos) return std::nullopt;
        const auto end = rest.find(' ', begin);
        const auto token = rest.substr(begin, end == std::string_view::npos
            ? rest.size() - begin : end - begin);
        if (field == 22) return parse_u64(token);
        if (end == std::string_view::npos) return std::nullopt;
        rest.remove_prefix(end + 1);
    }
    return std::nullopt;
}

std::optional<ServerOwnership> parse_lease_record(
    const std::string_view text, const std::string_view expected_token)
{
    std::array<std::string_view, 6> fields;
    std::size_t count{};
    std::size_t offset{};
    while (offset < text.size()) {
        offset = text.find_first_not_of(" \t\r\n", offset);
        if (offset == std::string_view::npos) break;
        const auto end = text.find_first_of(" \t\r\n", offset);
        if (count == fields.size()) return std::nullopt;
        fields[count++] = text.substr(offset, end == std::string_view::npos
            ? text.size() - offset : end - offset);
        if (end == std::string_view::npos) break;
        offset = end;
    }
    if (count != fields.size() || fields[0] != "OWNED"
        || fields[1] != expected_token) return std::nullopt;
    const auto supervisor_pid = parse_u64(fields[2]);
    const auto supervisor_start = parse_u64(fields[3]);
    const auto child_pid = parse_u64(fields[4]);
    const auto child_start = parse_u64(fields[5]);
    if (!supervisor_pid || *supervisor_pid > std::numeric_limits<unsigned>::max()
        || !child_pid || *child_pid > std::numeric_limits<unsigned>::max()
        || !supervisor_start || !child_start) return std::nullopt;
    return ServerOwnership{
        std::string{expected_token}, static_cast<unsigned>(*supervisor_pid),
        *supervisor_start, static_cast<unsigned>(*child_pid), *child_start};
}

bool expected_owner_environment(
    const std::string_view bytes, const std::string_view token)
{
    const auto expected = std::string{owner_environment} + std::string{token};
    std::size_t offset{};
    while (offset < bytes.size()) {
        auto end = bytes.find('\0', offset);
        if (end == std::string_view::npos) end = bytes.size();
        if (bytes.substr(offset, end - offset) == expected) return true;
        offset = end + 1;
    }
    return false;
}

void replace_token(std::string& command, const std::string_view token)
{
    constexpr std::string_view placeholder = "__BAAS_OWNER_TOKEN__";
    for (auto offset = command.find(placeholder); offset != std::string::npos;
         offset = command.find(placeholder, offset + token.size())) {
        command.replace(offset, placeholder.size(), token);
    }
}

std::string lease_probe_command()
{
    return R"SH(# BAAS_WS_LEASE_PROBE
lease=/data/local/tmp/baas-ws-scrcpy.lease
proc_start() { value=$(cat "/proc/$1/stat" 2>/dev/null) || return 1; value=${value##*) }; set -- $value; [ "$#" -ge 20 ] || return 1; printf "%s" "${20}"; }
if [ ! -L "$lease" ]; then echo NONE; exit 0; fi
target=$(readlink "$lease" 2>/dev/null) || { echo BUSY; exit 0; }
case "$target" in baas-ws-scrcpy.owner.*) ;; *) echo BUSY; exit 0;; esac
path_token=${target#baas-ws-scrcpy.owner.}
[ "${#path_token}" -eq 64 ] || { echo BUSY; exit 0; }
case "$path_token" in *[!0-9a-f]*) echo BUSY; exit 0;; esac
dir=/data/local/tmp/$target
read token supervisor supervisor_start < "$dir/init" 2>/dev/null || token=
 [ "$token" = "$path_token" ] || { echo BUSY; exit 0; }
actual_start=$(proc_start "$supervisor" 2>/dev/null) || actual_start=
if [ "${#token}" -eq 64 ] && [ "$actual_start" = "$supervisor_start" ] && tr '\000' '\n' < "/proc/$supervisor/environ" 2>/dev/null | grep -Fqx "BAAS_WS_SCRCPY_OWNER=$token"; then echo BUSY; exit 0; fi
if [ "$(readlink "$lease" 2>/dev/null)" = "$target" ]; then rm -f "$lease"; fi
rm -rf "$dir"
echo STALE
)SH";
}

std::string supervisor_launch_command(const std::string_view owner_token)
{
    std::string command = R"SH(# BAAS_WS_SUPERVISOR_LAUNCH
token=__BAAS_OWNER_TOKEN__
lease=/data/local/tmp/baas-ws-scrcpy.lease
name=baas-ws-scrcpy.owner.$token
dir=/data/local/tmp/$name
umask 077
mkdir "$dir" 2>/dev/null || { echo ERROR; exit 0; }
printf "%s\n" "$token" > "$dir/token.tmp" && mv "$dir/token.tmp" "$dir/token" || { rm -rf "$dir"; echo ERROR; exit 0; }
BAAS_WS_SCRCPY_OWNER="$token" /system/bin/setsid /system/bin/sh -c '
dir=$1; lease=$2; token=$3; name=$4
trap "" HUP
proc_start() { value=$(cat "/proc/$1/stat" 2>/dev/null) || return 1; value=${value##*) }; set -- $value; [ "$#" -ge 20 ] || return 1; printf "%s" "${20}"; }
cleanup() { if [ "$(readlink "$lease" 2>/dev/null)" = "$name" ]; then rm -f "$lease"; fi; rm -rf "$dir"; }
supervisor=$$
supervisor_start=$(proc_start "$supervisor") || { cleanup; exit 1; }
printf "%s %s %s\n" "$token" "$supervisor" "$supervisor_start" > "$dir/init.tmp" && mv "$dir/init.tmp" "$dir/init" || { cleanup; exit 1; }
i=0
while [ "$(readlink "$lease" 2>/dev/null)" != "$name" ]; do [ -f "$dir/stop" ] && { cleanup; exit 0; }; i=$((i+1)); [ "$i" -ge 200 ] && { cleanup; exit 1; }; sleep 0.05; done
BAAS_WS_SCRCPY_OWNER="$token" CLASSPATH=/data/local/tmp/baas-ws-scrcpy-server.jar \
  /system/bin/sh -c "trap '' HUP; kill -STOP \$\$; exec app_process / com.genymobile.scrcpy.Server 1.19-ws7 web ERROR 8886 true" \
  </dev/null > /data/local/tmp/ws_scrcpy.log 2>&1 &
child=$!
i=0
while :; do
  value=$(cat "/proc/$child/stat" 2>/dev/null) || { cleanup; exit 1; }
  value=${value##*) }; set -- $value
  [ "$1" = T ] && break
  [ "$1" = Z ] && { wait "$child" 2>/dev/null; cleanup; exit 1; }
  i=$((i+1)); [ "$i" -ge 200 ] && { kill -KILL "$child" 2>/dev/null; wait "$child" 2>/dev/null; cleanup; exit 1; }
  sleep 0.05
done
child_start=$(proc_start "$child") || { kill -KILL "$child" 2>/dev/null; wait "$child" 2>/dev/null; cleanup; exit 1; }
printf "%s %s %s %s %s\n" "$token" "$supervisor" "$supervisor_start" "$child" "$child_start" > "$dir/meta.tmp" && mv "$dir/meta.tmp" "$dir/meta" || { kill -KILL "$child" 2>/dev/null; wait "$child" 2>/dev/null; cleanup; exit 1; }
kill -CONT "$child" 2>/dev/null || { kill -KILL "$child" 2>/dev/null; wait "$child" 2>/dev/null; cleanup; exit 1; }
while :; do
  current_start=$(proc_start "$child" 2>/dev/null) || { wait "$child" 2>/dev/null; break; }
  [ "$current_start" = "$child_start" ] || break
  if [ "$(cat "$dir/stop" 2>/dev/null)" = "$token" ]; then
    if tr "\000" "\n" < "/proc/$child/environ" 2>/dev/null | grep -Fqx "BAAS_WS_SCRCPY_OWNER=$token" && [ "$(proc_start "$child" 2>/dev/null)" = "$child_start" ]; then kill "$child" 2>/dev/null; fi
    wait "$child" 2>/dev/null
    break
  fi
  value=$(cat "/proc/$child/stat" 2>/dev/null) || { wait "$child" 2>/dev/null; break; }
  value=${value##*) }; set -- $value
  [ "$1" = Z ] && { wait "$child" 2>/dev/null; break; }
  sleep 0.1
done
cleanup
' baas-supervisor "$dir" "$lease" "$token" "$name" </dev/null >/data/local/tmp/baas-ws-scrcpy-supervisor.log 2>&1 &
supervisor=$!
i=0
while [ ! -f "$dir/init" ]; do kill -0 "$supervisor" 2>/dev/null || { rm -rf "$dir"; echo ERROR; exit 0; }; i=$((i+1)); [ "$i" -ge 200 ] && { printf "%s\n" "$token" > "$dir/stop"; echo ERROR; exit 0; }; sleep 0.05; done
ln -s "$name" "$lease" 2>/dev/null || { printf "%s\n" "$token" > "$dir/stop"; echo BUSY; exit 0; }
i=0
while [ ! -f "$dir/meta" ]; do kill -0 "$supervisor" 2>/dev/null || { echo ERROR; exit 0; }; i=$((i+1)); [ "$i" -ge 200 ] && { printf "%s\n" "$token" > "$dir/stop"; echo ERROR; exit 0; }; sleep 0.05; done
printf "OWNED "; cat "$dir/meta"
)SH";
    replace_token(command, owner_token);
    return command;
}

std::string supervisor_stop_command(const std::string_view owner_token)
{
    std::string command = R"SH(# BAAS_WS_SUPERVISOR_STOP
expected=__BAAS_OWNER_TOKEN__
lease=/data/local/tmp/baas-ws-scrcpy.lease
proc_start() { value=$(cat "/proc/$1/stat" 2>/dev/null) || return 1; value=${value##*) }; set -- $value; [ "$#" -ge 20 ] || return 1; printf "%s" "${20}"; }
if [ ! -L "$lease" ]; then echo GONE; exit 0; fi
target=$(readlink "$lease" 2>/dev/null) || { echo OTHER; exit 0; }
case "$target" in baas-ws-scrcpy.owner.*) ;; *) echo OTHER; exit 0;; esac
path_token=${target#baas-ws-scrcpy.owner.}
[ "${#path_token}" -eq 64 ] || { echo OTHER; exit 0; }
case "$path_token" in *[!0-9a-f]*) echo OTHER; exit 0;; esac
dir=/data/local/tmp/$target
read token supervisor supervisor_start < "$dir/init" 2>/dev/null || token=
[ "$token" = "$path_token" ] && [ "$token" = "$expected" ] || { echo OTHER; exit 0; }
actual_start=$(proc_start "$supervisor" 2>/dev/null) || actual_start=
if [ "$actual_start" = "$supervisor_start" ] && tr '\000' '\n' < "/proc/$supervisor/environ" 2>/dev/null | grep -Fqx "BAAS_WS_SCRCPY_OWNER=$token"; then
  printf "%s\n" "$token" > "$dir/stop.tmp" && mv "$dir/stop.tmp" "$dir/stop" && echo REQUESTED || echo ERROR
  exit 0
fi
if [ "$(readlink "$lease" 2>/dev/null)" = "$target" ]; then rm -f "$lease"; fi
rm -rf "$dir"
echo STALE
)SH";
    replace_token(command, owner_token);
    return command;
}

bool expected_cmdline(const std::string_view bytes)
{
    std::vector<std::string_view> fields;
    std::size_t offset{};
    while (offset < bytes.size()) {
        auto end = bytes.find('\0', offset);
        if (end == std::string_view::npos) end = bytes.size();
        if (end != offset) fields.emplace_back(bytes.substr(offset, end - offset));
        offset = end + 1;
    }
    const std::array expected{
        package, version, server_type, log_level,
        std::string_view{"8886"}, all_interfaces};
    const auto found = std::find(fields.begin(), fields.end(), package);
    return found != fields.end()
        && static_cast<std::size_t>(fields.end() - found) == expected.size()
        && std::equal(expected.begin(), expected.end(), found);
}

RemoteBackendError map_resource_error(const channels::ResourceStoreError error)
{
    switch (error) {
        case channels::ResourceStoreError::not_found: return RemoteBackendError::not_found;
        case channels::ResourceStoreError::capacity: return RemoteBackendError::capacity;
        case channels::ResourceStoreError::invalid_data: return RemoteBackendError::invalid_config;
        case channels::ResourceStoreError::none:
        case channels::ResourceStoreError::internal_error:
            return RemoteBackendError::internal_error;
    }
    return RemoteBackendError::internal_error;
}

RemoteIoStatus map_send_failure() noexcept { return RemoteIoStatus::internal_error; }

}  // namespace

namespace {

class ReapedReader {
public:
    virtual ~ReapedReader() = default;
    virtual void reap_reader() noexcept = 0;
private:
    friend class ReaderThreadReaper;
    ReapedReader* reaper_next_{};
};

class ReaderThreadReaper final {
public:
    static ReaderThreadReaper& instance()
    {
        static ReaderThreadReaper reaper;
        return reaper;
    }

    void enqueue(ReapedReader* reader) noexcept
    {
        std::lock_guard lock{mutex_};
        if (tail_) tail_->reaper_next_ = reader;
        else head_ = reader;
        tail_ = reader;
        ready_.notify_one();
    }

private:
    ReaderThreadReaper() : worker_([this] { run(); }) {}
    ~ReaderThreadReaper()
    {
        {
            std::lock_guard lock{mutex_};
            stopping_ = true;
        }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    void run() noexcept
    {
        for (;;) {
            ReapedReader* item{};
            {
                std::unique_lock lock{mutex_};
                ready_.wait(lock, [this] { return stopping_ || head_; });
                if (stopping_ && !head_) return;
                item = head_;
                head_ = head_->reaper_next_;
                if (!head_) tail_ = nullptr;
                item->reaper_next_ = nullptr;
            }
            item->reap_reader();
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    ReapedReader* head_{};
    ReapedReader* tail_{};
    bool stopping_{};
    std::thread worker_;
};

class ProductionRemoteSessionCore final
    : public ReapedReader,
      public std::enable_shared_from_this<ProductionRemoteSessionCore> {
public:
    using Release = std::function<void(
        const std::string&, std::uint16_t, bool,
        std::optional<ServerOwnership>)>;

    ProductionRemoteSessionCore(
        std::string serial, std::unique_ptr<RemoteWebSocketClient> websocket,
        RemoteSessionCallbacks callbacks, const std::size_t max_frame,
        const std::uint16_t local_port, const bool owns_forward,
        std::optional<ServerOwnership> ownership, Release release)
        : serial_(std::move(serial)), websocket_(std::move(websocket)),
          callbacks_(std::move(callbacks)), max_frame_(max_frame),
          local_port_(local_port), owns_forward_(owns_forward),
          ownership_(std::move(ownership)), release_(std::move(release))
    {}

    ~ProductionRemoteSessionCore() { close(); }

    void start()
    {
        // Construct the allocation-free reaper before a reader exists. If its
        // worker cannot be created, open fails while close still has no thread.
        static_cast<void>(ReaderThreadReaper::instance());
        const auto self = shared_from_this();
        std::scoped_lock lock{gate_, reader_mutex_};
        reader_ = std::thread([self] { self->read_loop(); });
        reader_id_ = reader_.get_id();
        reader_started_ = true;
    }

    RemoteIoStatus send(auth::SecretBuffer payload, const std::stop_token stop)
    {
        if (stop.stop_requested() || !enter_send()) return RemoteIoStatus::closed;
        struct Leave { ProductionRemoteSessionCore* self; ~Leave() { self->leave(); } } leave{this};
        if (stop.stop_requested()) return RemoteIoStatus::closed;
        try {
            std::lock_guard send_lock{send_mutex_};
            return websocket_->send_binary(payload.bytes())
                ? RemoteIoStatus::accepted : map_send_failure();
        }
        catch (...) { return RemoteIoStatus::internal_error; }
    }

    void close() noexcept
    {
        bool first{};
        bool reentrant{};
        {
            std::lock_guard lock{gate_};
            reentrant = reader_started_
                && reader_id_ == std::this_thread::get_id();
            if (reentrant) reentrant_close_ = true;
            if (!closing_) { closing_ = true; first = true; }
        }
        if (first) {
            try { static_cast<void>(websocket_->request_close()); } catch (...) {}
            try { websocket_->interrupt(); } catch (...) {}
        }
        // A callback cannot wait for itself to return. Its reader transfers its
        // join handle to ReaderThreadReaper on exit; every external close still
        // waits for the join and complete cleanup barrier below.
        if (reentrant) return;

        std::unique_lock lifecycle{close_mutex_};
        if (close_complete_) return;
        std::thread reader;
        bool started{};
        {
            std::lock_guard lock{reader_mutex_};
            started = reader_started_;
            if (reader_.joinable()) reader = std::move(reader_);
        }
        if (reader.joinable()) {
            reader.join();
            finish_close_locked();
        }
        else if (!started) {
            finish_close_locked();
        }
        else {
            close_completed_.wait(lifecycle, [this] { return close_complete_; });
        }
    }

    void reap_reader() noexcept override
    {
        std::thread reader;
        {
            std::lock_guard lock{reader_mutex_};
            if (reader_.joinable()) reader = std::move(reader_);
        }
        if (reader.joinable()) reader.join();
        {
            std::lock_guard lifecycle{close_mutex_};
            finish_close_locked();
        }
        std::shared_ptr<ProductionRemoteSessionCore> keepalive;
        {
            std::lock_guard lock{reader_mutex_};
            keepalive = std::move(reaper_keepalive_);
        }
    }

private:
    bool enter_send() noexcept
    {
        std::lock_guard lock{gate_};
        if (closing_ || reader_ended_) return false;
        ++active_;
        return true;
    }

    bool enter_callback() noexcept
    {
        std::lock_guard lock{gate_};
        if (closing_) return false;
        ++active_;
        return true;
    }

    void leave() noexcept
    {
        std::lock_guard lock{gate_};
        if (active_ != 0) --active_;
        if (closing_ && active_ == 0) drained_.notify_all();
    }

    void read_loop() noexcept
    {
        struct Exit final {
            ProductionRemoteSessionCore* self;
            ~Exit() { self->reader_exiting(); }
        } exit{this};
        RemoteSessionEnd reason = RemoteSessionEnd::device_closed;
        for (;;) {
            RemoteWebSocketReadResult result;
            try { result = websocket_->read(); }
            catch (...) { result.kind = RemoteWebSocketReadKind::error; }
            if (result.kind == RemoteWebSocketReadKind::closed) break;
            if (result.kind == RemoteWebSocketReadKind::error) {
                reason = RemoteSessionEnd::internal_error;
                break;
            }
            if (result.payload.size() > max_frame_) {
                reason = RemoteSessionEnd::capacity;
                break;
            }
            const auto message_kind = result.kind == RemoteWebSocketReadKind::binary
                ? channels::RemoteDeviceMessageKind::binary
                : channels::RemoteDeviceMessageKind::text;
            if (!enter_callback()) return;
            RemoteIoStatus delivered{RemoteIoStatus::internal_error};
            try {
                delivered = callbacks_.device_bytes(
                    message_kind, std::move(result.payload));
            }
            catch (...) { delivered = RemoteIoStatus::internal_error; }
            leave();
            if (delivered != RemoteIoStatus::accepted) {
                reason = delivered == RemoteIoStatus::capacity
                    ? RemoteSessionEnd::capacity
                    : delivered == RemoteIoStatus::closed
                        ? RemoteSessionEnd::clean
                        : RemoteSessionEnd::internal_error;
                break;
            }
        }
        bool notify{};
        {
            std::lock_guard lock{gate_};
            if (!closing_) {
                reader_ended_ = true;
                ++active_;
                notify = true;
            }
        }
        if (notify) {
            try { callbacks_.ended(reason); } catch (...) {}
            leave();
        }
    }

    void reader_exiting() noexcept
    {
        {
            std::lock_guard lock{gate_};
            if (!reentrant_close_) return;
        }
        std::lock_guard lock{reader_mutex_};
        if (!reader_.joinable()
            || reader_.get_id() != std::this_thread::get_id()) return;
        reaper_keepalive_ = shared_from_this();
        ReaderThreadReaper::instance().enqueue(this);
    }

    void finish_close_locked() noexcept
    {
        if (close_complete_) return;
        {
            std::unique_lock lock{gate_};
            drained_.wait(lock, [this] { return active_ == 0; });
        }
        cleanup_once();
        close_complete_ = true;
        close_completed_.notify_all();
    }

    void cleanup_once() noexcept
    {
        if (cleaned_.exchange(true, std::memory_order_acq_rel)) return;
        try { release_(serial_, local_port_, owns_forward_, ownership_); }
        catch (...) {}
    }

    std::string serial_;
    std::unique_ptr<RemoteWebSocketClient> websocket_;
    RemoteSessionCallbacks callbacks_;
    std::size_t max_frame_{};
    std::uint16_t local_port_{};
    bool owns_forward_{};
    std::optional<ServerOwnership> ownership_;
    Release release_;
    std::mutex gate_;
    std::mutex send_mutex_;
    std::mutex close_mutex_;
    std::mutex reader_mutex_;
    std::condition_variable drained_;
    std::condition_variable close_completed_;
    std::size_t active_{};
    bool closing_{};
    bool reader_ended_{};
    bool reader_started_{};
    bool reentrant_close_{};
    bool close_complete_{};
    std::thread::id reader_id_;
    std::thread reader_;
    std::shared_ptr<ProductionRemoteSessionCore> reaper_keepalive_;
    std::atomic_bool cleaned_{};
};

class ProductionRemoteSession final : public channels::RemoteSession {
public:
    explicit ProductionRemoteSession(std::shared_ptr<ProductionRemoteSessionCore> core)
        : core_(std::move(core)) {}
    ~ProductionRemoteSession() override { close(); }
    RemoteIoStatus send_to_device(
        auth::SecretBuffer payload, const std::stop_token stop) override
    { return core_->send(std::move(payload), stop); }
    void close() noexcept override { core_->close(); }
private:
    std::shared_ptr<ProductionRemoteSessionCore> core_;
};

}  // namespace

class ProductionRemoteBackend::Impl
    : public std::enable_shared_from_this<ProductionRemoteBackend::Impl> {
public:
    struct PendingOpen final {
        Impl* owner;
        const std::string* serial;
        std::uint16_t local_port{};
        bool owns_forward{};
        std::optional<ServerOwnership> ownership;
        bool active{true};
        ~PendingOpen()
        {
            if (!active) return;
            owner->cleanup(*serial, local_port, owns_forward, ownership);
            owner->unreserve(*serial);
        }
    };

    Impl(ProductionRemoteBackendDependencies dependencies,
         ProductionRemoteBackendLimits limits)
        : dependencies_(std::move(dependencies)), limits_(limits)
    {
        if (!dependencies_.adb) {
            dependencies_.adb = std::make_shared<ConcreteAdbClient>(
                dependencies_.adb_transport);
        }
        if (!dependencies_.websocket_factory) {
            dependencies_.websocket_factory = [](std::string url,
                const std::chrono::milliseconds connect,
                const std::chrono::milliseconds write) {
                return std::make_unique<HttplibRemoteWebSocketClient>(
                    std::move(url), connect, write);
            };
        }
        if (!dependencies_.clock) dependencies_.clock = [] {
            return std::chrono::steady_clock::now();
        };
        if (!dependencies_.sleep) dependencies_.sleep = [](const auto duration) {
            std::this_thread::sleep_for(duration);
        };
        if (!dependencies_.owner_token_factory)
            dependencies_.owner_token_factory = make_owner_token;
    }

    RemoteOpenResult open(
        std::optional<std::string> config_id, RemoteSessionCallbacks callbacks,
        const std::stop_token stop)
    {
        if (!config_id || config_id->empty() || stop.stop_requested())
            return {nullptr, RemoteBackendError::invalid_config};
        auto snapshot = dependencies_.resources->pull(
            {channels::SyncResource::config, *config_id}, stop);
        if (!snapshot) return {nullptr, map_resource_error(snapshot.error)};
        auto config = parse_device_config(snapshot->data_json, limits_.max_config_bytes);
        if (!config) return {nullptr, RemoteBackendError::invalid_config};
        if (!reserve(config->serial)) return {nullptr, RemoteBackendError::capacity};
        struct OpenGuard final {
            Impl* owner;
            ~OpenGuard() { owner->finish_open(); }
        } open_guard{this};
        PendingOpen pending{this, &config->serial};

        const auto fail = [](RemoteBackendError error) {
            return RemoteOpenResult{nullptr, error};
        };
        if (stop.stop_requested()) return fail(RemoteBackendError::internal_error);
        const auto state = dependencies_.adb->get_state(config->serial, stop);
        if (!state) return fail(map_adb_error(state.error));
        if (*state.value != "device") return fail(RemoteBackendError::not_found);

        const auto lease = dependencies_.adb->shell(
            config->serial, lease_probe_command(), stop);
        if (!lease) return fail(map_adb_error(lease.error));
        if (lease->starts_with("BUSY")) return fail(RemoteBackendError::capacity);
        if (!lease->starts_with("NONE") && !lease->starts_with("STALE"))
            return fail(RemoteBackendError::internal_error);

        std::optional<ServerOwnership> ownership;
        auto server = find_server(config->serial, stop);
        if (server.error != RemoteBackendError::none)
            return fail(server.error);
        if (server.pid) {
            // The first lease probe and process discovery are separate ADB
            // round trips. Another backend can acquire the device lease and
            // exec its child between them, so a discovered process is legacy
            // only after a second, post-discovery lease probe says so.
            const auto current_lease = dependencies_.adb->shell(
                config->serial, lease_probe_command(), stop);
            if (!current_lease)
                return fail(map_adb_error(current_lease.error));
            if (current_lease->starts_with("BUSY"))
                return fail(RemoteBackendError::capacity);
            if (!current_lease->starts_with("NONE")
                && !current_lease->starts_with("STALE")) {
                return fail(RemoteBackendError::internal_error);
            }
        }
        if (!server.pid) {
            std::optional<std::string> token;
            try { token = dependencies_.owner_token_factory(); }
            catch (...) { return fail(RemoteBackendError::internal_error); }
            if (!token || !valid_owner_token(*token))
                return fail(RemoteBackendError::internal_error);
            ServerOwnership launched{std::move(*token)};
            const auto pushed = dependencies_.adb->push_file(
                config->serial, remote_jar, dependencies_.server_jar, stop);
            if (!pushed) return fail(map_adb_error(pushed.error));
            const auto started = dependencies_.adb->shell(
                config->serial, supervisor_launch_command(launched.token), stop);
            if (!started) {
                // The device supervisor may own the lease even when the host
                // response is lost. Request token-bound stop; never kill a PID
                // inferred from an uncertain transport response.
                cleanup_owned(config->serial, launched);
                return fail(map_adb_error(started.error));
            }
            if (started->starts_with("BUSY"))
                return fail(RemoteBackendError::capacity);
            const auto record = parse_lease_record(*started.value, launched.token);
            if (!record) {
                cleanup_owned(config->serial, launched);
                return fail(RemoteBackendError::internal_error);
            }
            launched = *record;
            pending.ownership = launched;
            const auto ready = wait_for_owned_server(
                config->serial, launched, stop);
            if (!ready)
                return fail(RemoteBackendError::internal_error);
            ownership = launched;
        }

        std::uint16_t local_port{};
        bool owns_forward{};
        const auto forwards = dependencies_.adb->list_forwards(stop);
        if (!forwards) return fail(map_adb_error(forwards.error));
        for (const auto& item : *forwards.value) {
            if (item.serial != config->serial || item.remote != "tcp:8886") continue;
            constexpr std::string_view prefix = "tcp:";
            if (!item.local.starts_with(prefix)) continue;
            const auto parsed = parse_port(
                std::string_view{item.local}.substr(prefix.size()));
            if (parsed && (local_port == 0 || *parsed < local_port)) local_port = *parsed;
        }
        if (local_port == 0) {
            const auto forwarded = dependencies_.adb->forward_tcp_zero(
                config->serial, server_port, stop);
            if (!forwarded) return fail(map_adb_error(forwarded.error));
            local_port = *forwarded.value;
            owns_forward = true;
            pending.local_port = local_port;
            pending.owns_forward = true;
        }

        auto websocket = connect_websocket(local_port, stop);
        if (!websocket) return fail(RemoteBackendError::internal_error);

        std::shared_ptr<ProductionRemoteSessionCore> core;
        try {
            const auto weak = weak_from_this();
            core = std::make_shared<ProductionRemoteSessionCore>(
                config->serial, std::move(websocket), std::move(callbacks),
                limits_.max_device_frame_bytes, local_port, owns_forward, ownership,
                [weak](const std::string& serial, const std::uint16_t port,
                       const bool own_forward,
                       const std::optional<ServerOwnership> owned) {
                    if (const auto self = weak.lock())
                        self->release(serial, port, own_forward, owned);
                });
            pending.active = false;
            {
                std::lock_guard lock{mutex_};
                if (stopped_) throw std::runtime_error("remote backend stopped");
                sessions_.emplace_back(core);
            }
            core->start();
            return {std::make_unique<ProductionRemoteSession>(std::move(core)),
                    RemoteBackendError::none};
        }
        catch (...) {
            if (core) core->close();
            return {nullptr, RemoteBackendError::internal_error};
        }
    }

    void stop() noexcept
    {
        std::vector<std::shared_ptr<ProductionRemoteSessionCore>> live;
        {
            std::unique_lock lock{mutex_};
            if (stopped_) {
                stop_drained_.wait(lock, [this] { return stop_complete_; });
                return;
            }
            stopped_ = true;
            opens_drained_.wait(lock, [this] { return opening_ == 0; });
            for (auto& weak : sessions_) if (auto item = weak.lock())
                live.emplace_back(std::move(item));
        }
        for (auto& session : live) session->close();
        dependencies_.adb->stop();
        {
            std::lock_guard lock{mutex_};
            stop_complete_ = true;
        }
        stop_drained_.notify_all();
    }

private:
    struct ServerSearch {
        std::optional<unsigned> pid;
        RemoteBackendError error{RemoteBackendError::none};
    };

    bool reserve(const std::string& serial)
    {
        std::lock_guard lock{mutex_};
        if (stopped_ || serials_.contains(serial)
            || serials_.size() >= limits_.max_sessions) return false;
        serials_.emplace(serial);
        ++opening_;
        return true;
    }

    void finish_open() noexcept
    {
        std::lock_guard lock{mutex_};
        if (opening_ != 0) --opening_;
        if (stopped_ && opening_ == 0) opens_drained_.notify_all();
    }

    void unreserve(const std::string& serial) noexcept
    {
        std::lock_guard lock{mutex_};
        serials_.erase(serial);
    }

    static RemoteBackendError map_adb_error(const AdbTransportError error)
    {
        switch (error) {
            case AdbTransportError::capacity: return RemoteBackendError::capacity;
            case AdbTransportError::invalid_argument: return RemoteBackendError::invalid_config;
            case AdbTransportError::none:
            case AdbTransportError::connection_failed:
            case AdbTransportError::timeout:
            case AdbTransportError::cancelled:
            case AdbTransportError::protocol_error:
            case AdbTransportError::adb_fail:
            case AdbTransportError::local_io_error:
            case AdbTransportError::closed:
            case AdbTransportError::internal_error:
                return RemoteBackendError::internal_error;
        }
        return RemoteBackendError::internal_error;
    }

    bool is_expected_pid(const std::string& serial, const unsigned pid,
                         const std::stop_token stop)
    {
        const auto command = "cat /proc/" + std::to_string(pid) + "/cmdline";
        const auto result = dependencies_.adb->shell(serial, command, stop);
        return result && expected_cmdline(*result.value);
    }

    bool is_owned_process(
        const std::string& serial, const ServerOwnership& ownership,
        const unsigned pid, const std::stop_token stop)
    {
        if (!is_expected_pid(serial, pid, stop)) return false;
        const auto stat = dependencies_.adb->shell(
            serial, "cat /proc/" + std::to_string(pid) + "/stat", stop);
        if (!stat || parse_process_start_time(*stat.value)
                != std::optional<std::uint64_t>{ownership.child_start_time}) {
            return false;
        }
        const auto result = dependencies_.adb->shell(
            serial, "cat /proc/" + std::to_string(pid) + "/environ", stop);
        return result && expected_owner_environment(
            *result.value, ownership.token);
    }

    ServerSearch find_server(const std::string& serial, const std::stop_token stop)
    {
        const auto result = dependencies_.adb->shell(serial, "ps -A -o PID,ARGS", stop);
        if (!result) return {{}, map_adb_error(result.error)};
        std::size_t candidates{};
        std::size_t offset{};
        while (offset < result->size()) {
            auto end = result->find('\n', offset);
            if (end == std::string::npos) end = result->size();
            const std::string_view line{result->data() + offset, end - offset};
            offset = end + 1;
            if (line.find(package) == std::string_view::npos
                || line.find(version) == std::string_view::npos) continue;
            if (++candidates > limits_.max_process_candidates)
                return {{}, RemoteBackendError::capacity};
            const auto pid = parse_pid(line);
            if (pid && is_expected_pid(serial, *pid, stop)) return {*pid, {}};
        }
        return {};
    }

    bool wait_for_owned_server(
        const std::string& serial, const ServerOwnership& ownership,
        const std::stop_token stop)
    {
        if (ownership.child_pid == 0 || ownership.child_start_time == 0)
            return false;
        const auto deadline = dependencies_.clock() + limits_.startup_timeout;
        while (!stop.stop_requested() && dependencies_.clock() < deadline) {
            if (is_owned_process(
                    serial, ownership, ownership.child_pid, stop)) return true;
            dependencies_.sleep(limits_.startup_poll_interval);
        }
        return false;
    }

    std::unique_ptr<RemoteWebSocketClient> connect_websocket(
        const std::uint16_t local_port, const std::stop_token stop)
    {
        const auto deadline = dependencies_.clock() + limits_.startup_timeout;
        for (;;) {
            const auto now = dependencies_.clock();
            if (stop.stop_requested() || now >= deadline) break;
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            if (remaining.count() <= 0) remaining = std::chrono::milliseconds{1};
            const auto connect_timeout = (std::min)(
                limits_.websocket_connect_timeout, remaining);
            std::unique_ptr<RemoteWebSocketClient> websocket;
            try {
                websocket = dependencies_.websocket_factory(
                    "ws://127.0.0.1:" + std::to_string(local_port) + '/',
                    connect_timeout,
                    limits_.websocket_write_timeout);
                if (websocket && websocket->connect()) return websocket;
            }
            catch (...) {}
            if (stop.stop_requested()) break;
            dependencies_.sleep(limits_.startup_poll_interval);
        }
        return {};
    }

    void cleanup(const std::string& serial, const std::uint16_t local_port,
                 const bool owns_forward,
                 const std::optional<ServerOwnership> ownership) noexcept
    {
        try {
            if (owns_forward && local_port != 0) {
                const auto forwards = dependencies_.adb->list_forwards({});
                if (forwards && std::any_of(forwards->begin(), forwards->end(),
                    [&](const adb::AdbForwardItem& item) {
                        return item.serial == serial
                            && item.local == "tcp:" + std::to_string(local_port)
                            && item.remote == "tcp:8886";
                    })) {
                    static_cast<void>(dependencies_.adb->remove_tcp_forward(
                        serial, local_port, {}));
                }
            }
            if (ownership) cleanup_owned(serial, *ownership);
        }
        catch (...) {}
    }

    void cleanup_owned(
        const std::string& serial, const ServerOwnership& ownership) noexcept
    {
        try {
            const auto deadline = dependencies_.clock() + limits_.startup_timeout;
            for (;;) {
                const auto result = dependencies_.adb->shell(
                    serial, supervisor_stop_command(ownership.token), {});
                if (result && (result->starts_with("GONE")
                    || result->starts_with("STALE")
                    || result->starts_with("OTHER"))) return;
                if (dependencies_.clock() >= deadline) return;
                dependencies_.sleep(limits_.startup_poll_interval);
            }
        }
        catch (...) {}
    }

    void release(const std::string& serial, const std::uint16_t local_port,
                 const bool owns_forward,
                 const std::optional<ServerOwnership> ownership) noexcept
    {
        cleanup(serial, local_port, owns_forward, ownership);
        unreserve(serial);
        std::lock_guard lock{mutex_};
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->expired()) it = sessions_.erase(it); else ++it;
        }
    }

    ProductionRemoteBackendDependencies dependencies_;
    ProductionRemoteBackendLimits limits_;
    std::mutex mutex_;
    std::condition_variable opens_drained_;
    std::condition_variable stop_drained_;
    bool stopped_{};
    bool stop_complete_{};
    std::size_t opening_{};
    std::unordered_set<std::string> serials_;
    std::vector<std::weak_ptr<ProductionRemoteSessionCore>> sessions_;
};

ProductionRemoteBackend::ProductionRemoteBackend(
    ProductionRemoteBackendDependencies dependencies,
    const ProductionRemoteBackendLimits limits)
{
    if (!dependencies.resources || (!dependencies.adb && !dependencies.adb_transport)
        || dependencies.server_jar.empty() || limits.max_sessions == 0
        || limits.max_config_bytes == 0 || limits.max_device_frame_bytes == 0
        || limits.max_process_candidates == 0 || limits.startup_timeout.count() <= 0
        || limits.startup_poll_interval.count() <= 0
        || limits.websocket_connect_timeout.count() <= 0
        || limits.websocket_write_timeout.count() <= 0) {
        throw std::invalid_argument("invalid production remote backend configuration");
    }
    impl_ = std::make_shared<Impl>(std::move(dependencies), limits);
}

ProductionRemoteBackend::~ProductionRemoteBackend() { stop(); }

RemoteOpenResult ProductionRemoteBackend::open(
    std::optional<std::string> config_id, RemoteSessionCallbacks callbacks,
    const std::stop_token stop)
{
    if (!callbacks.device_bytes || !callbacks.ended)
        return {nullptr, RemoteBackendError::invalid_config};
    try {
        return impl_->open(std::move(config_id), std::move(callbacks), stop);
    }
    catch (...) {
        return {nullptr, RemoteBackendError::internal_error};
    }
}

void ProductionRemoteBackend::stop() noexcept
{
    if (impl_) impl_->stop();
}

}  // namespace baas::service::app
