#include <cstdint>
#include <string>
#include <vector>

#include "vh/command_sink.hpp"
#include "vh/dry_run_bridge.hpp"
#include "vh/endian.hpp"
#include "vh/health.hpp"
#include "vh/match_session.hpp"
#include "vh/mavlink.hpp"
#include "vh/navigator.hpp"
#include "vh/route_match.hpp"
#include "vh/safety_gate.hpp"
#include "vh_test.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes build_v1(std::uint32_t msgid, const Bytes& payload) {
  Bytes region;
  region.push_back(static_cast<std::uint8_t>(payload.size()));
  region.push_back(0);  // seq
  region.push_back(1);  // sysid
  region.push_back(1);  // compid
  region.push_back(static_cast<std::uint8_t>(msgid & 0xFF));
  region.insert(region.end(), payload.begin(), payload.end());
  std::uint8_t extra = 0;
  vh::mavlink_crc_extra(msgid, extra);
  const std::uint16_t crc = vh::mavlink_crc(region.data(), region.size(), extra);
  Bytes f;
  f.push_back(vh::kMavlinkStxV1);
  f.insert(f.end(), region.begin(), region.end());
  f.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  f.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
  return f;
}

Bytes heartbeat_armed() {
  Bytes p(9, 0);
  vh::store_le32(p.data(), 5);  // custom_mode (LOITER)
  p[6] = 0x80;                  // base_mode armed
  return p;
}

Bytes heartbeat_disarmed() {
  Bytes p(9, 0);
  vh::store_le32(p.data(), 5);  // custom_mode (LOITER)
  p[6] = 0x00;                  // base_mode NOT armed
  return p;
}

// Keep telemetry fresh at `now` so the bridge's mavlink_ok gate stays open.
void feed_heartbeat(vh::DryRunBridge& bridge, std::int64_t now) {
  Bytes hb = build_v1(vh::kMsgHeartbeat, heartbeat_armed());
  bridge.ingest_telemetry(hb.data(), hb.size(), now);
}

// Fresh but disarmed heartbeat: telemetry is healthy, vehicle is not armed.
void feed_heartbeat_disarmed(vh::DryRunBridge& bridge, std::int64_t now) {
  Bytes hb = build_v1(vh::kMsgHeartbeat, heartbeat_disarmed());
  bridge.ingest_telemetry(hb.data(), hb.size(), now);
}

vh::HealthSnapshot ready_health(std::int64_t now) {
  vh::HealthSnapshot h;
  h.state = vh::HealthState::Ready;
  h.camera_ok = true;
  h.navigation_ok = true;
  h.mavlink_ok = true;
  h.now_ns = now;
  return h;
}

vh::RouteMatch mk(std::int64_t now, std::uint16_t progress, std::uint32_t idx,
                  std::uint16_t conf, bool valid = true) {
  vh::RouteMatch m;
  m.valid = valid;
  m.timestamp_ns = now;
  m.route_index = idx;
  m.route_total = 5;
  m.progress_mille = progress;
  m.progress = static_cast<float>(progress) / 1000.0f;
  m.confidence_mille = conf;
  m.confidence = static_cast<float>(conf) / 1000.0f;
  m.direction_error_millirad = 0;
  return m;
}

vh::MatchSessionConfig nominal_cfg() {
  vh::MatchSessionConfig c;
  c.expected_progress = vh::ExpectedProgress::Forward;
  c.endpoint_progress_gate_mille = 950;
  c.min_confidence_mille = 600;
  c.requested_frame_count = 5;
  c.configured_fps = 10;
  c.dry_run_quality_passed = true;
  return c;
}

// ---------------------------------------------------------------------------
void test_nominal_pass() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 800),
                 ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(r.passed);
  VH_EXPECT(r.frames == 5);
  VH_EXPECT(r.valid_matches == 5);
  VH_EXPECT(r.endpoint_passed);
  VH_EXPECT(r.progress_gate_passed);
  VH_EXPECT(r.progress_regressions == 0);
  VH_EXPECT(r.index_jumps == 0);
  VH_EXPECT(r.confidence_min_mille == 800);
  VH_EXPECT(r.dry_run_valid == 5);
  VH_EXPECT(r.dry_run_total == 5);
  VH_EXPECT(r.telemetry_health);
  VH_EXPECT(r.telemetry_dropped == 0);
  VH_EXPECT(r.live_output_gate_allowed == 0);
  VH_EXPECT(r.live_output_gate_blocked == 5);
  VH_EXPECT(r.elapsed_ms == 400);
  VH_EXPECT(r.stop_reason == "endpoint_reached");
}

void test_regression_detected() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  // 500 -> 250 is a backward step under Forward expectation.
  const std::uint16_t prog[5] = {0, 500, 250, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 800),
                 ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(r.progress_regressions == 1);
  VH_EXPECT(r.rollback_total_mille == 250);
  VH_EXPECT(!r.passed);  // max_regressions=0
}

void test_index_jump_detected() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  const std::uint32_t idx[5] = {0, 1, 5, 6, 7};  // 1->5 is a jump of 4
  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    s.step_match(mk(now, prog[i], idx[i], 800), ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(r.index_jumps == 1);
  VH_EXPECT(!r.passed);  // max_index_jumps=0
}

void test_endpoint_stops_commands() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  // Endpoint (>=950) reached on frame index 4; a 6th frame follows.
  const std::uint16_t prog[6] = {0, 250, 500, 750, 1000, 1000};
  for (int i = 0; i < 6; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    auto cmd = s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 800),
                            ready_health(now), now);
    if (i == 5) VH_EXPECT(!cmd.valid);  // no command generated past endpoint
  }
  auto r = s.finish();
  VH_EXPECT(s.stopped());
  VH_EXPECT(r.endpoint_passed);
  VH_EXPECT(r.frames == 6);
  VH_EXPECT(r.dry_run_total == 5);  // command generation stopped at the endpoint
  VH_EXPECT(r.live_output_gate_blocked == 5);
  VH_EXPECT(r.stop_reason == "endpoint_reached");
}

void test_stale_telemetry_fails() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  // Never feed a heartbeat: every tick is blocked by stale telemetry.
  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 800),
                 ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(!r.telemetry_health);
  VH_EXPECT(r.telemetry_dropped == 5);
  VH_EXPECT(r.dry_run_valid == 0);
  VH_EXPECT(!r.passed);
}

void test_low_confidence_fails() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 500),  // < 600
                 ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(r.confidence_min_mille == 500);
  VH_EXPECT(!r.passed);
}

void test_invalid_match_breaks_pass() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    const bool valid = (i != 2);  // frame 2 fails to match
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 800, valid),
                 ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(r.valid_matches == 4);
  VH_EXPECT(r.frames == 5);
  VH_EXPECT(!r.passed);
}

void test_reverse_endpoint() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  auto cfg = nominal_cfg();
  cfg.expected_progress = vh::ExpectedProgress::Reverse;  // endpoint at progress<=50
  vh::DryRunMatchSession s(bridge, cfg);

  const std::uint16_t prog[5] = {1000, 750, 500, 250, 0};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(4 - i), 800),
                 ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(r.endpoint_passed);
  VH_EXPECT(r.progress_gate_passed);
  VH_EXPECT(r.progress_regressions == 0);  // decreasing progress is expected
  VH_EXPECT(r.stop_reason == "endpoint_reached");
}

void test_dry_run_quality_gate() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  auto cfg = nominal_cfg();
  cfg.dry_run_quality_passed = false;  // route quality not established
  vh::DryRunMatchSession s(bridge, cfg);

  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 800),
                 ready_health(now), now);
  }
  auto r = s.finish();
  VH_EXPECT(!r.dry_run_quality);
  VH_EXPECT(!r.passed);  // everything else green, but quality gate fails closed
}

void test_compact_log_fields() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat(bridge, now);
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 800),
                 ready_health(now), now);
  }
  const std::string log = vh::format_compact_log(s.finish());
  VH_EXPECT(log.find("passed=true") != std::string::npos);
  VH_EXPECT(log.find("frames=5/5") != std::string::npos);
  VH_EXPECT(log.find("valid_matches=5") != std::string::npos);
  VH_EXPECT(log.find("endpoint_passed=true") != std::string::npos);
  VH_EXPECT(log.find("progress_gate_passed=true") != std::string::npos);
  VH_EXPECT(log.find("telemetry_health=true") != std::string::npos);
  VH_EXPECT(log.find("dry_run_quality=true") != std::string::npos);
  VH_EXPECT(log.find("dry_run_valid=5/5") != std::string::npos);
  VH_EXPECT(log.find("live_output_gate_allowed=0") != std::string::npos);
  VH_EXPECT(log.find("live_output_gate_blocked=5") != std::string::npos);
  VH_EXPECT(log.find("live_output_gate_block_reasons=live_output_disabled:5") !=
            std::string::npos);
  VH_EXPECT(log.find("stop_reason=endpoint_reached") != std::string::npos);
}

// With a bench-readiness safety gate attached but the vehicle disarmed, every
// command is blocked for exactly one reason: vehicle_not_armed.
void test_live_output_gate_vehicle_not_armed() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  vh::DryRunMatchSession s(bridge, nominal_cfg());

  vh::SafetyGateConfig gcfg;
  gcfg.runtime_enabled = true;
  gcfg.operator_confirmed = true;
  vh::LiveMavlinkOutputSafetyGate gate(gcfg);
  vh::LiveOutputContext ctx;
  ctx.single_writer_owned = true;
  ctx.audit_ready = true;
  s.set_live_output_gate(gate, ctx);

  const std::uint16_t prog[5] = {0, 250, 500, 750, 1000};
  for (int i = 0; i < 5; ++i) {
    const std::int64_t now = 1'000'000'000 + static_cast<std::int64_t>(i) * 100'000'000;
    feed_heartbeat_disarmed(bridge, now);
    vh::HealthSnapshot h = ready_health(now);
    h.frames_seen = static_cast<std::uint64_t>(i) + 1;
    h.frame_age_ns = 0;
    s.step_match(mk(now, prog[i], static_cast<std::uint32_t>(i), 900),
                 h, now);
  }
  auto r = s.finish();
  VH_EXPECT(r.live_output_gate_allowed == 0);
  VH_EXPECT(r.live_output_gate_blocked == 5);
  VH_EXPECT(r.live_output_gate_block_reason_counts.size() == 1);
  VH_EXPECT(r.live_output_gate_block_reason_counts.at("vehicle_not_armed") == 5);
  const std::string log = vh::format_compact_log(r);
  VH_EXPECT(log.find("live_output_gate_block_reasons=vehicle_not_armed:5") !=
            std::string::npos);
}

}  // namespace

int main() {
  test_nominal_pass();
  test_regression_detected();
  test_index_jump_detected();
  test_endpoint_stops_commands();
  test_stale_telemetry_fails();
  test_low_confidence_fails();
  test_invalid_match_breaks_pass();
  test_reverse_endpoint();
  test_dry_run_quality_gate();
  test_compact_log_fields();
  test_live_output_gate_vehicle_not_armed();
  return ::vh::test::summary("test_match_session");
}
