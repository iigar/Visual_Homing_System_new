#include "vh/navigator.hpp"

#include <cmath>
#include <cstdint>

namespace vh {

namespace {

// A zero, explicitly-invalid command stamped with whatever time we know.
NavigationCommand zero_command(std::int64_t timestamp_ns) noexcept {
  NavigationCommand c;
  c.timestamp_ns = timestamp_ns;
  c.vx_mps = 0.0f;
  c.vy_mps = 0.0f;
  c.yaw_rate_microradps = 0;
  c.yaw_rate_radps = 0.0f;
  c.confidence_mille = 0;
  c.confidence = 0.0f;
  c.valid = false;
  return c;
}

// Reject NaN/Inf riding in on the float projections. The integer fields drive
// the math, but a non-finite float anywhere in the inputs signals corrupt
// upstream state we refuse to act on.
bool inputs_finite(const RouteMatch& m, const HealthSnapshot& h) noexcept {
  return std::isfinite(m.confidence) && std::isfinite(m.progress) &&
         std::isfinite(h.route_match_confidence);
}

}  // namespace

std::int64_t yaw_rate_from_error(std::int32_t direction_error_millirad,
                                 std::int32_t gain_milli) noexcept {
  return static_cast<std::int64_t>(direction_error_millirad) *
         static_cast<std::int64_t>(gain_milli);
}

std::int64_t clamp_symmetric(std::int64_t value, std::int64_t limit) noexcept {
  if (limit < 0) limit = 0;
  if (value > limit) return limit;
  if (value < -limit) return -limit;
  return value;
}

std::int32_t slew_limit(std::int32_t previous, std::int32_t target,
                        std::int32_t max_step) noexcept {
  if (max_step < 0) max_step = 0;
  const std::int64_t delta =
      static_cast<std::int64_t>(target) - static_cast<std::int64_t>(previous);
  const std::int64_t step = clamp_symmetric(delta, max_step);
  return static_cast<std::int32_t>(static_cast<std::int64_t>(previous) + step);
}

BoundedNavigator::BoundedNavigator(NavigatorConfig config) noexcept
    : cfg_(config) {}

void BoundedNavigator::reset() noexcept { last_yaw_rate_microradps_ = 0; }

NavigationCommand BoundedNavigator::propose(const RouteMatch& match,
                                            const HealthSnapshot& health) {
  // The command is stamped with the freshest time we have — the health
  // snapshot's now_ns — so the audit trail (M13) sees decision time, not
  // match time.
  const std::int64_t now_ns = health.now_ns;

  // --- Gate chain. Any failure -> reset slew memory and fail closed. -------
  const bool gate_health_ready = health.state == HealthState::Ready;
  const bool gate_stage_flags =
      health.camera_ok && health.mavlink_ok && health.navigation_ok;
  const bool gate_match_valid = match.valid;
  const bool gate_finite = inputs_finite(match, health);
  const bool gate_confidence = match.confidence_mille >= cfg_.min_confidence_mille;

  const std::int64_t age_ns = now_ns - match.timestamp_ns;
  const bool gate_age = age_ns >= 0 && age_ns <= cfg_.max_match_age_ns;

  if (!(gate_health_ready && gate_stage_flags && gate_match_valid &&
        gate_finite && gate_confidence && gate_age)) {
    reset();
    return zero_command(now_ns);
  }

  // --- Bounded yaw-rate-only command. --------------------------------------
  const std::int64_t raw =
      yaw_rate_from_error(match.direction_error_millirad, cfg_.gain_milli);
  const std::int64_t clamped = clamp_symmetric(
      raw, static_cast<std::int64_t>(cfg_.max_yaw_rate_microradps));
  const std::int32_t target = static_cast<std::int32_t>(clamped);
  const std::int32_t slewed =
      slew_limit(last_yaw_rate_microradps_, target, cfg_.max_slew_microradps);

  last_yaw_rate_microradps_ = slewed;

  NavigationCommand c;
  c.timestamp_ns = now_ns;
  c.vx_mps = 0.0f;  // yaw-rate-only scope: no forward authority
  c.vy_mps = 0.0f;  // yaw-rate-only scope: no lateral authority
  c.yaw_rate_microradps = slewed;
  c.yaw_rate_radps = static_cast<float>(slewed) /
                     static_cast<float>(NavigationCommand::kMicroPerRad);
  c.confidence_mille = match.confidence_mille;
  c.confidence = match.confidence;
  c.valid = true;
  return c;
}

}  // namespace vh
