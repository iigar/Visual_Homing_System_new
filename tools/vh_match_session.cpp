// vh_match_session (M12): drive a dry-run "replay the route" session over a
// manifest of live frames and emit the compact evidence log. NO live output —
// every command faces the hard-closed live-output boundary (blocked, never
// allowed). Exit 0 if passed=1, else 1.
//
// usage:
//   vh_match_session <route.vhrs> <frames.csv> [options]
// options:
//   --expected forward|reverse|any   (default forward)
//   --endpoint-gate <mille>          (default 950)
//   --min-confidence <mille>         (default 600)
//   --fps <n>                        configured capture fps (log only)
//   --quality-pass                   assert the route passed M6 quality (gate)
//   --synthetic-heartbeat            inject a fresh armed heartbeat each tick
//                                    (desktop replay has no real autopilot)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "vh/command_sink.hpp"
#include "vh/dry_run_bridge.hpp"
#include "vh/endian.hpp"
#include "vh/health.hpp"
#include "vh/manifest.hpp"
#include "vh/match_session.hpp"
#include "vh/mavlink.hpp"
#include "vh/navigator.hpp"
#include "vh/replay_camera.hpp"
#include "vh/route_io.hpp"
#include "vh/route_matcher.hpp"

namespace {

std::vector<std::uint8_t> heartbeat_frame() {
  std::vector<std::uint8_t> p(9, 0);
  vh::store_le32(p.data(), 5);  // custom_mode LOITER
  p[6] = 0x80;                  // armed
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
                 "usage: vh_match_session <route.vhrs> <frames.csv> [options]\n");
    return EXIT_FAILURE;
  }

  vh::MatchSessionConfig cfg;
  bool synthetic_hb = false;
  std::uint16_t min_conf = 600;
  for (int i = 3; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--expected" && i + 1 < argc) {
      const std::string v = argv[++i];
      cfg.expected_progress = v == "reverse"  ? vh::ExpectedProgress::Reverse
                              : v == "any"     ? vh::ExpectedProgress::Any
                                               : vh::ExpectedProgress::Forward;
    } else if (a == "--endpoint-gate" && i + 1 < argc) {
      cfg.endpoint_progress_gate_mille =
          static_cast<std::uint16_t>(std::atoi(argv[++i]));
    } else if (a == "--min-confidence" && i + 1 < argc) {
      min_conf = static_cast<std::uint16_t>(std::atoi(argv[++i]));
      cfg.min_confidence_mille = min_conf;
    } else if (a == "--fps" && i + 1 < argc) {
      cfg.configured_fps = static_cast<std::uint32_t>(std::atoi(argv[++i]));
    } else if (a == "--quality-pass") {
      cfg.dry_run_quality_passed = true;
    } else if (a == "--synthetic-heartbeat") {
      synthetic_hb = true;
    } else {
      std::fprintf(stderr, "unknown option: %s\n", a.c_str());
      return EXIT_FAILURE;
    }
  }

  // Route.
  vh::VhrsReader reader((std::filesystem::path(argv[1])));
  if (reader.error() != vh::RouteIoError::None) {
    std::printf("session_error=route_%s\n", vh::to_string(reader.error()));
    return EXIT_FAILURE;
  }

  // Live frames.
  auto man = vh::load_manifest(std::filesystem::path(argv[2]));
  if (man.error != vh::ManifestError::None) {
    std::printf("session_error=manifest_%s line=%zu\n",
                vh::to_string(man.error), man.error_line);
    return EXIT_FAILURE;
  }
  cfg.requested_frame_count = static_cast<std::uint32_t>(man.entries.size());

  vh::MatcherConfig mcfg;
  mcfg.min_confidence_mille = min_conf;
  vh::Gray8RouteMatcher matcher(reader.result().entries, mcfg);

  vh::ReplayCameraSource camera(std::move(man.entries));
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession session(bridge, cfg, &matcher);

  while (auto frame = camera.next_frame()) {
    const std::int64_t now = frame->timestamp_ns;
    if (synthetic_hb) {
      const auto hb = heartbeat_frame();
      bridge.ingest_telemetry(hb.data(), hb.size(), now);
    }
    vh::HealthSnapshot h;
    h.state = vh::HealthState::Ready;
    h.camera_ok = true;
    h.navigation_ok = true;
    h.now_ns = now;
    session.step(*frame, h, now);
  }

  const auto result = session.finish();
  std::printf("%s\n", vh::format_compact_log(result).c_str());
  return result.passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
