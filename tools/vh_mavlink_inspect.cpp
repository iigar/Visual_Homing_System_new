// vh_mavlink_inspect: read raw MAVLink bytes from stdin, parse read-only, and
// print a stable key=value telemetry summary for capture/validation scripts.
// Never sends anything. Intended use on the Pi:
//   stty -F /dev/serial0 raw 115200 ; timeout 5 cat /dev/serial0 | vh_mavlink_inspect
//
// All bytes are stamped with a single synthetic receive time, so freshness is
// reported as age-from-last-byte (0 for a fresh live capture). Exit code 0 if
// at least one valid frame was decoded, else 1 (so checkers can gate on it).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "vh/telemetry.hpp"

int main(int argc, char** argv) {
  std::uint8_t expected_sysid = 0;
  if (argc >= 3 && std::string(argv[1]) == "--sysid") {
    expected_sysid = static_cast<std::uint8_t>(std::atoi(argv[2]));
  }

  vh::TelemetryConfig cfg;
  cfg.expected_sysid = expected_sysid;
  vh::MavlinkTelemetry telem(cfg);

  // Read all of stdin (binary).
  std::vector<std::uint8_t> buf;
  buf.reserve(1 << 16);
  std::uint8_t chunk[4096];
  std::size_t r = 0;
  while ((r = std::fread(chunk, 1, sizeof(chunk), stdin)) > 0) {
    buf.insert(buf.end(), chunk, chunk + r);
  }

  const std::int64_t now = 0;
  const std::size_t decoded = telem.ingest(buf.data(), buf.size(), now);
  telem.update(now);
  const vh::TelemetrySnapshot s = telem.snapshot();

  std::printf("bytes_seen=%llu\n",
              static_cast<unsigned long long>(s.counters.bytes_seen));
  std::printf("frames_ok=%llu\n",
              static_cast<unsigned long long>(s.counters.frames_ok));
  std::printf("frames_crc_error=%llu\n",
              static_cast<unsigned long long>(s.counters.frames_crc_error));
  std::printf("frames_unknown_msgid=%llu\n",
              static_cast<unsigned long long>(s.counters.frames_unknown_msgid));
  std::printf("bytes_discarded=%llu\n",
              static_cast<unsigned long long>(s.counters.bytes_discarded));

  std::printf("heartbeat_seen=%d\n", s.heartbeat_seen ? 1 : 0);
  std::printf("mavlink_ok=%d\n", s.mavlink_ok ? 1 : 0);
  if (s.heartbeat_seen) {
    std::printf("sysid=%u\n", s.sysid);
    std::printf("compid=%u\n", s.compid);
    std::printf("armed=%d\n", s.armed ? 1 : 0);
    std::printf("custom_mode=%u\n", s.custom_mode);
    std::printf("mode_label=%s\n", s.mode_label);
    std::printf("system_status=%u\n", s.system_status);
  }
  std::printf("attitude_seen=%d\n", s.attitude_seen ? 1 : 0);
  if (s.attitude_seen) {
    std::printf("roll=%.4f\n", static_cast<double>(s.roll));
    std::printf("pitch=%.4f\n", static_cast<double>(s.pitch));
    std::printf("yaw=%.4f\n", static_cast<double>(s.yaw));
  }
  std::printf("position_seen=%d\n", s.position_seen ? 1 : 0);
  if (s.position_seen) {
    std::printf("lat=%d\n", s.lat);
    std::printf("lon=%d\n", s.lon);
    std::printf("relative_alt_mm=%d\n", s.relative_alt_mm);
    std::printf("hdg_cdeg=%u\n", s.hdg_cdeg);
  }
  std::printf("decoded_frames=%zu\n", decoded);

  return decoded > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
