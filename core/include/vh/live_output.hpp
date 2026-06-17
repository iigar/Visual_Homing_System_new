#pragma once

// Live-output safety scaffolding (M13). Three fail-closed pieces that frame the
// future live-output boundary WITHOUT enabling any transmission:
//
//   * LiveMavlinkOutputAuditLog — records start/decision/stop. Refuses to
//     record when not ready; a write failure is itself a block signal.
//   * LiveMavlinkBridge          — the writer-shaped stub. Disabled by default,
//     rejects every start()/send(). The real transport lands in M17 behind the
//     explicit bench-props-off + writer-attach CMake chain.
//   * LiveMavlinkOutputSession   — the coordinator: starts the audit, evaluates
//     the safety gate per tick, audits allowed AND blocked decisions, routes
//     commands to the dry-run sink (never the live bridge, which refuses), and
//     records an explicit end-of-route action.
//
// No command is ever transmitted by this milestone. See DECISIONS D-027.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "vh/command_sink.hpp"
#include "vh/interfaces.hpp"
#include "vh/navigation_command.hpp"
#include "vh/safety_gate.hpp"

namespace vh {

// --- Audit log --------------------------------------------------------------
struct AuditRecord {
  enum class Kind : std::uint8_t { Start, Decision, Stop };

  Kind kind = Kind::Start;
  std::uint64_t seq = 0;
  std::string reason;  // start/stop reason; decision: block reasons or "allowed"
  bool allowed = false;
  bool command_valid = false;     // decision: the proposed command's validity
  float vx_mps = 0.0f;            // decision: forward speed (must be 0 in scope)
  std::int32_t yaw_rate_microradps = 0;
  std::uint16_t confidence_mille = 0;
};

struct AuditCounters {
  std::uint64_t starts = 0;
  std::uint64_t decisions = 0;
  std::uint64_t stops = 0;
  std::uint64_t allowed = 0;
  std::uint64_t blocked = 0;
};

class LiveMavlinkOutputAuditLog : public IAuditLog {
 public:
  explicit LiveMavlinkOutputAuditLog(bool ready = false) noexcept
      : ready_(ready) {}

  // Readiness is the master fail-closed switch: nothing is recorded until ready.
  void set_ready(bool ready) noexcept { ready_ = ready; }
  bool ready() const override { return ready_; }

  // Test/fault hook: simulate an audit write failure (disk full, etc.).
  void set_write_should_fail(bool fail) noexcept { write_should_fail_ = fail; }

  bool record_start(const std::string& reason) override;
  bool record_decision(const NavigationCommand& command,
                       const GateDecision& decision) override;
  bool record_stop(const std::string& reason) override;

  std::size_t size() const noexcept { return records_.size(); }
  const AuditRecord& at(std::size_t i) const noexcept { return records_.at(i); }
  const AuditCounters& counters() const noexcept { return counters_; }

  // Render the whole log as one event per line (for the audit readiness
  // checker and stored evidence). Stable key=value format.
  std::string format_log() const;

 private:
  bool can_write() const noexcept { return ready_ && !write_should_fail_; }

  bool ready_ = false;
  bool write_should_fail_ = false;
  std::uint64_t seq_ = 0;
  std::vector<AuditRecord> records_;
  AuditCounters counters_;
};

// --- Live bridge stub (fail-closed) ----------------------------------------
// Writer-shaped, but disabled: every operation is refused. The real writer is
// out of scope until M17 and only behind VH_ENABLE_WRITER_ATTACH (which the
// top-level CMake already chains to VH_ENABLE_LIVE_OUTPUT + props-off).
class LiveMavlinkBridge : public ICommandSink {
 public:
  // Whether a live writer is actually available. Always false this milestone.
  bool available() const noexcept {
#if VH_ENABLE_WRITER_ATTACH
    return false;  // M13: stub only — real attach lands in M17.
#else
    return false;  // compiled-out / disabled by default.
#endif
  }

  bool start() noexcept { return false; }  // rejects starts
  bool send(const NavigationCommand&) override { return false; }  // never sends
  bool started() const override { return false; }

  const char* reject_reason() const noexcept { return "live_bridge_unavailable"; }
};

// --- Session coordinator ----------------------------------------------------
struct LiveSessionCounters {
  std::uint64_t ticks = 0;
  std::uint64_t allowed = 0;        // gate-allowed decisions
  std::uint64_t blocked = 0;        // gate-blocked decisions
  std::uint64_t live_rejected = 0;  // allowed but live bridge refused (no TX)
  std::uint64_t audit_failures = 0;
};

class LiveMavlinkOutputSession {
 public:
  LiveMavlinkOutputSession(LiveMavlinkOutputSafetyGate& gate,
                           LiveMavlinkOutputAuditLog& audit,
                           DryRunCommandSink& dry_sink,
                           LiveMavlinkBridge& live_bridge) noexcept;

  // Start the session. Fail-closed: returns false (and starts nothing) if the
  // audit log is not ready or the start record cannot be written.
  bool start(const std::string& reason);

  // One coordinated decision. Evaluates the gate, audits the decision, routes
  // the command to the dry-run sink (never to the refusing live bridge).
  // Returns the gate decision (with "session_not_started"/"session_stopped"
  // when the session is not in a running state, and "audit_write_failed" if the
  // audit refused the write — which also stops the session).
  GateDecision tick(const SafetyGateInputs& in);

  // Explicit end-of-route action: record it and stop command generation so no
  // post-endpoint command is ever issued.
  bool mark_endpoint();

  // Operator/process stop: emit a final stop record.
  bool stop(const std::string& reason);

  bool started() const noexcept { return started_ && !stopped_; }
  bool stopped() const noexcept { return stopped_; }

  const LiveSessionCounters& counters() const noexcept { return counters_; }
  const std::map<std::string, std::uint64_t>& block_reason_counts()
      const noexcept {
    return block_reason_counts_;
  }

 private:
  LiveMavlinkOutputSafetyGate& gate_;
  LiveMavlinkOutputAuditLog& audit_;
  DryRunCommandSink& dry_sink_;
  LiveMavlinkBridge& live_bridge_;

  bool started_ = false;
  bool stopped_ = false;
  LiveSessionCounters counters_;
  std::map<std::string, std::uint64_t> block_reason_counts_;
};

}  // namespace vh
