#pragma once

// Route-quality analysis (M6). All checks are stateless and deterministic
// so they produce identical reports on desktop and Pi.
//
// The report flows into the readiness checker script which gates whether a
// recorded route is usable as dry-run evidence. Quality is a PREFILTER —
// it never authorises flight (see prompt M6 docs note).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vh/route_entry.hpp"
#include "vh/route_matcher.hpp"

namespace vh {

struct SelfMatchReport {
  std::size_t checked = 0;
  std::size_t valid_matches = 0;
  std::size_t exact_index_matches = 0;
  std::uint16_t confidence_min_mille = 1000;
  std::uint16_t confidence_max_mille = 0;
  std::uint64_t confidence_sum_mille = 0;  // sum across all checked entries
  bool progress_monotonic = true;
  std::size_t first_non_monotonic_index = 0;
};

// Run every route entry through the matcher as if it were a live frame.
// A clean route must have every entry match its own index with valid=true.
SelfMatchReport self_match(const std::vector<RouteEntry>& route,
                           const MatcherConfig& cfg);

struct PerturbationReport {
  // Each row: how many of the `checked` perturbed frames still produced a
  // valid match and (separately) still matched their original index.
  std::size_t checked = 0;
  std::size_t brightness_valid = 0;
  std::size_t brightness_index_kept = 0;
  std::size_t noise_valid = 0;
  std::size_t noise_index_kept = 0;
  std::size_t shift_valid = 0;
  std::size_t shift_index_kept = 0;
  std::size_t malformed_rejected = 0;  // all malformed entries must be rejected
};

struct PerturbationConfig {
  std::int32_t brightness_delta = 20;   // uniform +delta on each pixel
  std::uint8_t noise_amplitude = 8;     // deterministic LCG noise amplitude
  std::int32_t horizontal_shift_px = 1;
  std::uint64_t noise_seed = 0xC0FFEEull;
};

PerturbationReport perturbation_check(const std::vector<RouteEntry>& route,
                                      const MatcherConfig& cfg,
                                      const PerturbationConfig& pcfg);

// ---------------------------------------------------------------------------
// Distinctiveness diagnostics
// ---------------------------------------------------------------------------

struct DistinctivenessReport {
  std::size_t checked = 0;

  // Per-entry counters.
  std::size_t low_texture_entries = 0;        // payload range below threshold
  std::size_t exact_duplicate_entries = 0;    // byte-equal to any neighbour
  std::size_t ambiguous_nearest_entries = 0;  // 2nd-best too close to best

  // Aggregates (MAD = mean absolute difference, not sum).
  std::uint64_t adjacent_mad_sum = 0;        // sum across i,i+1 pairs
  std::size_t adjacent_pairs = 0;
  std::uint64_t nearest_neighbour_mad_sum = 0;  // sum across entries
  std::size_t nearest_neighbour_pairs = 0;

  // First-violation samples for log diagnostics.
  std::vector<std::size_t> low_texture_indices;
  std::vector<std::size_t> exact_duplicate_indices;
  std::vector<std::size_t> ambiguous_nearest_indices;

  // Mean payload value range across the route (max - min over all bytes
  // across all entries) — quick texture proxy.
  std::uint8_t payload_min = 255;
  std::uint8_t payload_max = 0;
};

struct DistinctivenessConfig {
  // An entry whose Gray8 payload (max - min) is below this is "low texture".
  std::uint8_t low_texture_range = 20;

  // Best-vs-runner-up gap: if (runner_up_sad - best_sad) is smaller than
  // best_sad * ambiguous_ratio_mille / 1000, the entry is ambiguous.
  std::uint16_t ambiguous_ratio_mille = 100;  // 10% gap

  // Edge trim: skip this many entries from the start and end (useful when
  // a route has stationary frames during operator cue/countdown).
  std::size_t edge_trim = 0;
};

DistinctivenessReport distinctiveness(const std::vector<RouteEntry>& route,
                                      const DistinctivenessConfig& cfg);

// ---------------------------------------------------------------------------
// Quality policy
// ---------------------------------------------------------------------------

struct QualityPolicy {
  // Thresholds from prompt (Milestone 6).
  std::uint16_t low_texture_fraction_mille = 50;     // <= 0.05
  std::uint16_t ambiguous_nearest_fraction_mille = 100;  // <= 0.10
  std::uint64_t average_nearest_mad_min = 5;         // >= 5 (per-pixel)
  bool require_no_exact_duplicates = true;
  bool require_self_match_exact = true;
  bool require_malformed_rejected = true;
};

struct QualityVerdict {
  bool quality_pass = false;
  // Reasons for failure (key=value style for logs).
  std::vector<std::string> failures;
};

QualityVerdict evaluate_quality(const SelfMatchReport& sm,
                                const PerturbationReport& pr,
                                const DistinctivenessReport& dr,
                                const QualityPolicy& policy);

}  // namespace vh
