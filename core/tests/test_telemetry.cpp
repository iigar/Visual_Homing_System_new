#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

#include "vh/endian.hpp"
#include "vh/mavlink.hpp"
#include "vh/telemetry.hpp"
#include "vh_test.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes build_v1(std::uint32_t msgid, std::uint8_t sysid, const Bytes& payload) {
  Bytes region;
  region.push_back(static_cast<std::uint8_t>(payload.size()));
  region.push_back(0);       // seq
  region.push_back(sysid);
  region.push_back(1);       // compid
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

void put_f32(Bytes& p, std::size_t off, float v) {
  vh::store_le32(&p[off], std::bit_cast<std::uint32_t>(v));
}

Bytes heartbeat_payload(std::uint32_t custom_mode, std::uint8_t base_mode) {
  Bytes p(9, 0);
  vh::store_le32(p.data(), custom_mode);
  p[4] = 2;   // type
  p[5] = 3;   // autopilot = ArduPilotMega
  p[6] = base_mode;
  p[7] = 4;   // system_status
  p[8] = 3;   // mavlink_version
  return p;
}

Bytes attitude_payload(float roll, float pitch, float yaw, float yawspeed) {
  Bytes p(28, 0);
  vh::store_le32(p.data(), 12345);  // time_boot_ms
  put_f32(p, 4, roll);
  put_f32(p, 8, pitch);
  put_f32(p, 12, yaw);
  put_f32(p, 16, 0.0f);   // rollspeed
  put_f32(p, 20, 0.0f);   // pitchspeed
  put_f32(p, 24, yawspeed);
  return p;
}

Bytes global_position_payload(std::int32_t lat, std::int32_t lon,
                              std::int32_t rel_alt_mm, std::uint16_t hdg) {
  Bytes p(28, 0);
  vh::store_le32(p.data(), 12345);  // time_boot_ms
  vh::store_le_i32(&p[4], lat);
  vh::store_le_i32(&p[8], lon);
  vh::store_le_i32(&p[12], 100000);       // alt MSL mm
  vh::store_le_i32(&p[16], rel_alt_mm);
  vh::store_le16(&p[26], hdg);
  return p;
}

// -------------------------------------------------------------------------
void test_heartbeat_armed_and_mode() {
  vh::MavlinkTelemetry t;
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, heartbeat_payload(5, 0x80));  // LOITER, armed
  t.ingest(f.data(), f.size(), 1'000'000'000);
  auto s = t.snapshot();
  VH_EXPECT(s.heartbeat_seen);
  VH_EXPECT(s.armed);
  VH_EXPECT(s.custom_mode == 5);
  VH_EXPECT(std::strcmp(s.mode_label, "LOITER") == 0);
  VH_EXPECT(s.mavlink_ok);  // fresh
}

void test_disarmed_and_unknown_mode() {
  vh::MavlinkTelemetry t;
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, heartbeat_payload(999, 0x00));
  t.ingest(f.data(), f.size(), 1'000'000'000);
  auto s = t.snapshot();
  VH_EXPECT(!s.armed);
  VH_EXPECT(std::strcmp(s.mode_label, "UNKNOWN") == 0);
}

void test_heartbeat_freshness() {
  vh::MavlinkTelemetry t;  // default max age 2s
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, heartbeat_payload(5, 0x80));
  t.ingest(f.data(), f.size(), 1'000'000'000);
  VH_EXPECT(t.snapshot().mavlink_ok);

  // 3 s later, no new heartbeat -> stale -> not ok.
  t.update(1'000'000'000 + 3'000'000'000);
  VH_EXPECT(!t.snapshot().mavlink_ok);
  VH_EXPECT(t.snapshot().heartbeat_age_ns == 3'000'000'000);
}

void test_future_heartbeat_fail_closed() {
  vh::MavlinkTelemetry t;
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, heartbeat_payload(5, 0x80));
  t.ingest(f.data(), f.size(), 2'000'000'000);
  // "now" earlier than the stamp -> negative age -> not fresh.
  t.update(1'000'000'000);
  VH_EXPECT(!t.snapshot().mavlink_ok);
}

void test_attitude_decode() {
  vh::MavlinkTelemetry t;
  Bytes f = build_v1(vh::kMsgAttitude, 1,
                     attitude_payload(0.5f, -0.25f, 1.5f, 0.1f));
  t.ingest(f.data(), f.size(), 1'000'000'000);
  auto s = t.snapshot();
  VH_EXPECT(s.attitude_seen);
  VH_EXPECT(s.roll == 0.5f);
  VH_EXPECT(s.pitch == -0.25f);
  VH_EXPECT(s.yaw == 1.5f);
  VH_EXPECT(s.yawspeed == 0.1f);
}

void test_global_position_decode() {
  vh::MavlinkTelemetry t;
  Bytes f = build_v1(vh::kMsgGlobalPositionInt, 1,
                     global_position_payload(503443830, 301753990, 25000, 9000));
  t.ingest(f.data(), f.size(), 1'000'000'000);
  auto s = t.snapshot();
  VH_EXPECT(s.position_seen);
  VH_EXPECT(s.lat == 503443830);
  VH_EXPECT(s.lon == 301753990);
  VH_EXPECT(s.relative_alt_mm == 25000);
  VH_EXPECT(s.hdg_cdeg == 9000);
}

// Untrusted source: a CRC-valid heartbeat from the wrong sysid must not
// populate the snapshot when an expected sysid is configured.
void test_wrong_sysid_filtered() {
  vh::TelemetryConfig cfg;
  cfg.expected_sysid = 1;
  vh::MavlinkTelemetry t(cfg);
  Bytes f = build_v1(vh::kMsgHeartbeat, 99, heartbeat_payload(5, 0x80));  // sysid 99
  t.ingest(f.data(), f.size(), 1'000'000'000);
  auto s = t.snapshot();
  VH_EXPECT(!s.heartbeat_seen);          // dropped by filter
  VH_EXPECT(!s.mavlink_ok);
  VH_EXPECT(s.counters.frames_ok == 1);  // parser still validated the frame
}

void test_crc_fail_no_update() {
  vh::MavlinkTelemetry t;
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, heartbeat_payload(5, 0x80));
  f[f.size() - 2] ^= 0x55;  // corrupt CRC low byte
  t.ingest(f.data(), f.size(), 1'000'000'000);
  auto s = t.snapshot();
  VH_EXPECT(!s.heartbeat_seen);
  VH_EXPECT(!s.mavlink_ok);
  VH_EXPECT(s.counters.frames_crc_error == 1);
}

void test_mixed_stream_counters() {
  vh::MavlinkTelemetry t;
  Bytes s;
  Bytes hb = build_v1(vh::kMsgHeartbeat, 1, heartbeat_payload(4, 0x80));  // GUIDED
  Bytes at = build_v1(vh::kMsgAttitude, 1, attitude_payload(0, 0, 0.7f, 0));
  Bytes gp = build_v1(vh::kMsgGlobalPositionInt, 1,
                      global_position_payload(1, 2, 3, 0));
  for (Bytes* b : {&hb, &at, &gp}) s.insert(s.end(), b->begin(), b->end());
  const std::size_t decoded = t.ingest(s.data(), s.size(), 5'000'000'000);
  auto snap = t.snapshot();
  VH_EXPECT(decoded == 3);
  VH_EXPECT(snap.heartbeat_seen && snap.attitude_seen && snap.position_seen);
  VH_EXPECT(std::strcmp(snap.mode_label, "GUIDED") == 0);
  VH_EXPECT(snap.yaw == 0.7f);
  VH_EXPECT(snap.counters.frames_ok == 3);
}

}  // namespace

int main() {
  test_heartbeat_armed_and_mode();
  test_disarmed_and_unknown_mode();
  test_heartbeat_freshness();
  test_future_heartbeat_fail_closed();
  test_attitude_decode();
  test_global_position_decode();
  test_wrong_sysid_filtered();
  test_crc_fail_no_update();
  test_mixed_stream_counters();
  return ::vh::test::summary("test_telemetry");
}
