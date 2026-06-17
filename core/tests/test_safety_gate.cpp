#include <cmath>
#include <limits>
#include <string>

#include "vh/safety_gate.hpp"
#include "vh_test.hpp"

namespace {

constexpr std::int64_t kNow = 1'000'000'000;

vh::SafetyGateConfig green_cfg() {
  vh::SafetyGateConfig c;
  c.runtime_enabled = true;
  c.operator_confirmed = true;
  return c;
}

vh::SafetyGateInputs green_inputs() {
  vh::SafetyGateInputs in;
  in.single_writer_owned = true;
  in.audit_ready = true;
  in.dry_run_quality_passed = true;

  in.health.frames_seen = 10;
  in.health.frame_age_ns = 1'000'000;  // 1 ms
  in.health.state = vh::HealthState::Ready;
  in.health.camera_ok = true;
  in.health.now_ns = kNow;

  in.telemetry.mavlink_ok = true;
  in.telemetry.armed = true;

  in.match.valid = true;
  in.match.timestamp_ns = kNow;
  in.match.confidence_mille = 900;

  in.command.valid = true;
  in.command.yaw_rate_microradps = 50'000;
  in.command.yaw_rate_radps = 0.05f;
  in.command.vx_mps = 0.0f;
  in.command.vy_mps = 0.0f;
  in.command.confidence_mille = 900;

  in.now_ns = kNow;
  return in;
}

bool has(const vh::GateDecision& d, const std::string& reason) {
  for (const auto& r : d.block_reasons) {
    if (r == reason) return true;
  }
  return false;
}

// -------------------------------------------------------------------------
void test_all_green_allowed() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  const auto d = gate.evaluate(green_inputs());
  VH_EXPECT(d.allowed);
  VH_EXPECT(d.block_reasons.empty());
}

void test_runtime_not_enabled() {
  auto cfg = green_cfg();
  cfg.runtime_enabled = false;
  vh::LiveMavlinkOutputSafetyGate gate(cfg);
  const auto d = gate.evaluate(green_inputs());
  VH_EXPECT(!d.allowed);
  VH_EXPECT(has(d, "runtime_not_enabled"));
}

void test_operator_not_confirmed() {
  auto cfg = green_cfg();
  cfg.operator_confirmed = false;
  vh::LiveMavlinkOutputSafetyGate gate(cfg);
  const auto d = gate.evaluate(green_inputs());
  VH_EXPECT(has(d, "operator_not_confirmed"));
}

void test_writer_not_owned() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.single_writer_owned = false;
  VH_EXPECT(has(gate.evaluate(in), "writer_not_owned"));
}

void test_audit_not_ready() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.audit_ready = false;
  VH_EXPECT(has(gate.evaluate(in), "audit_not_ready"));
}

void test_dry_run_quality_not_passed() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.dry_run_quality_passed = false;
  VH_EXPECT(has(gate.evaluate(in), "dry_run_quality_not_passed"));
}

void test_camera_frame_timeout() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.health.frames_seen = 0;  // no frames yet
  VH_EXPECT(has(gate.evaluate(in), "camera_frame_timeout"));

  in = green_inputs();
  in.health.frame_age_ns = 2'000'000'000;  // 2 s stale
  VH_EXPECT(has(gate.evaluate(in), "camera_frame_timeout"));
}

void test_telemetry_stale() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.telemetry.mavlink_ok = false;
  VH_EXPECT(has(gate.evaluate(in), "telemetry_stale"));
}

void test_vehicle_not_armed() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.telemetry.armed = false;
  VH_EXPECT(has(gate.evaluate(in), "vehicle_not_armed"));
}

void test_match_invalid() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.match.valid = false;
  VH_EXPECT(has(gate.evaluate(in), "match_invalid"));
}

void test_match_stale() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.match.timestamp_ns = kNow - 500'000'000;  // 500 ms old > 200 ms
  VH_EXPECT(has(gate.evaluate(in), "match_stale"));
}

void test_match_low_confidence() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.match.confidence_mille = 500;  // < 800
  VH_EXPECT(has(gate.evaluate(in), "match_low_confidence"));
}

void test_command_invalid() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.command.valid = false;
  VH_EXPECT(has(gate.evaluate(in), "command_invalid"));
}

void test_command_not_finite() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.command.yaw_rate_radps = std::numeric_limits<float>::infinity();
  VH_EXPECT(has(gate.evaluate(in), "command_not_finite"));
}

void test_command_out_of_bounds() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.command.yaw_rate_microradps = 5'000'000;  // > 1 rad/s bound
  VH_EXPECT(has(gate.evaluate(in), "command_out_of_bounds"));
}

void test_forward_speed_nonzero() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  auto in = green_inputs();
  in.command.vx_mps = 1.0f;
  VH_EXPECT(has(gate.evaluate(in), "forward_speed_nonzero"));
}

void test_default_config_blocks() {
  // A freshly configured gate denies by default (fail closed).
  vh::LiveMavlinkOutputSafetyGate gate;  // default config
  const auto d = gate.evaluate(green_inputs());
  VH_EXPECT(!d.allowed);
  VH_EXPECT(has(d, "runtime_not_enabled"));
  VH_EXPECT(has(d, "operator_not_confirmed"));
}

}  // namespace

int main() {
  test_all_green_allowed();
  test_runtime_not_enabled();
  test_operator_not_confirmed();
  test_writer_not_owned();
  test_audit_not_ready();
  test_dry_run_quality_not_passed();
  test_camera_frame_timeout();
  test_telemetry_stale();
  test_vehicle_not_armed();
  test_match_invalid();
  test_match_stale();
  test_match_low_confidence();
  test_command_invalid();
  test_command_not_finite();
  test_command_out_of_bounds();
  test_forward_speed_nonzero();
  test_default_config_blocks();
  return ::vh::test::summary("test_safety_gate");
}
