#pragma once

// LiveMavlinkOutputSafetyGate (M13) — the explicit, fail-closed permission
// check that a future live-output writer (M16/M17) MUST clear before any
// command could ever be transmitted. It produces no output itself; it answers
// one question — "may this command be sent live right now?" — and, when the
// answer is no, lists EVERY reason explicitly.
//
// The default answer is "blocked": a freshly configured gate denies output
// until every condition is satisfied. Conditions cover operator/runtime
// authority, single-writer ownership, audit readiness, dry-run quality
// evidence, watchdog freshness (camera frame, telemetry, route match), and the
// command shape (valid, finite, bounded, exact zero forward speed for the
// first scope). See DECISIONS D-027.
//
// NOTHING here enables transmission. There is no writer. This is gate logic.

#include <cstdint>
#include <string>
#include <vector>

#include "vh/health.hpp"
#include "vh/navigation_command.hpp"
#include "vh/route_match.hpp"
#include "vh/telemetry.hpp"

namespace vh {

// Result of a safety evaluation. `allowed` is true only when block_reasons is
// empty. Reasons are stable lowercase tokens for logs/audit/checkers.
struct GateDecision {
  bool allowed = false;
  std::vector<std::string> block_reasons;

  [[nodiscard]] bool blocked() const noexcept { return !allowed; }
};

// Join reasons for a compact log: "a,b,c" or "none".
std::string format_block_reasons(const std::vector<std::string>& reasons);

struct SafetyGateConfig {
  // Operator / runtime authority — both default OFF (fail closed).
  bool runtime_enabled = false;     // live-output scope enabled at runtime
  bool operator_confirmed = false;  // explicit operator confirmation given

  // High-confidence threshold for the route match (mille).
  std::uint16_t min_confidence_mille = 800;

  // Watchdog freshness windows (ns).
  std::int64_t max_match_age_ns = 200'000'000;   // 200 ms
  std::int64_t max_frame_age_ns = 500'000'000;   // 500 ms (camera frame timeout)

  // Command bound: |yaw_rate_microradps| must not exceed this.
  std::int32_t max_yaw_rate_microradps = 1'000'000;  // 1 rad/s
};

// Everything the gate inspects for one decision. Ownership/audit/quality flags
// are supplied by the coordinating session; telemetry/health/match/command come
// from the live pipeline. now_ns is the injected clock (D-008/D-021).
struct SafetyGateInputs {
  bool single_writer_owned = false;   // the caller owns the one writer slot
  bool audit_ready = false;           // audit log is enabled and ready
  bool dry_run_quality_passed = false;

  HealthSnapshot health;
  TelemetrySnapshot telemetry;
  RouteMatch match;
  NavigationCommand command;
  std::int64_t now_ns = 0;
};

class LiveMavlinkOutputSafetyGate {
 public:
  explicit LiveMavlinkOutputSafetyGate(SafetyGateConfig config = {}) noexcept
      : cfg_(config) {}

  const SafetyGateConfig& config() const noexcept { return cfg_; }
  void set_config(SafetyGateConfig config) noexcept { cfg_ = config; }

  // Evaluate all conditions. Reasons are appended in a deterministic order so
  // a stuck condition always shows the same token.
  GateDecision evaluate(const SafetyGateInputs& in) const;

 private:
  SafetyGateConfig cfg_;
};

}  // namespace vh
