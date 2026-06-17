// vh_live_session (M14): drive the M13 LiveMavlinkOutputSession over a route +
// live-frame manifest and emit the session AUDIT log as evidence. Bench-
// readiness scenario: operator authority granted, single writer owned, audit
// ready, dry-run quality passed, telemetry fresh — but the vehicle is DISARMED
// (props off), so every command is blocked for exactly vehicle_not_armed and
// nothing is transmitted. At the route endpoint the session records
// `endpoint_progress_reached` and stops command generation.
//
// usage:
//   vh_live_session <route.vhrs> <frames.csv> [--min-confidence <mille>]
//                   [--endpoint-gate <mille>]
//
// stdout: one `audit_event=...` line per record (consumed by
// scripts/check-live-session-audit-log.sh). Exit 0 on a clean run.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "vh/command_sink.hpp"
#include "vh/dry_run_bridge.hpp"
#include "vh/endian.hpp"
#include "vh/health.hpp"
#include "vh/live_output.hpp"
#include "vh/manifest.hpp"
#include "vh/mavlink.hpp"
#include "vh/navigator.hpp"
#include "vh/replay_camera.hpp"
#include "vh/route_io.hpp"
#include "vh/route_matcher.hpp"
#include "vh/safety_gate.hpp"

namespace {

std::vector<std::uint8_t> heartbeat_disarmed() {
  std::vector<std::uint8_t> p(9, 0);
  vh::store_le32(p.data(), 5);  // custom_mode LOITER
  p[6] = 0x00;                  // disarmed
  std::vector<std::uint8_t> region;
  region.push_back(static_cast<std::uint8_t>(p.size()));
  region.push_back(0);
  region.push_back(1);
  region.push_back(1);
  region.push_back(static_cast<std::uint8_t>(vh::kMsgHeartbeat & 0xFF));
  region.insert(region.end(), p.begin(), p.end());
  std::uint8_t extra = 0;
  vh::mavlink_crc_extra(vh::kMsgHeartbeat, extra);
  const std::uint16_t crc = vh::mavlink_crc(region.data(), region.size(), extra);
  std::vector<std::uint8_t> f;
  f.push_back(vh::kMavlinkStxV1);
  f.insert(f.end(), region.begin(), region.end());
  f.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  f.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
  return f;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: vh_live_session <route.vhrs> <frames.csv> [options]\n");
    return EXIT_FAILURE;
  }

  std::uint16_t min_conf = 800;
  std::uint16_t endpoint_gate = 1000;  // endpoint == final frame for clean audit
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--min-confidence" && i + 1 < argc) {
      min_conf = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (a == "--endpoint-gate" && i + 1 < argc) {
      endpoint_gate = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else {
      std::fprintf(stderr, "unknown option: %s\n", a.c_str());
      return EXIT_FAILURE;
    }
  }

  vh::VhrsReader reader((std::filesystem::path(argv[1])));
  if (reader.error() != vh::RouteIoError::None) {
    std::printf("session_error=route_%s\n", vh::to_string(reader.error()));
    return EXIT_FAILURE;
  }
  auto man = vh::load_manifest(std::filesystem::path(argv[2]));
  if (man.error != vh::ManifestError::None) {
    std::printf("session_error=manifest_%s line=%zu\n",
                vh::to_string(man.error), man.error_line);
    return EXIT_FAILURE;
  }

  vh::MatcherConfig mcfg;
  mcfg.min_confidence_mille = min_conf;
  vh::Gray8RouteMatcher matcher(reader.result().entries, mcfg);
  vh::ReplayCameraSource camera(std::move(man.entries));

  vh::BoundedNavigator nav;
  vh::DryRunCommandSink dry_sink;
  vh::DryRunBridge bridge(nav, dry_sink);
  dry_sink.start();

  vh::SafetyGateConfig gcfg;
  gcfg.runtime_enabled = true;
  gcfg.operator_confirmed = true;
  gcfg.min_confidence_mille = min_conf;
  vh::LiveMavlinkOutputSafetyGate gate(gcfg);

  vh::LiveMavlinkOutputAuditLog audit(/*ready=*/true);
  vh::LiveMavlinkBridge live_bridge;
  vh::LiveMavlinkOutputSession session(gate, audit, dry_sink, live_bridge);

  if (!session.start("live_match_dry_run")) {
    std::printf("session_error=start_failed\n");
    return EXIT_FAILURE;
  }

  std::uint64_t seen = 0;
  while (auto frame = camera.next_frame()) {
    const std::int64_t now = frame->timestamp_ns;
    const auto hb = heartbeat_disarmed();
    bridge.ingest_telemetry(hb.data(), hb.size(), now);

    ++seen;
    vh::HealthSnapshot h;
    h.state = vh::HealthState::Ready;
    h.camera_ok = true;
    h.navigation_ok = true;
    h.frames_seen = seen;
    h.last_frame_timestamp_ns = now;
    h.frame_age_ns = 0;
    h.now_ns = now;

    const vh::RouteMatch m = matcher.match(*frame);
    const vh::NavigationCommand cmd = bridge.tick(m, h, now);

    vh::SafetyGateInputs in;
    in.single_writer_owned = true;
    in.audit_ready = true;
    in.dry_run_quality_passed = true;
    in.health = h;
    in.telemetry = bridge.telemetry();
    in.match = m;
    in.command = cmd;
    in.now_ns = now;

    if (session.stopped()) break;
    session.tick(in);

    // Endpoint == reached the gate: record the explicit end-of-route action and
    // stop generation (no post-endpoint command tail).
    if (m.valid && m.progress_mille >= endpoint_gate) {
      session.mark_endpoint();
    }
  }

  std::printf("%s", audit.format_log().c_str());
  return EXIT_SUCCESS;
}
