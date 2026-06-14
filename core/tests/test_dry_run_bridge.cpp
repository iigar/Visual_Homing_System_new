#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "vh/command_sink.hpp"
#include "vh/dry_run_bridge.hpp"
#include "vh/endian.hpp"
#include "vh/health.hpp"
#include "vh/mavlink.hpp"
#include "vh/navigator.hpp"
#include "vh/route_match.hpp"
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

Bytes heartbeat(std::uint32_t custom_mode, std::uint8_t base_mode) {
  Bytes p(9, 0);
  vh::store_le32(p.data(), custom_mode);
  p[6] = base_mode;
  return p;
}

Bytes attitude(float roll, float pitch, float yaw) {
  Bytes p(28, 0);
  vh::store_le32(&p[4], std::bit_cast<std::uint32_t>(roll));
  vh::store_le32(&p[8], std::bit_cast<std::uint32_t>(pitch));
  vh::store_le32(&p[12], std::bit_cast<std::uint32_t>(yaw));
  return p;
}

Bytes global_position(std::int32_t rel_alt_mm) {
  Bytes p(28, 0);
  vh::store_le_i32(&p[16], rel_alt_mm);
  return p;
}

vh::HealthSnapshot ready_health(std::int64_t now_ns) {
  vh::HealthSnapshot h;
  h.state = vh::HealthState::Ready;
  h.camera_ok = true;
  h.navigation_ok = true;
  h.mavlink_ok = true;  // overlaid by the bridge anyway
  h.now_ns = now_ns;
  return h;
}

vh::RouteMatch good_match(std::int64_t now_ns) {
  vh::RouteMatch m;
  m.valid = true;
  m.timestamp_ns = now_ns;
  m.confidence_mille = 800;
  m.confidence = 0.8f;
  m.direction_error_millirad = 100;
  return m;
}

// -------------------------------------------------------------------------
void test_telemetry_polling() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);

  Bytes s;
  for (Bytes f : {build_v1(vh::kMsgHeartbeat, heartbeat(5, 0x80)),
                  build_v1(vh::kMsgAttitude, attitude(0.5f, -0.25f, 1.5f)),
                  build_v1(vh::kMsgGlobalPositionInt, global_position(25000))}) {
    s.insert(s.end(), f.begin(), f.end());
  }
  bridge.ingest_telemetry(s.data(), s.size(), 1'000'000'000);
  auto t = bridge.telemetry();
  VH_EXPECT(t.armed);
  VH_EXPECT(std::strcmp(t.mode_label, "LOITER") == 0);
  VH_EXPECT(t.roll == 0.5f);
  VH_EXPECT(t.yaw == 1.5f);
  VH_EXPECT(t.relative_alt_mm == 25000);
}

void test_fresh_produces_valid_command() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();

  Bytes hb = build_v1(vh::kMsgHeartbeat, heartbeat(5, 0x80));
  bridge.ingest_telemetry(hb.data(), hb.size(), 1'000'000'000);

  auto cmd = bridge.tick(good_match(1'000'000'000), ready_health(1'000'000'000),
                         1'000'000'000);
  VH_EXPECT(cmd.valid);
  VH_EXPECT(cmd.yaw_rate_microradps == 50'000);  // 100 millirad * gain 500
  VH_EXPECT(bridge.counters().commands_valid == 1);
  VH_EXPECT(bridge.counters().blocked_stale == 0);
  VH_EXPECT(sink.history_size() == 1);
  VH_EXPECT(sink.latest().valid);
}

void test_stale_telemetry_blocks() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();

  Bytes hb = build_v1(vh::kMsgHeartbeat, heartbeat(5, 0x80));
  bridge.ingest_telemetry(hb.data(), hb.size(), 1'000'000'000);

  // 3 s later, no fresh heartbeat -> stale (default max age 2 s). The match is
  // fresh, so stale telemetry is the only reason to block.
  const std::int64_t now = 4'000'000'000;
  auto cmd = bridge.tick(good_match(now), ready_health(now), now);
  VH_EXPECT(!cmd.valid);
  VH_EXPECT(cmd.yaw_rate_microradps == 0);
  VH_EXPECT(bridge.counters().blocked_stale == 1);
  VH_EXPECT(bridge.counters().commands_invalid == 1);
  VH_EXPECT(sink.latest().valid == false);  // dry-run still records the block
}

void test_never_heartbeat_blocks() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);
  sink.start();
  // No telemetry ever ingested -> mavlink_ok false -> blocked.
  auto cmd = bridge.tick(good_match(1'000'000'000), ready_health(1'000'000'000),
                         1'000'000'000);
  VH_EXPECT(!cmd.valid);
  VH_EXPECT(bridge.counters().blocked_stale == 1);
}

void test_incompatible_disarmed_blocks() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridgeConfig cfg;
  cfg.require_armed = true;
  vh::DryRunBridge bridge(nav, sink, cfg);
  sink.start();

  // Fresh heartbeat but DISARMED (base_mode without 0x80).
  Bytes hb = build_v1(vh::kMsgHeartbeat, heartbeat(5, 0x00));
  bridge.ingest_telemetry(hb.data(), hb.size(), 1'000'000'000);

  auto cmd = bridge.tick(good_match(1'000'000'000), ready_health(1'000'000'000),
                         1'000'000'000);
  VH_EXPECT(!cmd.valid);
  VH_EXPECT(bridge.counters().blocked_incompatible == 1);
  VH_EXPECT(bridge.counters().blocked_stale == 0);  // heartbeat was fresh
}

// Sink left stopped: the bridge still proposes a valid command, but the
// dry-run boundary refuses to record it (fail-closed write path).
void test_sink_stopped_rejects_record() {
  vh::BoundedNavigator nav;
  vh::DryRunCommandSink sink;
  vh::DryRunBridge bridge(nav, sink);  // sink NOT started

  Bytes hb = build_v1(vh::kMsgHeartbeat, heartbeat(5, 0x80));
  bridge.ingest_telemetry(hb.data(), hb.size(), 1'000'000'000);
  auto cmd = bridge.tick(good_match(1'000'000'000), ready_health(1'000'000'000),
                         1'000'000'000);
  VH_EXPECT(cmd.valid);                         // navigator proposed it
  VH_EXPECT(bridge.counters().commands_valid == 1);
  VH_EXPECT(sink.history_size() == 0);          // but nothing recorded
  VH_EXPECT(sink.counters().rejected_stopped == 1);
}

}  // namespace

int main() {
  test_telemetry_polling();
  test_fresh_produces_valid_command();
  test_stale_telemetry_blocks();
  test_never_heartbeat_blocks();
  test_incompatible_disarmed_blocks();
  test_sink_stopped_rejects_record();
  return ::vh::test::summary("test_dry_run_bridge");
}
