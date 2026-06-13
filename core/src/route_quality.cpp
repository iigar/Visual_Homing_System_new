#include "vh/route_quality.hpp"

#include <algorithm>
#include <sstream>

#include "vh/frame.hpp"

namespace vh {

namespace {

Frame entry_as_frame(const RouteEntry& e) {
  Frame f;
  f.id = e.frame_id;
  f.timestamp_ns = e.timestamp_ns;
  f.width = e.width;
  f.height = e.height;
  f.format = e.format;
  f.payload = e.payload;
  return f;
}

// Deterministic LCG noise — independent of std::rand(), bit-exact across
// platforms. Returns a value in [-amp, +amp].
inline std::int32_t lcg_step(std::uint64_t& s) noexcept {
  s = s * 6364136223846793005ull + 1442695040888963407ull;
  // Use top 8 bits.
  return static_cast<std::int32_t>((s >> 56) & 0xFF);
}

std::vector<std::uint8_t> apply_brightness(const std::vector<std::uint8_t>& in,
                                           std::int32_t delta) {
  std::vector<std::uint8_t> out(in.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    int v = static_cast<int>(in[i]) + delta;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    out[i] = static_cast<std::uint8_t>(v);
  }
  return out;
}

std::vector<std::uint8_t> apply_noise(const std::vector<std::uint8_t>& in,
                                      std::uint8_t amp, std::uint64_t seed) {
  std::vector<std::uint8_t> out(in.size());
  std::uint64_t s = seed;
  for (std::size_t i = 0; i < in.size(); ++i) {
    const int n =
        (lcg_step(s) % (2 * static_cast<int>(amp) + 1)) - static_cast<int>(amp);
    int v = static_cast<int>(in[i]) + n;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    out[i] = static_cast<std::uint8_t>(v);
  }
  return out;
}

std::vector<std::uint8_t> apply_shift_h(const std::vector<std::uint8_t>& in,
                                        std::uint32_t w, std::uint32_t h,
                                        std::int32_t s) {
  std::vector<std::uint8_t> out(in.size(), 0);
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::int32_t x = 0; x < static_cast<std::int32_t>(w); ++x) {
      const std::int32_t sx = x - s;
      if (sx >= 0 && sx < static_cast<std::int32_t>(w)) {
        out[y * w + x] = in[y * w + sx];
      }
    }
  }
  return out;
}

}  // namespace

SelfMatchReport self_match(const std::vector<RouteEntry>& route,
                           const MatcherConfig& cfg) {
  SelfMatchReport r;
  if (route.empty()) return r;

  // Matcher must run stateless for self-match — no sliding window memory.
  MatcherConfig cfg_stateless = cfg;
  cfg_stateless.window_radius = 0;
  Gray8RouteMatcher m(route, cfg_stateless);

  std::uint32_t prev_progress = 0;
  for (std::size_t i = 0; i < route.size(); ++i) {
    const Frame frame = entry_as_frame(route[i]);
    const RouteMatch match = m.match_stateless(frame);
    ++r.checked;
    if (match.valid) ++r.valid_matches;
    if (match.valid && match.route_index == i) ++r.exact_index_matches;

    r.confidence_min_mille =
        std::min(r.confidence_min_mille, match.confidence_mille);
    r.confidence_max_mille =
        std::max(r.confidence_max_mille, match.confidence_mille);
    r.confidence_sum_mille += match.confidence_mille;

    if (i > 0 && r.progress_monotonic &&
        match.progress_mille < prev_progress) {
      r.progress_monotonic = false;
      r.first_non_monotonic_index = i;
    }
    prev_progress = match.progress_mille;
  }
  return r;
}

// ---------------------------------------------------------------------------
// Distinctiveness
// ---------------------------------------------------------------------------

namespace {

std::uint64_t mad_pair(const RouteEntry& a, const RouteEntry& b) noexcept {
  if (a.payload.size() != b.payload.size()) return 0;
  std::uint64_t sum = 0;
  for (std::size_t i = 0; i < a.payload.size(); ++i) {
    const int d = static_cast<int>(a.payload[i]) - static_cast<int>(b.payload[i]);
    sum += static_cast<std::uint64_t>(d >= 0 ? d : -d);
  }
  return a.payload.empty() ? 0 : sum / a.payload.size();
}

void update_payload_range(const RouteEntry& e, std::uint8_t& lo,
                          std::uint8_t& hi) noexcept {
  for (auto b : e.payload) {
    if (b < lo) lo = b;
    if (b > hi) hi = b;
  }
}

std::pair<std::uint8_t, std::uint8_t> entry_range(const RouteEntry& e) noexcept {
  if (e.payload.empty()) return {0, 0};
  std::uint8_t lo = 255, hi = 0;
  update_payload_range(e, lo, hi);
  return {lo, hi};
}

}  // namespace

DistinctivenessReport distinctiveness(const std::vector<RouteEntry>& route,
                                      const DistinctivenessConfig& cfg) {
  DistinctivenessReport r;
  if (route.size() <= 2 * cfg.edge_trim) return r;

  const std::size_t lo_i = cfg.edge_trim;
  const std::size_t hi_i = route.size() - cfg.edge_trim;

  for (std::size_t i = lo_i; i < hi_i; ++i) {
    ++r.checked;
    const auto& e = route[i];

    const auto [emin, emax] = entry_range(e);
    r.payload_min = std::min(r.payload_min, emin);
    r.payload_max = std::max(r.payload_max, emax);
    const std::uint8_t range = static_cast<std::uint8_t>(emax - emin);
    if (range < cfg.low_texture_range) {
      ++r.low_texture_entries;
      if (r.low_texture_indices.size() < 8)
        r.low_texture_indices.push_back(i);
    }

    // Find best (excluding self) and runner-up entries by per-pixel MAD.
    std::uint64_t best = UINT64_MAX, runner = UINT64_MAX;
    std::size_t best_j = i;
    for (std::size_t j = lo_i; j < hi_i; ++j) {
      if (j == i) continue;
      const std::uint64_t m = mad_pair(e, route[j]);
      if (m < best) {
        runner = best;
        best = m;
        best_j = j;
      } else if (m < runner) {
        runner = m;
      }
    }
    (void)best_j;

    if (best != UINT64_MAX) {
      r.nearest_neighbour_mad_sum += best;
      ++r.nearest_neighbour_pairs;
      if (best == 0) {
        ++r.exact_duplicate_entries;
        if (r.exact_duplicate_indices.size() < 8)
          r.exact_duplicate_indices.push_back(i);
      }
      // Ambiguous: runner-up too close to best.
      if (runner != UINT64_MAX) {
        const std::uint64_t gap = runner - best;
        const std::uint64_t threshold =
            (best * cfg.ambiguous_ratio_mille) / 1000;
        if (gap < threshold || (best == 0 && runner == 0)) {
          ++r.ambiguous_nearest_entries;
          if (r.ambiguous_nearest_indices.size() < 8)
            r.ambiguous_nearest_indices.push_back(i);
        }
      }
    }

    if (i + 1 < hi_i) {
      r.adjacent_mad_sum += mad_pair(e, route[i + 1]);
      ++r.adjacent_pairs;
    }
  }
  return r;
}

// ---------------------------------------------------------------------------
// Quality policy
// ---------------------------------------------------------------------------

QualityVerdict evaluate_quality(const SelfMatchReport& sm,
                                const PerturbationReport& pr,
                                const DistinctivenessReport& dr,
                                const QualityPolicy& policy) {
  QualityVerdict v;
  v.quality_pass = true;

  if (dr.checked > 0) {
    const std::uint64_t low_tex_mille =
        (static_cast<std::uint64_t>(dr.low_texture_entries) * 1000) / dr.checked;
    const std::uint64_t ambig_mille =
        (static_cast<std::uint64_t>(dr.ambiguous_nearest_entries) * 1000) /
        dr.checked;
    if (low_tex_mille > policy.low_texture_fraction_mille) {
      std::ostringstream s;
      s << "low_texture_fraction_mille=" << low_tex_mille
        << " over " << policy.low_texture_fraction_mille;
      v.failures.push_back(s.str());
      v.quality_pass = false;
    }
    if (ambig_mille > policy.ambiguous_nearest_fraction_mille) {
      std::ostringstream s;
      s << "ambiguous_nearest_fraction_mille=" << ambig_mille
        << " over " << policy.ambiguous_nearest_fraction_mille;
      v.failures.push_back(s.str());
      v.quality_pass = false;
    }
  }

  if (dr.nearest_neighbour_pairs > 0) {
    const std::uint64_t avg =
        dr.nearest_neighbour_mad_sum / dr.nearest_neighbour_pairs;
    if (avg < policy.average_nearest_mad_min) {
      std::ostringstream s;
      s << "average_nearest_mad=" << avg
        << " under " << policy.average_nearest_mad_min;
      v.failures.push_back(s.str());
      v.quality_pass = false;
    }
  }

  if (policy.require_no_exact_duplicates && dr.exact_duplicate_entries > 0) {
    std::ostringstream s;
    s << "exact_duplicate_entries=" << dr.exact_duplicate_entries;
    v.failures.push_back(s.str());
    v.quality_pass = false;
  }

  if (policy.require_self_match_exact &&
      sm.exact_index_matches != sm.checked) {
    std::ostringstream s;
    s << "self_match_exact=" << sm.exact_index_matches << "/" << sm.checked;
    v.failures.push_back(s.str());
    v.quality_pass = false;
  }

  if (policy.require_malformed_rejected &&
      pr.malformed_rejected != pr.checked) {
    std::ostringstream s;
    s << "malformed_rejected=" << pr.malformed_rejected << "/" << pr.checked;
    v.failures.push_back(s.str());
    v.quality_pass = false;
  }

  return v;
}

PerturbationReport perturbation_check(const std::vector<RouteEntry>& route,
                                      const MatcherConfig& cfg,
                                      const PerturbationConfig& pcfg) {
  PerturbationReport r;
  if (route.empty()) return r;

  MatcherConfig cfg_stateless = cfg;
  cfg_stateless.window_radius = 0;
  Gray8RouteMatcher m(route, cfg_stateless);

  for (std::size_t i = 0; i < route.size(); ++i) {
    const auto& e = route[i];
    if (e.format != PixelFormat::Gray8) continue;
    ++r.checked;

    // Brightness perturbation.
    {
      Frame f = entry_as_frame(e);
      f.payload = apply_brightness(e.payload, pcfg.brightness_delta);
      const auto m_res = m.match_stateless(f);
      if (m_res.valid) ++r.brightness_valid;
      if (m_res.valid && m_res.route_index == i) ++r.brightness_index_kept;
    }
    // Deterministic noise perturbation.
    {
      Frame f = entry_as_frame(e);
      f.payload = apply_noise(e.payload, pcfg.noise_amplitude,
                              pcfg.noise_seed ^ i);
      const auto m_res = m.match_stateless(f);
      if (m_res.valid) ++r.noise_valid;
      if (m_res.valid && m_res.route_index == i) ++r.noise_index_kept;
    }
    // Horizontal shift perturbation.
    {
      Frame f = entry_as_frame(e);
      f.payload = apply_shift_h(e.payload, e.width, e.height,
                                pcfg.horizontal_shift_px);
      const auto m_res = m.match_stateless(f);
      if (m_res.valid) ++r.shift_valid;
      if (m_res.valid && m_res.route_index == i) ++r.shift_index_kept;
    }
    // Malformed payload: drop bytes so the live frame is invalid. The
    // matcher must refuse to produce a valid match.
    {
      Frame f = entry_as_frame(e);
      if (!f.payload.empty()) f.payload.pop_back();
      const auto m_res = m.match_stateless(f);
      if (!m_res.valid) ++r.malformed_rejected;
    }
  }
  return r;
}

}  // namespace vh
