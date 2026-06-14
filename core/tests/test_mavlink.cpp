#include <cstdint>
#include <vector>

#include "vh/endian.hpp"
#include "vh/mavlink.hpp"
#include "vh_test.hpp"

namespace {

using Bytes = std::vector<std::uint8_t>;

// Build a MAVLink v1 frame with a correct CRC for the given msgid/payload.
Bytes build_v1(std::uint32_t msgid, std::uint8_t seq, std::uint8_t sysid,
               std::uint8_t compid, const Bytes& payload) {
  Bytes region;  // bytes the CRC covers: LEN..end of payload
  region.push_back(static_cast<std::uint8_t>(payload.size()));
  region.push_back(seq);
  region.push_back(sysid);
  region.push_back(compid);
  region.push_back(static_cast<std::uint8_t>(msgid & 0xFF));
  region.insert(region.end(), payload.begin(), payload.end());

  std::uint8_t extra = 0;
  vh::mavlink_crc_extra(msgid, extra);  // tests use known msgids
  const std::uint16_t crc = vh::mavlink_crc(region.data(), region.size(), extra);

  Bytes f;
  f.push_back(vh::kMavlinkStxV1);
  f.insert(f.end(), region.begin(), region.end());
  f.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  f.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
  return f;
}

// Build a MAVLink v2 frame (optionally signed) with a correct CRC.
Bytes build_v2(std::uint32_t msgid, std::uint8_t seq, std::uint8_t sysid,
               std::uint8_t compid, const Bytes& payload,
               std::uint8_t incompat = 0) {
  Bytes region;
  region.push_back(static_cast<std::uint8_t>(payload.size()));
  region.push_back(incompat);
  region.push_back(0);  // compat
  region.push_back(seq);
  region.push_back(sysid);
  region.push_back(compid);
  region.push_back(static_cast<std::uint8_t>(msgid & 0xFF));
  region.push_back(static_cast<std::uint8_t>((msgid >> 8) & 0xFF));
  region.push_back(static_cast<std::uint8_t>((msgid >> 16) & 0xFF));
  region.insert(region.end(), payload.begin(), payload.end());

  std::uint8_t extra = 0;
  vh::mavlink_crc_extra(msgid, extra);
  const std::uint16_t crc = vh::mavlink_crc(region.data(), region.size(), extra);

  Bytes f;
  f.push_back(vh::kMavlinkStxV2);
  f.insert(f.end(), region.begin(), region.end());
  f.push_back(static_cast<std::uint8_t>(crc & 0xFF));
  f.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
  if (incompat & vh::kMavlinkV2IflagSigned) {
    for (int i = 0; i < 13; ++i) f.push_back(static_cast<std::uint8_t>(0xA0 + i));
  }
  return f;
}

Bytes heartbeat_payload(std::uint32_t custom_mode, std::uint8_t base_mode) {
  Bytes p(9, 0);
  vh::store_le32(p.data(), custom_mode);
  p[4] = 2;          // type = MAV_TYPE_QUADROTOR (example)
  p[5] = 3;          // autopilot = ArduPilotMega (example)
  p[6] = base_mode;
  p[7] = 4;          // system_status
  p[8] = 3;          // mavlink_version
  return p;
}

// Feed a byte buffer through the parser; collect every emitted message.
std::vector<vh::MavlinkMessage> feed(vh::MavlinkParser& p, const Bytes& data) {
  std::vector<vh::MavlinkMessage> out;
  vh::MavlinkMessage m;
  for (std::uint8_t b : data) {
    if (p.parse_byte(b, m)) out.push_back(m);
  }
  return out;
}

// -------------------------------------------------------------------------
void test_v1_roundtrip() {
  vh::MavlinkParser p;
  Bytes f = build_v1(vh::kMsgHeartbeat, 7, 1, 1, heartbeat_payload(5, 0x80));
  auto msgs = feed(p, f);
  VH_EXPECT(msgs.size() == 1);
  if (msgs.size() == 1) {
    VH_EXPECT(msgs[0].version == 1);
    VH_EXPECT(msgs[0].msgid == vh::kMsgHeartbeat);
    VH_EXPECT(msgs[0].seq == 7);
    VH_EXPECT(msgs[0].sysid == 1);
    VH_EXPECT(msgs[0].payload_len == 9);
    VH_EXPECT(vh::load_le32(msgs[0].payload) == 5);  // custom_mode
    VH_EXPECT(msgs[0].payload[6] == 0x80);           // base_mode
  }
  VH_EXPECT(p.counters().frames_ok == 1);
  VH_EXPECT(p.counters().frames_crc_error == 0);
}

void test_v2_roundtrip() {
  vh::MavlinkParser p;
  Bytes f = build_v2(vh::kMsgAttitude, 3, 1, 1, Bytes(28, 0x11));
  auto msgs = feed(p, f);
  VH_EXPECT(msgs.size() == 1);
  if (msgs.size() == 1) {
    VH_EXPECT(msgs[0].version == 2);
    VH_EXPECT(msgs[0].msgid == vh::kMsgAttitude);
    VH_EXPECT(msgs[0].payload_len == 28);
  }
  VH_EXPECT(p.counters().frames_ok == 1);
}

// v2 empty-byte truncation: trailing zero bytes dropped on the wire; the
// parser must zero-extend the payload so fixed-offset decode still works.
void test_v2_truncated_payload() {
  vh::MavlinkParser p;
  // HEARTBEAT custom_mode=0, base_mode=0 -> all-zero payload truncates to LEN 0.
  Bytes f = build_v2(vh::kMsgHeartbeat, 1, 1, 1, Bytes{});  // len 0
  auto msgs = feed(p, f);
  VH_EXPECT(msgs.size() == 1);
  if (msgs.size() == 1) {
    VH_EXPECT(msgs[0].payload_len == 0);
    VH_EXPECT(vh::load_le32(msgs[0].payload) == 0);  // zero-extended
    VH_EXPECT(msgs[0].payload[6] == 0);
  }
}

void test_crc_corruption_rejected() {
  vh::MavlinkParser p;
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, 1, 1, heartbeat_payload(5, 0x80));
  f[f.size() - 1] ^= 0xFF;  // corrupt CRC high byte
  auto msgs = feed(p, f);
  VH_EXPECT(msgs.empty());
  VH_EXPECT(p.counters().frames_ok == 0);
  VH_EXPECT(p.counters().frames_crc_error == 1);
}

void test_corrupt_payload_rejected() {
  vh::MavlinkParser p;
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, 1, 1, heartbeat_payload(5, 0x80));
  f[7] ^= 0x01;  // flip a payload bit -> CRC no longer matches
  auto msgs = feed(p, f);
  VH_EXPECT(msgs.empty());
  VH_EXPECT(p.counters().frames_crc_error == 1);
}

void test_unknown_msgid() {
  vh::MavlinkParser p;
  // msgid 77 has no CRC_EXTRA in our table -> structurally consumed, untrusted.
  Bytes f = build_v2(77, 1, 1, 1, Bytes(4, 0x22));
  // build_v2 used crc_extra=0 (unknown) so CRC won't match a known table entry;
  // either way the parser must not emit and must count it as unknown.
  auto msgs = feed(p, f);
  VH_EXPECT(msgs.empty());
  VH_EXPECT(p.counters().frames_ok == 0);
  VH_EXPECT(p.counters().frames_unknown_msgid == 1);
}

void test_garbage_then_frame() {
  vh::MavlinkParser p;
  Bytes stream = {0x00, 0x11, 0x22, 0x33, 0x44};  // 5 junk bytes (no STX)
  Bytes f = build_v1(vh::kMsgHeartbeat, 9, 1, 1, heartbeat_payload(2, 0x00));
  stream.insert(stream.end(), f.begin(), f.end());
  auto msgs = feed(p, stream);
  VH_EXPECT(msgs.size() == 1);
  VH_EXPECT(p.counters().bytes_discarded == 5);
  VH_EXPECT(p.counters().frames_ok == 1);
}

void test_partial_then_complete() {
  vh::MavlinkParser p;
  Bytes f = build_v1(vh::kMsgHeartbeat, 1, 1, 1, heartbeat_payload(5, 0x80));
  // Feed all but the last byte: nothing emitted yet.
  Bytes head(f.begin(), f.end() - 1);
  auto a = feed(p, head);
  VH_EXPECT(a.empty());
  // Feed the final byte: frame completes.
  vh::MavlinkMessage m;
  VH_EXPECT(p.parse_byte(f.back(), m));
  VH_EXPECT(p.counters().frames_ok == 1);
}

void test_signed_v2_consumed() {
  vh::MavlinkParser p;
  Bytes f = build_v2(vh::kMsgAttitude, 1, 1, 1, Bytes(28, 0x07),
                     vh::kMavlinkV2IflagSigned);
  // Frame should still be emitted (signature consumed, not verified), and the
  // following valid frame must parse cleanly -> proves stream stayed aligned.
  Bytes next = build_v1(vh::kMsgHeartbeat, 2, 1, 1, heartbeat_payload(5, 0x80));
  f.insert(f.end(), next.begin(), next.end());
  auto msgs = feed(p, f);
  VH_EXPECT(msgs.size() == 2);
  if (msgs.size() == 2) {
    VH_EXPECT(msgs[0].signed_frame);
    VH_EXPECT(msgs[0].msgid == vh::kMsgAttitude);
    VH_EXPECT(msgs[1].msgid == vh::kMsgHeartbeat);
  }
  VH_EXPECT(p.counters().frames_ok == 2);
}

void test_back_to_back_frames() {
  vh::MavlinkParser p;
  Bytes s;
  for (int i = 0; i < 3; ++i) {
    Bytes f = build_v1(vh::kMsgHeartbeat, static_cast<std::uint8_t>(i), 1, 1,
                       heartbeat_payload(static_cast<std::uint32_t>(i), 0x80));
    s.insert(s.end(), f.begin(), f.end());
  }
  auto msgs = feed(p, s);
  VH_EXPECT(msgs.size() == 3);
  VH_EXPECT(p.counters().frames_ok == 3);
  VH_EXPECT(p.counters().bytes_discarded == 0);
}

}  // namespace

int main() {
  test_v1_roundtrip();
  test_v2_roundtrip();
  test_v2_truncated_payload();
  test_crc_corruption_rejected();
  test_corrupt_payload_rejected();
  test_unknown_msgid();
  test_garbage_then_frame();
  test_partial_then_complete();
  test_signed_v2_consumed();
  test_back_to_back_frames();
  return ::vh::test::summary("test_mavlink");
}
