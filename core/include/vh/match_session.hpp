#pragma once

// DryRunMatchSession (M12) — drives a "replay the recorded route" loop entirely
// in dry-run. Per frame it asks the matcher (M5) where on the route the live
// frame sits, folds the result through the DryRunBridge (M9: navigator +
// telemetry-health gate + dry-run command sink), and accumulates the evidence
// the prompt's compact log requires.
//
// It treats route-recording speed vs return/match speed as an explicit
// validation variable: it tracks progress first/last/min/max, counts progress
// regressions and rollback magnitude, and counts route-index jumps. A large
// speed mismatch shows up as regressions, jumps, endpoint misses, or false
// endpoint passes — visible in the log rather than silently acted upon.
//
// Endpoint action (fail-closed): once progress crosses the endpoint gate in the
// expected direction, the session STOPS generating commands, records a stop
// reason, and emits no further route-following commands — control returns to
// the autopilot / no-live-output boundary.
//
// Live output remains impossible: there is no LiveMavlinkOutputSafetyGate yet
// (M13) and no writer (M16/M17). Every command that would face that boundary is
// counted as BLOCKED with an explicit reason; allowed is always zero.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vh/dry_run_bridge.hpp"
#include "vh/frame.hpp"
#include "vh/health.hpp"
#include "vh/interfaces.hpp"
#include "vh/route_match.hpp"
#include "vh/safety_gate.hpp"

namespace vh {

enum class ExpectedProgress : std::uint8_t { Any = 0, Forward = 1, Reverse = 2 };

const char* to_string(ExpectedProgress e) noexcept;

struct MatchSessionConfig {
  ExpectedProgress expected_progress = ExpectedProgress::Forward;

  // Progress (mille) at/after which we consider the route endpoint reached.
  // Forward/Any: endpoint when progress >= gate. Reverse: progress <= 1000-gate.
  std::uint16_t endpoint_progress_gate_mille = 950;

  // Minimum match confidence for the run to pass (the matcher applies its own
  // per-frame threshold; this is the session-level floor on confidence_min).
  std::uint16_t min_confidence_mille = 600;

  // Expected number of live frames; 0 disables the "got all frames" check.
  std::uint32_t requested_frame_count = 0;

  // Capture FPS the camera was configured for (logged; not enforced).
  std::uint32_t configured_fps = 0;

  // Tolerances for the speed-mismatch validation variable.
  std::uint32_t max_regressions = 0;
  std::uint32_t max_index_jumps = 0;

  // Route-quality verdict from M6 (dry-run quality gate). Fed in by the caller.
  bool dry_run_quality_passed = false;
};

struct MatchSessionResult {
  bool passed = false;

  std::uint32_t frames = 0;
  std::uint32_t requested_frames = 0;
  std::uint32_t configured_fps = 0;
  double effective_fps = 0.0;
  std::int64_t elapsed_ms = 0;

  std::uint32_t valid_matches = 0;

  // Progress accounting (mille).
  std::uint16_t progress_first_mille = 0;
  std::uint16_t progress_last_mille = 0;
  std::uint16_t progress_min_mille = 0;
  std::uint16_t progress_max_mille = 0;
  std::uint32_t progress_regressions = 0;
  std::uint64_t rollback_total_mille = 0;  // summed backward progress magnitude
  std::uint32_t index_jumps = 0;           // |route_index - prev| > 1

  bool endpoint_passed = false;
  bool progress_gate_passed = false;

  std::uint16_t confidence_min_mille = 0;
  std::uint16_t confidence_avg_mille = 0;

  bool telemetry_health = false;
  std::uint64_t telemetry_dropped = 0;  // ticks blocked by stale telemetry

  bool dry_run_quality = false;
  std::uint32_t dry_run_valid = 0;  // valid dry-run commands recorded
  std::uint32_t dry_run_total = 0;  // command attempts (pre-endpoint frames)

  std::uint64_t live_output_gate_allowed = 0;  // 0 in dry-run / bench readiness
  std::uint64_t live_output_gate_blocked = 0;
  // reason -> count. With a safety gate attached these are the real gate
  // reasons (e.g. {"vehicle_not_armed": N}); without one, {"live_output_disabled": N}.
  std::map<std::string, std::uint64_t> live_output_gate_block_reason_counts;

  std::string stop_reason;  // "endpoint_reached" / "exhausted" / "" (running)
};

// Static (per-session) flags the safety gate needs that are not in the command
// stream. Supplied once via DryRunMatchSession::set_live_output_gate.
struct LiveOutputContext {
  bool single_writer_owned = false;
  bool audit_ready = false;
};

class DryRunMatchSession {
 public:
  // The bridge supplies the navigator + telemetry-health gate + dry-run sink.
  // A matcher is only required to use step(frame); step_match(match) feeds a
  // ready-made RouteMatch (used by tests and callers that match elsewhere).
  DryRunMatchSession(DryRunBridge& bridge, MatchSessionConfig config,
                     IRouteMatcher* matcher = nullptr) noexcept;

  // Process one already-matched frame. base_health is the pipeline health
  // (camera/nav); the bridge overlays telemetry freshness onto command
  // validity. now_ns is the injected clock (D-008/D-021).
  NavigationCommand step_match(const RouteMatch& match,
                               const HealthSnapshot& base_health,
                               std::int64_t now_ns) noexcept;

  // Match `frame` with the matcher supplied at construction, then step_match.
  // If no matcher was supplied, the frame is treated as an invalid match.
  NavigationCommand step(const Frame& frame, const HealthSnapshot& base_health,
                         std::int64_t now_ns);

  // Attach a live-output safety gate (M13). When set, each command attempt is
  // evaluated by the gate and the real block reasons (with counts) populate the
  // live-output accounting instead of the placeholder "live_output_disabled".
  // The gate must outlive the session.
  void set_live_output_gate(const LiveMavlinkOutputSafetyGate& gate,
                            LiveOutputContext ctx) noexcept;

  // Finalise aggregates and compute pass/fail. Idempotent.
  MatchSessionResult finish() noexcept;

  bool stopped() const noexcept { return stopped_; }

 private:
  bool at_endpoint(std::uint16_t progress_mille) const noexcept;

  DryRunBridge& bridge_;
  MatchSessionConfig cfg_;
  IRouteMatcher* matcher_;

  // Running state.
  std::uint32_t frames_ = 0;
  std::uint32_t valid_matches_ = 0;
  std::optional<std::uint16_t> progress_first_;
  std::uint16_t progress_last_ = 0;
  std::uint16_t progress_min_ = RouteMatch::kMille;
  std::uint16_t progress_max_ = 0;
  std::optional<std::uint16_t> prev_progress_;
  std::optional<std::uint32_t> prev_index_;
  std::uint32_t regressions_ = 0;
  std::uint64_t rollback_total_ = 0;
  std::uint32_t index_jumps_ = 0;
  std::uint64_t confidence_sum_ = 0;
  std::uint16_t confidence_min_ = RouteMatch::kMille;
  bool endpoint_passed_ = false;
  bool stopped_ = false;
  std::string stop_reason_;
  std::uint32_t dry_run_valid_ = 0;
  std::uint32_t dry_run_total_ = 0;
  std::uint64_t live_output_allowed_ = 0;
  std::uint64_t live_output_blocked_ = 0;
  std::map<std::string, std::uint64_t> live_block_counts_;
  std::int64_t first_ts_ns_ = 0;
  std::int64_t last_ts_ns_ = 0;

  // Optional live-output safety gate (M13) + its static context.
  const LiveMavlinkOutputSafetyGate* live_gate_ = nullptr;
  LiveOutputContext live_ctx_;

  // Bridge counter baselines (so deltas are session-scoped).
  std::uint64_t base_blocked_stale_ = 0;
  std::uint64_t base_blocked_incompatible_ = 0;
};

// Compact one-line key=value log (prompt M12). Stable ordering for readiness
// checkers and diffs.
std::string format_compact_log(const MatchSessionResult& r);

}  // namespace vh
