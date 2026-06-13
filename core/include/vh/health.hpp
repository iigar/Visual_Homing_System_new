#pragma once

// Health monitor: aggregates per-stage signals into a single HealthSnapshot
// and an explicit state machine. Single-threaded, deterministic, no clock
// access of its own — the caller passes "now" so tests are reproducible.
//
// State transitions (only these are legal):
//   Booting   -> Ready, Failsafe, Shutdown
//   Ready     -> Degraded, Failsafe, Shutdown
//   Degraded  -> Ready, Failsafe, Shutdown
//   Failsafe  -> Shutdown          (operator/process exit only)
//   Shutdown  -> (terminal)
//
// "Degraded" means at least one of camera_ok / mavlink_ok / navigation_ok is
// false. "Failsafe" is set explicitly by the caller — health on its own never
// promotes to Failsafe.

#include <cstdint>
#include <string>

namespace vh {

enum class HealthState : std::uint8_t {
  Booting = 0,
  Ready = 1,
  Degraded = 2,
  Failsafe = 3,
  Shutdown = 4,
};

const char* to_string(HealthState s) noexcept;

struct HealthSnapshot {
  // Counters / latencies populated by the pipeline.
  std::uint64_t frames_seen = 0;
  std::uint64_t frames_dropped = 0;
  std::int64_t last_frame_timestamp_ns = 0;
  std::int64_t now_ns = 0;
  std::int64_t frame_age_ns = 0;           // now_ns - last_frame_timestamp_ns
  std::int64_t processing_latency_ns = 0;  // last frame's preprocess time

  // Per-stage health flags.
  bool camera_ok = false;
  bool mavlink_ok = false;
  bool navigation_ok = false;

  // Route match confidence in [0.0, 1.0]; 0.0 if no fresh match.
  float route_match_confidence = 0.0f;

  HealthState state = HealthState::Booting;
};

struct HealthThresholds {
  std::int64_t max_frame_age_ns = 500'000'000;       // 500 ms
  std::int64_t max_processing_latency_ns = 200'000'000;  // 200 ms
};

class HealthMonitor {
 public:
  explicit HealthMonitor(HealthThresholds thresholds = {}) noexcept;

  // Called when a frame is observed entering the pipeline.
  void on_frame_seen(std::int64_t frame_timestamp_ns,
                     std::int64_t processing_latency_ns,
                     std::int64_t now_ns) noexcept;

  // Called when a frame is dropped (invalid / too late / preprocess refused).
  void on_frame_dropped(std::int64_t now_ns) noexcept;

  // External health signals (M8 telemetry, M7 navigation, hardware capture).
  void set_camera_ok(bool ok) noexcept;
  void set_mavlink_ok(bool ok) noexcept;
  void set_navigation_ok(bool ok) noexcept;
  void set_route_confidence(float c) noexcept;

  // Operator / failsafe controls.
  void request_failsafe() noexcept;
  void request_shutdown() noexcept;

  // Recompute state based on flags + thresholds + now.
  void update(std::int64_t now_ns) noexcept;

  const HealthSnapshot& snapshot() const noexcept { return snap_; }

 private:
  HealthThresholds th_;
  HealthSnapshot snap_;
  bool failsafe_requested_ = false;
  bool shutdown_requested_ = false;
};

}  // namespace vh
