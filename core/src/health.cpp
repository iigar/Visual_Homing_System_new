#include "vh/health.hpp"

#include <algorithm>

namespace vh {

const char* to_string(HealthState s) noexcept {
  switch (s) {
    case HealthState::Booting: return "Booting";
    case HealthState::Ready: return "Ready";
    case HealthState::Degraded: return "Degraded";
    case HealthState::Failsafe: return "Failsafe";
    case HealthState::Shutdown: return "Shutdown";
  }
  return "Unknown";
}

HealthMonitor::HealthMonitor(HealthThresholds thresholds) noexcept
    : th_(thresholds) {}

void HealthMonitor::on_frame_seen(std::int64_t frame_timestamp_ns,
                                  std::int64_t processing_latency_ns,
                                  std::int64_t now_ns) noexcept {
  ++snap_.frames_seen;
  snap_.last_frame_timestamp_ns = frame_timestamp_ns;
  snap_.processing_latency_ns = processing_latency_ns;
  snap_.now_ns = now_ns;
  snap_.frame_age_ns = now_ns - frame_timestamp_ns;
}

void HealthMonitor::on_frame_dropped(std::int64_t now_ns) noexcept {
  ++snap_.frames_dropped;
  snap_.now_ns = now_ns;
  if (snap_.last_frame_timestamp_ns != 0) {
    snap_.frame_age_ns = now_ns - snap_.last_frame_timestamp_ns;
  }
}

void HealthMonitor::set_camera_ok(bool ok) noexcept { snap_.camera_ok = ok; }
void HealthMonitor::set_mavlink_ok(bool ok) noexcept { snap_.mavlink_ok = ok; }
void HealthMonitor::set_navigation_ok(bool ok) noexcept {
  snap_.navigation_ok = ok;
}
void HealthMonitor::set_route_confidence(float c) noexcept {
  if (c < 0.0f) c = 0.0f;
  if (c > 1.0f) c = 1.0f;
  snap_.route_match_confidence = c;
}

void HealthMonitor::request_failsafe() noexcept { failsafe_requested_ = true; }
void HealthMonitor::request_shutdown() noexcept { shutdown_requested_ = true; }

void HealthMonitor::update(std::int64_t now_ns) noexcept {
  snap_.now_ns = now_ns;
  if (snap_.last_frame_timestamp_ns != 0) {
    snap_.frame_age_ns = now_ns - snap_.last_frame_timestamp_ns;
  }

  // Terminal states are sticky.
  if (snap_.state == HealthState::Shutdown) return;
  if (shutdown_requested_) {
    snap_.state = HealthState::Shutdown;
    return;
  }
  if (failsafe_requested_) {
    snap_.state = HealthState::Failsafe;
    return;
  }
  // Once in Failsafe (without operator clearing it via shutdown), stay there.
  if (snap_.state == HealthState::Failsafe) return;

  const bool stale_frame = snap_.last_frame_timestamp_ns != 0 &&
                           snap_.frame_age_ns > th_.max_frame_age_ns;
  const bool slow_processing =
      snap_.processing_latency_ns > th_.max_processing_latency_ns;
  const bool any_subsystem_bad =
      !snap_.camera_ok || !snap_.mavlink_ok || !snap_.navigation_ok;

  if (snap_.state == HealthState::Booting) {
    if (snap_.frames_seen == 0) {
      return;  // no frames yet — keep booting
    }
    // First frame observed: classify directly into Ready or Degraded.
    snap_.state = (any_subsystem_bad || stale_frame || slow_processing)
                      ? HealthState::Degraded
                      : HealthState::Ready;
    return;
  }

  // Ready <-> Degraded
  if (any_subsystem_bad || stale_frame || slow_processing) {
    snap_.state = HealthState::Degraded;
  } else {
    snap_.state = HealthState::Ready;
  }
}

}  // namespace vh
