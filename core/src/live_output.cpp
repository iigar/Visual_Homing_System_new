#include "vh/live_output.hpp"

namespace vh {

// --- Audit log --------------------------------------------------------------
bool LiveMavlinkOutputAuditLog::record_start(const std::string& reason) {
  if (!can_write()) return false;
  records_.push_back(AuditRecord{AuditRecord::Kind::Start, seq_++, reason, false,
                                 0, 0});
  ++counters_.starts;
  return true;
}

bool LiveMavlinkOutputAuditLog::record_decision(
    const NavigationCommand& command, const GateDecision& decision) {
  if (!can_write()) return false;
  AuditRecord r;
  r.kind = AuditRecord::Kind::Decision;
  r.seq = seq_++;
  r.reason = decision.allowed ? "allowed"
                              : format_block_reasons(decision.block_reasons);
  r.allowed = decision.allowed;
  r.yaw_rate_microradps = command.yaw_rate_microradps;
  r.confidence_mille = command.confidence_mille;
  records_.push_back(r);
  ++counters_.decisions;
  if (decision.allowed) {
    ++counters_.allowed;
  } else {
    ++counters_.blocked;
  }
  return true;
}

bool LiveMavlinkOutputAuditLog::record_stop(const std::string& reason) {
  if (!can_write()) return false;
  records_.push_back(AuditRecord{AuditRecord::Kind::Stop, seq_++, reason, false,
                                 0, 0});
  ++counters_.stops;
  return true;
}

// --- Session coordinator ----------------------------------------------------
LiveMavlinkOutputSession::LiveMavlinkOutputSession(
    LiveMavlinkOutputSafetyGate& gate, LiveMavlinkOutputAuditLog& audit,
    DryRunCommandSink& dry_sink, LiveMavlinkBridge& live_bridge) noexcept
    : gate_(gate),
      audit_(audit),
      dry_sink_(dry_sink),
      live_bridge_(live_bridge) {}

bool LiveMavlinkOutputSession::start(const std::string& reason) {
  // Fail-closed: cannot start without a ready audit log + a written start.
  if (!audit_.ready()) return false;
  if (!audit_.record_start(reason)) return false;
  started_ = true;
  stopped_ = false;
  return true;
}

GateDecision LiveMavlinkOutputSession::tick(const SafetyGateInputs& in) {
  ++counters_.ticks;

  GateDecision d;
  if (!started_) {
    d.block_reasons.emplace_back("session_not_started");
    return d;
  }
  if (stopped_) {
    d.block_reasons.emplace_back("session_stopped");
    return d;
  }

  d = gate_.evaluate(in);

  // Audit every decision (allowed and blocked). A write failure blocks output
  // and stops the session — the autopilot keeps control.
  if (!audit_.record_decision(in.command, d)) {
    ++counters_.audit_failures;
    stopped_ = true;
    GateDecision failed;
    failed.block_reasons = d.block_reasons;
    failed.block_reasons.emplace_back("audit_write_failed");
    return failed;  // nothing routed anywhere
  }

  // Dry-run record only. The live bridge is never used to transmit.
  dry_sink_.send(in.command);

  if (d.allowed) {
    ++counters_.allowed;
    // Even an allowed decision cannot transmit: the live bridge refuses.
    if (!live_bridge_.send(in.command)) ++counters_.live_rejected;
  } else {
    ++counters_.blocked;
    for (const auto& r : d.block_reasons) ++block_reason_counts_[r];
  }
  return d;
}

bool LiveMavlinkOutputSession::mark_endpoint() {
  if (!started_ || stopped_) return false;
  const bool ok = audit_.record_stop("endpoint_progress_reached");
  stopped_ = true;  // stop command generation regardless of the record result
  return ok;
}

bool LiveMavlinkOutputSession::stop(const std::string& reason) {
  if (!started_ || stopped_) return false;
  const bool ok = audit_.record_stop(reason);
  stopped_ = true;
  return ok;
}

}  // namespace vh
