#pragma once

// DryRunBridge (M9) — wires read-only telemetry, the bounded navigator, and
// the dry-run command sink into one control tick, with NO live output. Its
// defining job is to fold flight-controller freshness/compatibility into
// command validity: a stale heartbeat or an incompatible FC state forces the
// navigator's mavlink_ok gate false, so no valid command is produced. See
// DECISIONS D-023.
//
// "Dry-run" means every command goes to DryRunCommandSink (history only). The
// bridge never opens a MAVLink writer — that boundary stays fail-closed.

#include <cstddef>
#include <cstdint>

#include "vh/command_sink.hpp"
#include "vh/health.hpp"
#include "vh/navigation_command.hpp"
#include "vh/navigator.hpp"
#include "vh/route_match.hpp"
#include "vh/telemetry.hpp"

namespace vh {

struct DryRunBridgeConfig {
  TelemetryConfig telemetry;
  // If true, a disarmed FC is treated as incompatible and blocks valid
  // commands. Default false: the dry-run buildout exercises the path without
  // requiring a live armed vehicle.
  bool require_armed = false;
};

struct DryRunBridgeCounters {
  std::uint64_t ticks = 0;
  std::uint64_t blocked_stale = 0;          // heartbeat missing/stale
  std::uint64_t blocked_incompatible = 0;   // FC state incompatible (disarmed)
  std::uint64_t commands_valid = 0;
  std::uint64_t commands_invalid = 0;
};

class DryRunBridge {
 public:
  DryRunBridge(BoundedNavigator& nav, DryRunCommandSink& sink,
               DryRunBridgeConfig config = {}) noexcept;

  // Feed scripted serial bytes (heartbeat + telemetry snapshots). Returns the
  // number of valid frames decoded. Read-only — nothing is sent back.
  std::size_t ingest_telemetry(const std::uint8_t* data, std::size_t n,
                               std::int64_t now_ns) noexcept;

  // One control tick: overlay telemetry freshness/compatibility onto
  // base_health, run the navigator, and dry-run-send the command. Returns the
  // command that was produced (and recorded by the sink if started).
  NavigationCommand tick(const RouteMatch& match,
                         const HealthSnapshot& base_health,
                         std::int64_t now_ns) noexcept;

  TelemetrySnapshot telemetry() const { return telem_.snapshot(); }
  const DryRunBridgeCounters& counters() const noexcept { return counters_; }

 private:
  BoundedNavigator& nav_;
  DryRunCommandSink& sink_;
  DryRunBridgeConfig cfg_;
  MavlinkTelemetry telem_;
  DryRunBridgeCounters counters_;
};

}  // namespace vh
