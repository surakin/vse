#include "net/net_settings.h"

#include "settings/desc.h"
#include "settings/setting.h"

namespace vse::net
{
namespace
{

void Declare(settings::Registry* registry, settings::SettingDesc d, const char* env, const char* flag,
             const char* label, const char* description)
{
    if (registry->Find(StringView(d.key)).IsValid())
    {
        return;
    }
    d.env = env;
    d.flag = flag;
    d.label = label;
    d.description = description;
    d.group = "net";
    d.flags = settings::kAdvanced;
    registry->Register(d);
}

i64 Int(const settings::Registry& registry, const char* key)
{
    return settings::Setting<i64>::Find(registry, StringView(key)).Get();
}

f64 Real(const settings::Registry& registry, const char* key)
{
    return settings::Setting<f64>::Find(registry, StringView(key)).Get();
}

u64 Millis(const settings::Registry& registry, const char* key)
{
    return static_cast<u64>(Int(registry, key)) * 1'000'000ull;
}

}  // namespace

void RegisterNetSettings(settings::Registry* registry)
{
    const UdpTransportConfig udp;
    const TcpTransportConfig tcp;
    const TcpStreamOptions stream;
    const RateLimiterConfig hs = DefaultHandshakeLimits();
    const RateLimiterConfig acc = DefaultAcceptLimits();
    Declare(registry, settings::MakeInt("net.channels", udp.channels, 1, 255), "ENGINE_NET_CHANNELS", "net-channels",
            "Channels per connection", "channels a connection offers; a lower channel number is a higher priority");
    Declare(registry, settings::MakeInt("net.max_connections", static_cast<i64>(udp.max_connections), 1, 1 << 20),
            "ENGINE_NET_MAX_CONNECTIONS", "net-max-connections", "Most connections",
            "connections at once, both kinds; more are refused or closed on arrival");
    Declare(
        registry, settings::MakeInt("net.max_connections_per_address", 0, 0, 1 << 20),
        "ENGINE_NET_MAX_CONNECTIONS_PER_ADDRESS", "net-max-connections-per-address", "Most connections per address",
        "connections accepted from one address (an IPv6 /64 counts as one); 0 takes each transport's default (UDP 8, TCP 256)");
    Declare(registry, settings::MakeInt("net.max_message_bytes", static_cast<i64>(udp.max_message_bytes), 64, 1 << 26),
            "ENGINE_NET_MAX_MESSAGE_BYTES", "net-max-message-bytes", "Largest message",
            "the most bytes of one message; larger ones are refused on send and ends the connection on receive");
    Declare(registry, settings::MakeInt("net.max_frame_bytes", static_cast<i64>(stream.max_frame_bytes), 64, 1 << 28),
            "ENGINE_NET_MAX_FRAME_BYTES", "net-max-frame-bytes", "Largest frame",
            "the most bytes of one frame of a TCP stream");
    Declare(
        registry, settings::MakeInt("net.send_queue_bytes", 0, 0, 1 << 30), "ENGINE_NET_SEND_QUEUE_BYTES",
        "net-send-queue-bytes", "Send queue",
        "bytes waiting to be written per TCP stream or connection; a full queue is backpressure; 0 takes each one's default (stream 256 KiB, transport 8 MiB)");
    Declare(registry,
            settings::MakeInt("net.receive_buffer_bytes", static_cast<i64>(stream.receive_buffer_bytes), 64, 1 << 26),
            "ENGINE_NET_RECEIVE_BUFFER_BYTES", "net-receive-buffer-bytes", "Read buffer",
            "bytes a TCP stream reads from its socket per call");
    Declare(
        registry,
        settings::MakeInt("net.connection_budget_bytes", static_cast<i64>(udp.max_total_receive_buffer_bytes), 1024,
                          static_cast<i64>(1) << 40),
        "ENGINE_NET_CONNECTION_BUDGET_BYTES", "net-connection-budget-bytes", "Memory budget over all connections",
        "bytes held for reassembly, reordering and queued output over all connections; a connection that would pass it is ended or refused");
    Declare(registry,
            settings::MakeInt("net.timeout_ms", static_cast<i64>(udp.timeout_nanos / 1'000'000), 100, 3600000),
            "ENGINE_NET_TIMEOUT_MS", "net-timeout-ms", "Silence time-out",
            "milliseconds of silence after which a connection times out");
    Declare(registry,
            settings::MakeInt("net.connect_timeout_ms", static_cast<i64>(udp.connect_timeout_nanos / 1'000'000), 100,
                              3600000),
            "ENGINE_NET_CONNECT_TIMEOUT_MS", "net-connect-timeout-ms", "Connect time-out",
            "milliseconds a handshake may take");
    Declare(
        registry,
        settings::MakeInt("net.ping_interval_ms", static_cast<i64>(tcp.ping_interval_nanos / 1'000'000), 10, 600000),
        "ENGINE_NET_PING_INTERVAL_MS", "net-ping-interval-ms", "Ping interval",
        "milliseconds between the pings of a TCP connection (round-trip samples and keep-alive)");
    Declare(registry, settings::MakeInt("net.max_bandwidth_bytes_per_second", 0, 0, static_cast<i64>(1) << 40),
            "ENGINE_NET_MAX_BANDWIDTH_BYTES_PER_SECOND", "net-max-bandwidth-bytes-per-second", "Send rate cap",
            "cap on the send rate of one UDP connection; 0 for none (the congestion window still applies)");
    Declare(registry, settings::MakeFloat("net.handshake_rate_per_address", hs.address_per_second, 0, 1e9),
            "ENGINE_NET_HANDSHAKE_RATE_PER_ADDRESS", "net-handshake-rate-per-address",
            "Handshakes per second per address",
            "UDP connection requests answered per second for one address; 0 for no limit; the excess is dropped");
    Declare(registry, settings::MakeFloat("net.handshake_rate_total", hs.total_per_second, 0, 1e9),
            "ENGINE_NET_HANDSHAKE_RATE_TOTAL", "net-handshake-rate-total", "Handshakes per second in total",
            "UDP connection requests answered per second over all addresses; 0 for no limit");
    Declare(registry, settings::MakeFloat("net.accept_rate_per_address", acc.address_per_second, 0, 1e9),
            "ENGINE_NET_ACCEPT_RATE_PER_ADDRESS", "net-accept-rate-per-address", "Accepts per second per address",
            "TCP streams accepted per second from one address; 0 for no limit; the excess is closed on arrival");
    Declare(registry, settings::MakeFloat("net.accept_rate_total", acc.total_per_second, 0, 1e9),
            "ENGINE_NET_ACCEPT_RATE_TOTAL", "net-accept-rate-total", "Accepts per second in total",
            "TCP streams accepted per second over all addresses; 0 for no limit");
    Declare(registry,
            settings::MakeInt("net.ban_seconds", static_cast<i64>(acc.ban_nanos / 1'000'000'000ull), 1, 86400 * 30),
            "ENGINE_NET_BAN_SECONDS", "net-ban-seconds", "Ban length",
            "seconds an address stays banned after repeated failed TCP handshakes");
    Declare(registry, settings::MakeInt("net.simulated_latency_ms", 0, 0, 60000), "ENGINE_NET_SIMULATED_LATENCY_MS",
            "net-simulated-latency-ms", "Simulated latency", "one-way delay of the simulated network's links (tests)");
    Declare(registry, settings::MakeInt("net.simulated_jitter_ms", 0, 0, 60000), "ENGINE_NET_SIMULATED_JITTER_MS",
            "net-simulated-jitter-ms", "Simulated jitter", "extra random delay of the simulated links (tests)");
    Declare(registry, settings::MakeFloat("net.simulated_loss_percent", 0, 0, 100), "ENGINE_NET_SIMULATED_LOSS_PERCENT",
            "net-simulated-loss-percent", "Simulated loss", "percent of packets the simulated links drop (tests)");
    Declare(registry, settings::MakeInt("net.simulated_bandwidth_bps", 0, 0, static_cast<i64>(1) << 40),
            "ENGINE_NET_SIMULATED_BANDWIDTH_BPS", "net-simulated-bandwidth-bps", "Simulated bandwidth",
            "bits per second the simulated links carry; 0 for no limit (tests)");
}

UdpTransportConfig UdpConfigFromSettings(const settings::Registry& registry)
{
    UdpTransportConfig c;
    c.channels = static_cast<u8>(Int(registry, "net.channels"));
    c.max_connections = static_cast<usize>(Int(registry, "net.max_connections"));
    if (const i64 per = Int(registry, "net.max_connections_per_address"); per != 0)
    {
        c.max_connections_per_address = static_cast<usize>(per);
    }
    c.max_message_bytes = static_cast<usize>(Int(registry, "net.max_message_bytes"));
    c.max_total_receive_buffer_bytes = static_cast<usize>(Int(registry, "net.connection_budget_bytes"));
    c.timeout_nanos = Millis(registry, "net.timeout_ms");
    if (c.keepalive_nanos > c.timeout_nanos / 4)
    {
        c.keepalive_nanos = c.timeout_nanos / 4;  // an idle, healthy connection must be kept alive inside its time-out
    }
    c.connect_timeout_nanos = Millis(registry, "net.connect_timeout_ms");
    c.max_bandwidth_bytes_per_second = static_cast<u64>(Int(registry, "net.max_bandwidth_bytes_per_second"));
    c.handshake_limits.address_per_second = Real(registry, "net.handshake_rate_per_address");
    c.handshake_limits.address_burst = c.handshake_limits.address_per_second;
    c.handshake_limits.total_per_second = Real(registry, "net.handshake_rate_total");
    c.handshake_limits.total_burst = c.handshake_limits.total_per_second / 10;
    return c;
}

TcpTransportConfig TcpConfigFromSettings(const settings::Registry& registry)
{
    TcpTransportConfig c;
    c.channels = static_cast<u8>(Int(registry, "net.channels"));
    c.max_connections = static_cast<usize>(Int(registry, "net.max_connections"));
    if (const i64 per = Int(registry, "net.max_connections_per_address"); per != 0)
    {
        c.max_connections_per_address = static_cast<usize>(per);
    }
    c.max_message_bytes = static_cast<usize>(Int(registry, "net.max_message_bytes"));
    if (const i64 q = Int(registry, "net.send_queue_bytes"); q != 0)
    {
        c.max_send_queue_bytes = static_cast<usize>(q);
    }
    c.max_total_send_queue_bytes = static_cast<usize>(Int(registry, "net.connection_budget_bytes"));
    c.timeout_nanos = Millis(registry, "net.timeout_ms");
    c.handshake_timeout_nanos = Millis(registry, "net.connect_timeout_ms");
    c.ping_interval_nanos = Millis(registry, "net.ping_interval_ms");
    if (c.ping_interval_nanos > c.timeout_nanos / 4)
    {
        c.ping_interval_nanos = c.timeout_nanos / 4;  // pings must arrive inside the time-out
    }
    c.accept_limits.address_per_second = Real(registry, "net.accept_rate_per_address");
    c.accept_limits.address_burst = c.accept_limits.address_per_second;
    c.accept_limits.total_per_second = Real(registry, "net.accept_rate_total");
    c.accept_limits.total_burst = c.accept_limits.total_per_second / 5;
    c.accept_limits.ban_nanos = static_cast<u64>(Int(registry, "net.ban_seconds")) * 1'000'000'000ull;
    return c;
}

TcpStreamOptions StreamOptionsFromSettings(const settings::Registry& registry)
{
    TcpStreamOptions o;
    o.max_frame_bytes = static_cast<usize>(Int(registry, "net.max_frame_bytes"));
    o.receive_buffer_bytes = static_cast<usize>(Int(registry, "net.receive_buffer_bytes"));
    if (const i64 q = Int(registry, "net.send_queue_bytes"); q != 0)
    {
        o.send_queue_bytes = static_cast<usize>(q);
    }
    o.idle_timeout_nanos = static_cast<i64>(Millis(registry, "net.timeout_ms"));
    o.connect_timeout_nanos = static_cast<i64>(Millis(registry, "net.connect_timeout_ms"));
    return o;
}

LinkConfig LinkFromSettings(const settings::Registry& registry)
{
    LinkConfig l;
    l.latency_nanos = Millis(registry, "net.simulated_latency_ms");
    l.jitter_nanos = Millis(registry, "net.simulated_jitter_ms");
    l.loss = Real(registry, "net.simulated_loss_percent") / 100.0;
    l.bandwidth_bps = static_cast<u64>(Int(registry, "net.simulated_bandwidth_bps"));
    return l;
}

}  // namespace vse::net
