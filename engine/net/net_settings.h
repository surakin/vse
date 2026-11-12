#pragma once

//! The networking settings (Phase 20 Day 281): [[net::RegisterNetSettings]] declares every `net.*` option once, and the
//! `...FromSettings` functions turn the current values into the configuration structs of the transports, the TCP stream
//! and the simulated network. Nothing in `engine/net` reads the environment or the command line itself
//! (`config_access_check`): a user sets `net.max_connections` in a settings file, in `ENGINE_NET_MAX_CONNECTIONS` or
//! with
//! `--net-max-connections`.

#include "net/sim_network.h"
#include "net/tcp_stream.h"
#include "net/tcp_transport.h"
#include "net/udp_transport.h"
#include "settings/registry.h"
#include "types.h"

namespace engine_placeholder::net
{

/// Declares the `net.*` settings, each with an `ENGINE_NET_*` variable and a `--net-*` flag:
/// `net.channels`, `net.max_connections`, `net.max_connections_per_address`, `net.max_message_bytes`,
/// `net.max_frame_bytes`, `net.send_queue_bytes`, `net.receive_buffer_bytes`, `net.connection_budget_bytes` (the global
/// receive and send budgets), `net.timeout_ms`, `net.connect_timeout_ms`, `net.ping_interval_ms`,
/// `net.max_bandwidth_bytes_per_second`, `net.handshake_rate_per_address`, `net.handshake_rate_total`,
/// `net.accept_rate_per_address`, `net.accept_rate_total` (requests per second; 0 is no limit), `net.ban_seconds`, and
/// for tests `net.simulated_latency_ms`, `net.simulated_jitter_ms`, `net.simulated_loss_percent` and
/// `net.simulated_bandwidth_bps`. Registering twice is harmless: keys already present are kept.
///
/// - Param registry: the registry to declare them in, before it is frozen.
/// - Ownership: `registry` is borrowed for the call.
/// - Fails: never.
void RegisterNetSettings(settings::Registry* registry);

/// The configuration of a [[net::UdpTransport]] from the current settings (the seed stays 0: random).
///
/// - Param registry: a registry in which [[net::RegisterNetSettings]] ran.
/// - Returns: the configuration.
/// - Aborts: the settings are not registered.
/// - Ownership: `registry` is borrowed for the call.
UdpTransportConfig UdpConfigFromSettings(const settings::Registry& registry);

/// The configuration of a [[net::TcpTransport]] from the current settings.
///
/// - Param registry: a registry in which [[net::RegisterNetSettings]] ran.
/// - Returns: the configuration.
/// - Aborts: the settings are not registered.
/// - Ownership: `registry` is borrowed for the call.
TcpTransportConfig TcpConfigFromSettings(const settings::Registry& registry);

/// The options of a [[net::TcpStream]] from the current settings.
///
/// - Param registry: a registry in which [[net::RegisterNetSettings]] ran.
/// - Returns: the options.
/// - Aborts: the settings are not registered.
/// - Ownership: `registry` is borrowed for the call.
TcpStreamOptions StreamOptionsFromSettings(const settings::Registry& registry);

/// The default link of a [[net::SimNetwork]] from the `net.simulated_*` settings (no impairment by default).
///
/// - Param registry: a registry in which [[net::RegisterNetSettings]] ran.
/// - Returns: the link.
/// - Aborts: the settings are not registered.
/// - Ownership: `registry` is borrowed for the call.
LinkConfig LinkFromSettings(const settings::Registry& registry);

}  // namespace engine_placeholder::net
