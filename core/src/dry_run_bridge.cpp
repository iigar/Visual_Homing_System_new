#include "vh/dry_run_bridge.hpp"

namespace vh {

DryRunBridge::DryRunBridge(BoundedNavigator& nav, DryRunCommandSink& sink,
                           DryRunBridgeConfig config) noexcept
    : nav_(nav), sink_(sink), cfg_(config), telem_(config.telemetry) {}

std::size_t DryRunBridge::ingest_telemetry(const std::uint8_t* data,
                                           std::size_t n,
                                           std::int64_t now_ns) noexcept {
  return telem_.ingest(data, n, now_ns);
}

NavigationCommand DryRunBridge::tick(const RouteMatch& match,
                                     const HealthSnapshot& base_health,
                                     std::int64_t now_ns) noexcept {
  telem_.update(now_ns);
  const TelemetrySnapshot ts = telem_.snapshot();

  const bool stale = !ts.mavlink_ok;  // missing or stale heartbeat
  const bool incompatible = cfg_.require_armed && !ts.armed;

  // Overlay FC freshness/compatibility onto the pipeline's health: telemetry
  // drives mavlink_ok, and any incompatibility forces it false so the
  // navigator's gate refuses to produce a valid command.
  HealthSnapshot eff = base_health;
  eff.mavlink_ok = ts.mavlink_ok && !incompatible;

  const NavigationCommand cmd = nav_.propose(match, eff);
  sink_.send(cmd);  // dry-run: records to history, transmits nothing

  ++counters_.ticks;
  if (stale) {
    ++counters_.blocked_stale;
  } else if (incompatible) {
    ++counters_.blocked_incompatible;
  }
  if (cmd.valid) {
    ++counters_.commands_valid;
  } else {
    ++counters_.commands_invalid;
  }
  return cmd;
}

}  // namespace vh
