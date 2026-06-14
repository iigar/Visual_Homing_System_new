#pragma once

// Read-only MAVLink telemetry aggregation (M8). Decodes the three messages
// this milestone needs (HEARTBEAT, ATTITUDE, GLOBAL_POSITION_INT) from
// CRC-validated frames and folds them into a TelemetrySnapshot. Freshness is
// computed against a caller-supplied clock (D-008 convention), so a missing or
// stale heartbeat drops mavlink_ok — untrusted serial input never silently
// keeps the link "healthy".
//
// This layer produces NO commands. It only reports what the autopilot said.
// CRC validity is enforced upstream by MavlinkParser; an optional expected
// sysid filter additionally drops frames from unexpected sources so injected
// traffic with a wrong sysid cannot populate the snapshot.

#include <cstddef>
#include <cstdint>

#include "vh/interfaces.hpp"
#include "vh/mavlink.hpp"

namespace vh {

// MAV_MODE_FLAG_SAFETY_ARMED — bit in HEARTBEAT.base_mode meaning "armed".
inline constexpr std::uint8_t kMavModeFlagSafetyArmed = 0x80;

// --- Pure decoders (exposed for direct unit tests) --------------------------
struct HeartbeatInfo {
  std::uint32_t custom_mode = 0;
  std::uint8_t type = 0;
  std::uint8_t autopilot = 0;
  std::uint8_t base_mode = 0;
  std::uint8_t system_status = 0;
  std::uint8_t mavlink_version = 0;
  bool armed = false;
};

struct AttitudeInfo {
  std::uint32_t time_boot_ms = 0;
  float roll = 0.0f;
  float pitch = 0.0f;
  float yaw = 0.0f;
  float rollspeed = 0.0f;
  float pitchspeed = 0.0f;
  float yawspeed = 0.0f;
};

struct GlobalPositionInfo {
  std::uint32_t time_boot_ms = 0;
  std::int32_t lat = 0;           // degE7
  std::int32_t lon = 0;           // degE7
  std::int32_t alt_mm = 0;        // mm, MSL
  std::int32_t relative_alt_mm = 0;  // mm, above home
  std::int16_t vx = 0;            // cm/s
  std::int16_t vy = 0;
  std::int16_t vz = 0;
  std::uint16_t hdg_cdeg = 0;     // centidegrees, 0..35999 (65535 = unknown)
};

HeartbeatInfo decode_heartbeat(const MavlinkMessage& m) noexcept;
AttitudeInfo decode_attitude(const MavlinkMessage& m) noexcept;
GlobalPositionInfo decode_global_position_int(const MavlinkMessage& m) noexcept;

// ArduCopter flight-mode label for HEARTBEAT.custom_mode. Returns a stable
// uppercase string; unknown modes yield "UNKNOWN".
const char* copter_mode_label(std::uint32_t custom_mode) noexcept;

// --- Snapshot ---------------------------------------------------------------
struct TelemetrySnapshot {
  // Heartbeat / armed / mode.
  bool heartbeat_seen = false;
  std::int64_t heartbeat_ts_ns = 0;
  std::int64_t heartbeat_age_ns = 0;
  std::uint8_t type = 0;
  std::uint8_t autopilot = 0;
  std::uint8_t base_mode = 0;
  std::uint8_t system_status = 0;
  std::uint32_t custom_mode = 0;
  bool armed = false;
  const char* mode_label = "UNKNOWN";

  // Attitude (radians; from the autopilot's float fields).
  bool attitude_seen = false;
  std::int64_t attitude_ts_ns = 0;
  std::int64_t attitude_age_ns = 0;
  float roll = 0.0f;
  float pitch = 0.0f;
  float yaw = 0.0f;
  float yawspeed = 0.0f;

  // Global position / relative altitude.
  bool position_seen = false;
  std::int64_t position_ts_ns = 0;
  std::int64_t position_age_ns = 0;
  std::int32_t lat = 0;
  std::int32_t lon = 0;
  std::int32_t alt_mm = 0;
  std::int32_t relative_alt_mm = 0;
  std::uint16_t hdg_cdeg = 0;

  // Last source identity that updated the heartbeat (untrusted; informational).
  std::uint8_t sysid = 0;
  std::uint8_t compid = 0;

  // Parser byte/frame counters mirrored for diagnostics.
  MavlinkCounters counters;

  // Derived link health: heartbeat seen AND fresh.
  bool mavlink_ok = false;
};

struct TelemetryConfig {
  // Heartbeat older than this drops mavlink_ok. ArduPilot beats at ~1 Hz.
  std::int64_t max_heartbeat_age_ns = 2'000'000'000;  // 2 s

  // If nonzero, only frames from this sysid update the snapshot. Zero accepts
  // any source (still CRC-validated). Use to ignore injected/foreign traffic.
  std::uint8_t expected_sysid = 0;
};

// --- Aggregator -------------------------------------------------------------
class MavlinkTelemetry : public ITelemetrySource {
 public:
  explicit MavlinkTelemetry(TelemetryConfig config = {}) noexcept;

  // Feed received serial bytes. Each decoded message is stamped with now_ns as
  // its receive time. Returns the number of valid frames decoded from this
  // chunk.
  std::size_t ingest(const std::uint8_t* data, std::size_t n,
                     std::int64_t now_ns) noexcept;

  // Recompute ages + mavlink_ok against the clock. Call before reading the
  // snapshot, mirroring HealthMonitor's clock-injection pattern.
  void update(std::int64_t now_ns) noexcept;

  TelemetrySnapshot snapshot() const override { return snap_; }

  void reset() noexcept;

 private:
  void apply(const MavlinkMessage& m, std::int64_t now_ns) noexcept;

  TelemetryConfig cfg_;
  MavlinkParser parser_;
  TelemetrySnapshot snap_;
};

}  // namespace vh
