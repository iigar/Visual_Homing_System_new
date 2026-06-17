#include "vh/safety_gate.hpp"

#include <cmath>

namespace vh {

std::string format_block_reasons(const std::vector<std::string>& reasons) {
  if (reasons.empty()) return "none";
  std::string out;
  for (const auto& r : reasons) {
    if (!out.empty()) out += ',';
    out += r;
  }
  return out;
}

GateDecision LiveMavlinkOutputSafetyGate::evaluate(
    const SafetyGateInputs& in) const {
  GateDecision d;
  auto block = [&d](const char* reason) { d.block_reasons.emplace_back(reason); };

  // --- Operator / runtime authority ----------------------------------------
  if (!cfg_.runtime_enabled) block("runtime_not_enabled");
  if (!cfg_.operator_confirmed) block("operator_not_confirmed");

  // --- Ownership / audit / evidence ----------------------------------------
  if (!in.single_writer_owned) block("writer_not_owned");
  if (!in.audit_ready) block("audit_not_ready");
  if (!in.dry_run_quality_passed) block("dry_run_quality_not_passed");

  // --- Watchdog: camera frame freshness ------------------------------------
  if (in.health.frames_seen == 0 ||
      in.health.frame_age_ns > cfg_.max_frame_age_ns ||
      in.health.frame_age_ns < 0) {
    block("camera_frame_timeout");
  }

  // --- Watchdog: telemetry -------------------------------------------------
  if (!in.telemetry.mavlink_ok) block("telemetry_stale");
  if (!in.telemetry.armed) block("vehicle_not_armed");

  // --- Watchdog: route match freshness / quality ---------------------------
  if (!in.match.valid) {
    block("match_invalid");
  } else {
    const std::int64_t age = in.now_ns - in.match.timestamp_ns;
    if (age < 0 || age > cfg_.max_match_age_ns) block("match_stale");
    if (in.match.confidence_mille < cfg_.min_confidence_mille) {
      block("match_low_confidence");
    }
  }

  // --- Command shape -------------------------------------------------------
  if (!in.command.valid) block("command_invalid");
  if (!std::isfinite(in.command.yaw_rate_radps) ||
      !std::isfinite(in.command.vx_mps) || !std::isfinite(in.command.vy_mps)) {
    block("command_not_finite");
  }
  const std::int32_t yaw = in.command.yaw_rate_microradps;
  const std::int32_t mag = yaw < 0 ? -yaw : yaw;
  if (mag > cfg_.max_yaw_rate_microradps) block("command_out_of_bounds");

  // Exact zero forward speed for the first live-output scope.
  if (in.command.vx_mps != 0.0f || in.command.vy_mps != 0.0f) {
    block("forward_speed_nonzero");
  }

  d.allowed = d.block_reasons.empty();
  return d;
}

}  // namespace vh
