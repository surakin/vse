// The net.* settings (Phase 20 Day 281): they are declared once, default to the structs' own defaults and reach the
// configurations of the transports, the stream and the simulated links.

#include <catch2/catch_test_macros.hpp>

#include "net/net_settings.h"
#include "settings/registry.h"

namespace engine_placeholder::net
{

TEST_CASE("Net settings: the defaults are the defaults of the configuration structs", "[net][settings]")
{
    settings::Registry registry;
    RegisterNetSettings(&registry);
    RegisterNetSettings(&registry);  // twice is harmless
    const UdpTransportConfig udp = UdpConfigFromSettings(registry);
    const UdpTransportConfig udp_default;
    CHECK(udp.channels == udp_default.channels);
    CHECK(udp.max_connections == udp_default.max_connections);
    CHECK(udp.max_connections_per_address == udp_default.max_connections_per_address);
    CHECK(udp.max_message_bytes == udp_default.max_message_bytes);
    CHECK(udp.timeout_nanos == udp_default.timeout_nanos);
    CHECK(udp.connect_timeout_nanos == udp_default.connect_timeout_nanos);
    CHECK(udp.max_total_receive_buffer_bytes == udp_default.max_total_receive_buffer_bytes);
    CHECK(udp.handshake_limits.address_per_second == udp_default.handshake_limits.address_per_second);
    const TcpTransportConfig tcp = TcpConfigFromSettings(registry);
    const TcpTransportConfig tcp_default;
    CHECK(tcp.max_connections_per_address == tcp_default.max_connections_per_address);
    CHECK(tcp.max_send_queue_bytes == tcp_default.max_send_queue_bytes);
    CHECK(tcp.ping_interval_nanos == tcp_default.ping_interval_nanos);
    CHECK(tcp.accept_limits.address_per_second == tcp_default.accept_limits.address_per_second);
    const TcpStreamOptions stream = StreamOptionsFromSettings(registry);
    const TcpStreamOptions stream_default;
    CHECK(stream.max_frame_bytes == stream_default.max_frame_bytes);
    CHECK(stream.send_queue_bytes == stream_default.send_queue_bytes);
    const LinkConfig link = LinkFromSettings(registry);
    CHECK(link.latency_nanos == 0);
    CHECK(link.loss == 0.0);
}

TEST_CASE("Net settings: a changed value reaches the configurations", "[net][settings]")
{
    settings::Registry registry;
    RegisterNetSettings(&registry);
    registry.Publish(registry.Find(StringView("net.max_connections")), settings::Value::FromInt(77));
    registry.Publish(registry.Find(StringView("net.max_connections_per_address")), settings::Value::FromInt(5));
    registry.Publish(registry.Find(StringView("net.timeout_ms")), settings::Value::FromInt(2500));
    registry.Publish(registry.Find(StringView("net.handshake_rate_per_address")), settings::Value::FromFloat(7.5));
    registry.Publish(registry.Find(StringView("net.accept_rate_total")), settings::Value::FromFloat(0));
    registry.Publish(registry.Find(StringView("net.max_bandwidth_bytes_per_second")), settings::Value::FromInt(123456));
    registry.Publish(registry.Find(StringView("net.simulated_latency_ms")), settings::Value::FromInt(40));
    registry.Publish(registry.Find(StringView("net.simulated_loss_percent")), settings::Value::FromFloat(2.5));
    const UdpTransportConfig udp = UdpConfigFromSettings(registry);
    CHECK(udp.max_connections == 77);
    CHECK(udp.max_connections_per_address == 5);
    CHECK(udp.timeout_nanos == 2'500'000'000ull);
    CHECK(udp.handshake_limits.address_per_second == 7.5);
    CHECK(udp.max_bandwidth_bytes_per_second == 123456);
    const TcpTransportConfig tcp = TcpConfigFromSettings(registry);
    CHECK(tcp.max_connections == 77);
    CHECK(tcp.max_connections_per_address == 5);
    CHECK(tcp.timeout_nanos == 2'500'000'000ull);
    CHECK(tcp.accept_limits.total_per_second == 0.0);
    const LinkConfig link = LinkFromSettings(registry);
    CHECK(link.latency_nanos == 40'000'000ull);
    CHECK(link.loss == 0.025);
}

TEST_CASE("Net settings: a short time-out keeps the keep-alive and ping inside it", "[net][settings]")
{
    settings::Registry registry;
    RegisterNetSettings(&registry);
    registry.Publish(registry.Find(StringView("net.timeout_ms")), settings::Value::FromInt(500));
    const UdpTransportConfig udp = UdpConfigFromSettings(registry);
    const TcpTransportConfig tcp = TcpConfigFromSettings(registry);
    CHECK(udp.keepalive_nanos <= udp.timeout_nanos / 4);
    CHECK(tcp.ping_interval_nanos <= tcp.timeout_nanos / 4);
}

}  // namespace engine_placeholder::net
