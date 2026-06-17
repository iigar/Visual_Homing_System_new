#include <string>

#include "vh/command_sink.hpp"
#include "vh/live_output.hpp"
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
  in.health.frame_age_ns = 1'000'000;
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
  in.command.confidence_mille = 900;
  in.now_ns = kNow;
  return in;
}

vh::NavigationCommand a_command() {
  vh::NavigationCommand c;
  c.valid = true;
  c.yaw_rate_microradps = 50'000;
  c.confidence_mille = 900;
  return c;
}

// -------------------------------------------------------------------------
void test_audit_readiness_fail_closed() {
  vh::LiveMavlinkOutputAuditLog audit;  // not ready
  VH_EXPECT(!audit.ready());
  VH_EXPECT(!audit.record_start("go"));
  VH_EXPECT(audit.size() == 0);

  audit.set_ready(true);
  VH_EXPECT(audit.record_start("go"));
  VH_EXPECT(audit.counters().starts == 1);
  VH_EXPECT(audit.size() == 1);
}

void test_audit_write_failure() {
  vh::LiveMavlinkOutputAuditLog audit(true);
  audit.set_write_should_fail(true);
  vh::GateDecision d;
  d.allowed = false;
  d.block_reasons.emplace_back("vehicle_not_armed");
  VH_EXPECT(!audit.record_decision(a_command(), d));
  VH_EXPECT(audit.counters().decisions == 0);
}

void test_audit_decision_counts() {
  vh::LiveMavlinkOutputAuditLog audit(true);
  vh::GateDecision allowed;
  allowed.allowed = true;
  vh::GateDecision blocked;
  blocked.block_reasons.emplace_back("telemetry_stale");
  VH_EXPECT(audit.record_decision(a_command(), allowed));
  VH_EXPECT(audit.record_decision(a_command(), blocked));
  VH_EXPECT(audit.counters().decisions == 2);
  VH_EXPECT(audit.counters().allowed == 1);
  VH_EXPECT(audit.counters().blocked == 1);
}

void test_live_bridge_unavailable() {
  vh::LiveMavlinkBridge bridge;
  VH_EXPECT(!bridge.available());
  VH_EXPECT(!bridge.start());
  VH_EXPECT(!bridge.send(a_command()));
  VH_EXPECT(!bridge.started());
}

void test_session_start_fail_closed() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  vh::LiveMavlinkOutputAuditLog audit;  // not ready
  vh::DryRunCommandSink sink;
  vh::LiveMavlinkBridge bridge;
  vh::LiveMavlinkOutputSession s(gate, audit, sink, bridge);
  VH_EXPECT(!s.start("go"));  // audit not ready -> fail closed
  VH_EXPECT(!s.started());

  audit.set_ready(true);
  VH_EXPECT(s.start("go"));
  VH_EXPECT(s.started());
  VH_EXPECT(audit.counters().starts == 1);
}

void test_session_blocked_decision_audited() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  vh::LiveMavlinkOutputAuditLog audit(true);
  vh::DryRunCommandSink sink;
  sink.start();
  vh::LiveMavlinkBridge bridge;
  vh::LiveMavlinkOutputSession s(gate, audit, sink, bridge);
  VH_EXPECT(s.start("go"));

  auto in = green_inputs();
  in.telemetry.armed = false;  // the only block in an otherwise green scenario
  const auto d = s.tick(in);
  VH_EXPECT(!d.allowed);
  VH_EXPECT(d.block_reasons.size() == 1);
  VH_EXPECT(d.block_reasons[0] == "vehicle_not_armed");
  VH_EXPECT(s.counters().blocked == 1);
  VH_EXPECT(s.counters().allowed == 0);
  VH_EXPECT(s.block_reason_counts().at("vehicle_not_armed") == 1);
  VH_EXPECT(audit.counters().decisions == 1);
  VH_EXPECT(audit.counters().blocked == 1);
  VH_EXPECT(sink.history_size() == 1);  // dry-run recorded the blocked command
}

void test_session_allowed_decision_never_transmits() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  vh::LiveMavlinkOutputAuditLog audit(true);
  vh::DryRunCommandSink sink;
  sink.start();
  vh::LiveMavlinkBridge bridge;
  vh::LiveMavlinkOutputSession s(gate, audit, sink, bridge);
  VH_EXPECT(s.start("go"));

  const auto d = s.tick(green_inputs());  // fully green
  VH_EXPECT(d.allowed);
  VH_EXPECT(s.counters().allowed == 1);
  VH_EXPECT(s.counters().live_rejected == 1);  // live bridge refused (no TX)
  VH_EXPECT(audit.counters().allowed == 1);
  VH_EXPECT(sink.history_size() == 1);
}

void test_session_not_started_blocks() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  vh::LiveMavlinkOutputAuditLog audit(true);
  vh::DryRunCommandSink sink;
  vh::LiveMavlinkBridge bridge;
  vh::LiveMavlinkOutputSession s(gate, audit, sink, bridge);

  const auto d = s.tick(green_inputs());  // never started
  VH_EXPECT(!d.allowed);
  VH_EXPECT(d.block_reasons[0] == "session_not_started");
  VH_EXPECT(audit.counters().decisions == 0);
}

void test_session_endpoint_stops_generation() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  vh::LiveMavlinkOutputAuditLog audit(true);
  vh::DryRunCommandSink sink;
  sink.start();
  vh::LiveMavlinkBridge bridge;
  vh::LiveMavlinkOutputSession s(gate, audit, sink, bridge);
  VH_EXPECT(s.start("go"));

  VH_EXPECT(s.mark_endpoint());
  VH_EXPECT(s.stopped());
  // The stop record carries the explicit end-of-route action.
  const auto& last = audit.at(audit.size() - 1);
  VH_EXPECT(last.kind == vh::AuditRecord::Kind::Stop);
  VH_EXPECT(last.reason == "endpoint_progress_reached");

  // Post-endpoint ticks are refused — no command generation past the endpoint.
  const auto d = s.tick(green_inputs());
  VH_EXPECT(!d.allowed);
  VH_EXPECT(d.block_reasons[0] == "session_stopped");
}

void test_session_operator_stop() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  vh::LiveMavlinkOutputAuditLog audit(true);
  vh::DryRunCommandSink sink;
  vh::LiveMavlinkBridge bridge;
  vh::LiveMavlinkOutputSession s(gate, audit, sink, bridge);
  VH_EXPECT(s.start("go"));
  VH_EXPECT(s.stop("operator_stop"));
  VH_EXPECT(s.stopped());
  const auto& last = audit.at(audit.size() - 1);
  VH_EXPECT(last.kind == vh::AuditRecord::Kind::Stop);
  VH_EXPECT(last.reason == "operator_stop");
  VH_EXPECT(!s.stop("again"));  // already stopped
}

void test_session_audit_failure_during_tick() {
  vh::LiveMavlinkOutputSafetyGate gate(green_cfg());
  vh::LiveMavlinkOutputAuditLog audit(true);
  vh::DryRunCommandSink sink;
  sink.start();
  vh::LiveMavlinkBridge bridge;
  vh::LiveMavlinkOutputSession s(gate, audit, sink, bridge);
  VH_EXPECT(s.start("go"));

  audit.set_write_should_fail(true);  // audit write fails on the next decision
  const auto d = s.tick(green_inputs());
  VH_EXPECT(!d.allowed);
  bool found = false;
  for (const auto& r : d.block_reasons) {
    if (r == "audit_write_failed") found = true;
  }
  VH_EXPECT(found);
  VH_EXPECT(s.counters().audit_failures == 1);
  VH_EXPECT(s.stopped());          // session stops fail-closed
  VH_EXPECT(sink.history_size() == 0);  // nothing routed
}

}  // namespace

int main() {
  test_audit_readiness_fail_closed();
  test_audit_write_failure();
  test_audit_decision_counts();
  test_live_bridge_unavailable();
  test_session_start_fail_closed();
  test_session_blocked_decision_audited();
  test_session_allowed_decision_never_transmits();
  test_session_not_started_blocks();
  test_session_endpoint_stops_generation();
  test_session_operator_stop();
  test_session_audit_failure_during_tick();
  return ::vh::test::summary("test_live_output");
}
