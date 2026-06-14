#include "vh/telemetry.hpp"

#include <bit>
#include <cstdint>

#include "vh/endian.hpp"

namespace vh {

namespace {

float load_le_f32(const std::uint8_t* p) noexcept {
  return std::bit_cast<float>(load_le32(p));
}

std::int16_t load_le_i16(const std::uint8_t* p) noexcept {
  return static_cast<std::int16_t>(load_le16(p));
}

}  // namespace

HeartbeatInfo decode_heartbeat(const MavlinkMessage& m) noexcept {
  HeartbeatInfo h;
  h.custom_mode = load_le32(&m.payload[0]);
  h.type = m.payload[4];
  h.autopilot = m.payload[5];
  h.base_mode = m.payload[6];
  h.system_status = m.payload[7];
  h.mavlink_version = m.payload[8];
  h.armed = (h.base_mode & kMavModeFlagSafetyArmed) != 0;
  return h;
}

AttitudeInfo decode_attitude(const MavlinkMessage& m) noexcept {
  AttitudeInfo a;
  a.time_boot_ms = load_le32(&m.payload[0]);
  a.roll = load_le_f32(&m.payload[4]);
  a.pitch = load_le_f32(&m.payload[8]);
  a.yaw = load_le_f32(&m.payload[12]);
  a.rollspeed = load_le_f32(&m.payload[16]);
  a.pitchspeed = load_le_f32(&m.payload[20]);
  a.yawspeed = load_le_f32(&m.payload[24]);
  return a;
}

GlobalPositionInfo decode_global_position_int(const MavlinkMessage& m) noexcept {
  GlobalPositionInfo g;
  g.time_boot_ms = load_le32(&m.payload[0]);
  g.lat = load_le_i32(&m.payload[4]);
  g.lon = load_le_i32(&m.payload[8]);
  g.alt_mm = load_le_i32(&m.payload[12]);
  g.relative_alt_mm = load_le_i32(&m.payload[16]);
  g.vx = load_le_i16(&m.payload[20]);
  g.vy = load_le_i16(&m.payload[22]);
  g.vz = load_le_i16(&m.payload[24]);
  g.hdg_cdeg = load_le16(&m.payload[26]);
  return g;
}

const char* copter_mode_label(std::uint32_t custom_mode) noexcept {
  switch (custom_mode) {
    case 0:  return "STABILIZE";
    case 1:  return "ACRO";
    case 2:  return "ALT_HOLD";
    case 3:  return "AUTO";
    case 4:  return "GUIDED";
    case 5:  return "LOITER";
    case 6:  return "RTL";
    case 7:  return "CIRCLE";
    case 9:  return "LAND";
    case 16: return "POSHOLD";
    case 17: return "BRAKE";
    case 20: return "GUIDED_NOGPS";
    case 21: return "SMART_RTL";
    default: return "UNKNOWN";
  }
}

MavlinkTelemetry::MavlinkTelemetry(TelemetryConfig config) noexcept
    : cfg_(config) {}

void MavlinkTelemetry::reset() noexcept {
  parser_.reset();
  snap_ = TelemetrySnapshot{};
}

void MavlinkTelemetry::apply(const MavlinkMessage& m,
                             std::int64_t now_ns) noexcept {
  // Untrusted-source filter: drop frames from an unexpected sysid entirely.
  if (cfg_.expected_sysid != 0 && m.sysid != cfg_.expected_sysid) {
    return;
  }

  switch (m.msgid) {
    case kMsgHeartbeat: {
      const HeartbeatInfo h = decode_heartbeat(m);
      snap_.heartbeat_seen = true;
      snap_.heartbeat_ts_ns = now_ns;
      snap_.type = h.type;
      snap_.autopilot = h.autopilot;
      snap_.base_mode = h.base_mode;
      snap_.system_status = h.system_status;
      snap_.custom_mode = h.custom_mode;
      snap_.armed = h.armed;
      snap_.mode_label = copter_mode_label(h.custom_mode);
      snap_.sysid = m.sysid;
      snap_.compid = m.compid;
      break;
    }
    case kMsgAttitude: {
      const AttitudeInfo a = decode_attitude(m);
      snap_.attitude_seen = true;
      snap_.attitude_ts_ns = now_ns;
      snap_.roll = a.roll;
      snap_.pitch = a.pitch;
      snap_.yaw = a.yaw;
      snap_.yawspeed = a.yawspeed;
      break;
    }
    case kMsgGlobalPositionInt: {
      const GlobalPositionInfo g = decode_global_position_int(m);
      snap_.position_seen = true;
      snap_.position_ts_ns = now_ns;
      snap_.lat = g.lat;
      snap_.lon = g.lon;
      snap_.alt_mm = g.alt_mm;
      snap_.relative_alt_mm = g.relative_alt_mm;
      snap_.hdg_cdeg = g.hdg_cdeg;
      break;
    }
    default:
      break;  // CRC-valid but not a message we surface
  }
}

std::size_t MavlinkTelemetry::ingest(const std::uint8_t* data, std::size_t n,
                                     std::int64_t now_ns) noexcept {
  std::size_t decoded = 0;
  MavlinkMessage m;
  for (std::size_t i = 0; i < n; ++i) {
    if (parser_.parse_byte(data[i], m)) {
      apply(m, now_ns);
      ++decoded;
    }
  }
  update(now_ns);
  return decoded;
}

void MavlinkTelemetry::update(std::int64_t now_ns) noexcept {
  snap_.counters = parser_.counters();

  snap_.heartbeat_age_ns =
      snap_.heartbeat_seen ? (now_ns - snap_.heartbeat_ts_ns) : 0;
  snap_.attitude_age_ns =
      snap_.attitude_seen ? (now_ns - snap_.attitude_ts_ns) : 0;
  snap_.position_age_ns =
      snap_.position_seen ? (now_ns - snap_.position_ts_ns) : 0;

  // Link is healthy only with a heartbeat that is present and fresh. A future
  // timestamp (negative age) is treated as not-fresh, fail-closed.
  snap_.mavlink_ok =
      snap_.heartbeat_seen && snap_.heartbeat_age_ns >= 0 &&
      snap_.heartbeat_age_ns <= cfg_.max_heartbeat_age_ns;
}

}  // namespace vh
