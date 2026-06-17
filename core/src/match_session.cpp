#include "vh/match_session.hpp"

#include <cstdio>

namespace vh {

const char* to_string(ExpectedProgress e) noexcept {
  switch (e) {
    case ExpectedProgress::Any: return "any";
    case ExpectedProgress::Forward: return "forward";
    case ExpectedProgress::Reverse: return "reverse";
  }
  return "unknown";
}

DryRunMatchSession::DryRunMatchSession(DryRunBridge& bridge,
                                       MatchSessionConfig config,
                                       IRouteMatcher* matcher) noexcept
    : bridge_(bridge),
      cfg_(config),
      matcher_(matcher),
      base_blocked_stale_(bridge.counters().blocked_stale),
      base_blocked_incompatible_(bridge.counters().blocked_incompatible) {}

bool DryRunMatchSession::at_endpoint(std::uint16_t progress_mille) const noexcept {
  if (cfg_.expected_progress == ExpectedProgress::Reverse) {
    const std::uint16_t threshold =
        RouteMatch::kMille >= cfg_.endpoint_progress_gate_mille
            ? static_cast<std::uint16_t>(RouteMatch::kMille -
                                         cfg_.endpoint_progress_gate_mille)
            : 0;
    return progress_mille <= threshold;
  }
  return progress_mille >= cfg_.endpoint_progress_gate_mille;
}

NavigationCommand DryRunMatchSession::step_match(
    const RouteMatch& match, const HealthSnapshot& base_health,
    std::int64_t now_ns) noexcept {
  ++frames_;
  if (frames_ == 1) first_ts_ns_ = now_ns;
  last_ts_ns_ = now_ns;

  bool reached_now = false;
  if (match.valid) {
    ++valid_matches_;
    confidence_sum_ += match.confidence_mille;
    if (match.confidence_mille < confidence_min_) {
      confidence_min_ = match.confidence_mille;
    }

    const std::uint16_t p = match.progress_mille;
    if (!progress_first_) progress_first_ = p;
    progress_last_ = p;
    if (p < progress_min_) progress_min_ = p;
    if (p > progress_max_) progress_max_ = p;

    if (prev_progress_) {
      const int delta = static_cast<int>(p) - static_cast<int>(*prev_progress_);
      const bool backward =
          (cfg_.expected_progress == ExpectedProgress::Forward && delta < 0) ||
          (cfg_.expected_progress == ExpectedProgress::Reverse && delta > 0);
      if (backward) {
        ++regressions_;
        rollback_total_ += static_cast<std::uint64_t>(delta < 0 ? -delta : delta);
      }
    }
    if (prev_index_) {
      const std::int64_t d = static_cast<std::int64_t>(match.route_index) -
                             static_cast<std::int64_t>(*prev_index_);
      if (d > 1 || d < -1) ++index_jumps_;
    }
    prev_progress_ = p;
    prev_index_ = match.route_index;

    if (!endpoint_passed_ && at_endpoint(p)) {
      endpoint_passed_ = true;
      reached_now = true;
    }
  }

  NavigationCommand cmd{};
  if (!stopped_) {
    cmd = bridge_.tick(match, base_health, now_ns);
    ++dry_run_total_;
    if (cmd.valid) ++dry_run_valid_;
    // The live-output boundary is hard-closed (no M13 gate, no M16/M17 writer):
    // every command that would face it is blocked, never allowed.
    ++live_output_blocked_;
    block_reason_recorded_ = true;
  }

  if (reached_now) {
    stopped_ = true;
    stop_reason_ = "endpoint_reached";
  }
  return cmd;
}

NavigationCommand DryRunMatchSession::step(const Frame& frame,
                                           const HealthSnapshot& base_health,
                                           std::int64_t now_ns) {
  RouteMatch m;
  m.timestamp_ns = frame.timestamp_ns;
  if (matcher_ != nullptr) {
    m = matcher_->match(frame);
  }
  return step_match(m, base_health, now_ns);
}

MatchSessionResult DryRunMatchSession::finish() noexcept {
  MatchSessionResult r;
  r.frames = frames_;
  r.requested_frames = cfg_.requested_frame_count;
  r.configured_fps = cfg_.configured_fps;

  if (frames_ > 1 && last_ts_ns_ > first_ts_ns_) {
    const double span_ns = static_cast<double>(last_ts_ns_ - first_ts_ns_);
    r.effective_fps = static_cast<double>(frames_ - 1) * 1e9 / span_ns;
    r.elapsed_ms = (last_ts_ns_ - first_ts_ns_) / 1'000'000;
  }

  r.valid_matches = valid_matches_;
  r.progress_first_mille = progress_first_.value_or(0);
  r.progress_last_mille = progress_last_;
  r.progress_min_mille = valid_matches_ > 0 ? progress_min_ : 0;
  r.progress_max_mille = progress_max_;
  r.progress_regressions = regressions_;
  r.rollback_total_mille = rollback_total_;
  r.index_jumps = index_jumps_;

  r.endpoint_passed = endpoint_passed_;
  if (cfg_.expected_progress == ExpectedProgress::Reverse) {
    const std::uint16_t threshold =
        RouteMatch::kMille >= cfg_.endpoint_progress_gate_mille
            ? static_cast<std::uint16_t>(RouteMatch::kMille -
                                         cfg_.endpoint_progress_gate_mille)
            : 0;
    r.progress_gate_passed = valid_matches_ > 0 && progress_min_ <= threshold;
  } else {
    r.progress_gate_passed =
        valid_matches_ > 0 && progress_max_ >= cfg_.endpoint_progress_gate_mille;
  }

  r.confidence_min_mille = valid_matches_ > 0 ? confidence_min_ : 0;
  r.confidence_avg_mille =
      valid_matches_ > 0
          ? static_cast<std::uint16_t>(confidence_sum_ / valid_matches_)
          : 0;

  const std::uint64_t ds =
      bridge_.counters().blocked_stale - base_blocked_stale_;
  const std::uint64_t di =
      bridge_.counters().blocked_incompatible - base_blocked_incompatible_;
  r.telemetry_dropped = ds;
  r.telemetry_health = dry_run_total_ > 0 && ds == 0 && di == 0;

  r.dry_run_quality = cfg_.dry_run_quality_passed;
  r.dry_run_valid = dry_run_valid_;
  r.dry_run_total = dry_run_total_;

  r.live_output_gate_allowed = 0;
  r.live_output_gate_blocked = live_output_blocked_;
  if (live_output_blocked_ > 0) {
    r.live_output_gate_block_reasons.emplace_back("live_output_disabled");
  }

  r.stop_reason = stopped_ ? stop_reason_ : "exhausted";

  const bool all_frames = cfg_.requested_frame_count == 0 ||
                          frames_ == cfg_.requested_frame_count;
  const bool matches_ok = frames_ > 0 && valid_matches_ == frames_;
  const bool conf_ok =
      valid_matches_ > 0 && r.confidence_min_mille >= cfg_.min_confidence_mille;
  const bool regress_ok = regressions_ <= cfg_.max_regressions;
  const bool jumps_ok = index_jumps_ <= cfg_.max_index_jumps;
  const bool dr_ok = dry_run_total_ > 0 && dry_run_valid_ == dry_run_total_;

  r.passed = all_frames && matches_ok && r.progress_gate_passed &&
             r.endpoint_passed && conf_ok && regress_ok && jumps_ok &&
             r.telemetry_health && r.dry_run_quality && dr_ok &&
             r.live_output_gate_allowed == 0;
  return r;
}

std::string format_compact_log(const MatchSessionResult& r) {
  char buf[768];
  char fps[32];
  std::snprintf(fps, sizeof(fps), "%.1f", r.effective_fps);

  std::string reasons;
  for (const auto& s : r.live_output_gate_block_reasons) {
    if (!reasons.empty()) reasons += ',';
    reasons += s;
  }
  if (reasons.empty()) reasons = "none";

  std::snprintf(
      buf, sizeof(buf),
      "passed=%d frames=%u/%u effective_fps=%s configured_fps=%u elapsed_ms=%lld "
      "valid_matches=%u progress=%u..%u progress_first=%u progress_last=%u "
      "regressions=%u rollback_total=%llu index_jumps=%u endpoint_passed=%d "
      "progress_gate_passed=%d confidence_min_avg=%u/%u telemetry_health=%d "
      "telemetry_dropped=%llu dry_run_quality=%d dry_run_valid=%u/%u "
      "live_output_gate_allowed=%llu live_output_gate_blocked=%llu "
      "live_output_gate_block_reasons=%s stop_reason=%s",
      r.passed ? 1 : 0, r.frames, r.requested_frames, fps, r.configured_fps,
      static_cast<long long>(r.elapsed_ms), r.valid_matches,
      r.progress_min_mille, r.progress_max_mille, r.progress_first_mille,
      r.progress_last_mille, r.progress_regressions,
      static_cast<unsigned long long>(r.rollback_total_mille), r.index_jumps,
      r.endpoint_passed ? 1 : 0, r.progress_gate_passed ? 1 : 0,
      r.confidence_min_mille, r.confidence_avg_mille, r.telemetry_health ? 1 : 0,
      static_cast<unsigned long long>(r.telemetry_dropped),
      r.dry_run_quality ? 1 : 0, r.dry_run_valid, r.dry_run_total,
      static_cast<unsigned long long>(r.live_output_gate_allowed),
      static_cast<unsigned long long>(r.live_output_gate_blocked),
      reasons.c_str(), r.stop_reason.c_str());
  return std::string(buf);
}

}  // namespace vh
