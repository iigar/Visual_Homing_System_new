#include "vh/route_matcher.hpp"

#include <algorithm>
#include <cstdlib>

namespace vh {

// =============================================================================
// Kernels
// =============================================================================

std::uint64_t sum_abs_diff(const std::uint8_t* a, const std::uint8_t* b,
                           std::size_t n) noexcept {
  std::uint64_t s = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const int diff = static_cast<int>(a[i]) - static_cast<int>(b[i]);
    s += static_cast<std::uint64_t>(diff >= 0 ? diff : -diff);
  }
  return s;
}

std::uint8_t mean_gray8(const std::uint8_t* p, std::size_t n) noexcept {
  if (n == 0) return 0;
  std::uint64_t s = 0;
  for (std::size_t i = 0; i < n; ++i) s += p[i];
  // Half-up rounding, integer-only.
  const std::uint64_t half = n / 2;
  const std::uint64_t avg = (s + half) / n;
  return static_cast<std::uint8_t>(avg > 255 ? 255 : avg);
}

std::uint16_t mad_confidence_mille(std::uint64_t sum_abs_diff_value,
                                   std::size_t pixel_count,
                                   std::uint8_t max_value) noexcept {
  if (pixel_count == 0 || max_value == 0) return 0;
  const std::uint64_t worst =
      static_cast<std::uint64_t>(pixel_count) * max_value;
  if (sum_abs_diff_value >= worst) return 0;
  // 1000 * (worst - sum) / worst, rounded half-up.
  const std::uint64_t numerator =
      static_cast<std::uint64_t>(1000) * (worst - sum_abs_diff_value);
  const std::uint64_t conf = (numerator + worst / 2) / worst;
  return static_cast<std::uint16_t>(conf > 1000 ? 1000 : conf);
}

// =============================================================================
// Matcher
// =============================================================================

Gray8RouteMatcher::Gray8RouteMatcher(std::vector<RouteEntry> route,
                                     MatcherConfig config)
    : route_(std::move(route)), cfg_(config) {}

namespace {

// Per-entry MAD, with optional mean-normalisation. Returns SIZE_MAX on
// dimension/format mismatch so the caller treats this entry as worst.
std::uint64_t entry_sum_abs_diff(const RouteEntry& entry,
                                 const Frame& live,
                                 bool mean_normalise) noexcept {
  if (!entry.valid() || !live.valid()) return UINT64_MAX;
  if (entry.format != live.format) return UINT64_MAX;
  if (entry.width != live.width || entry.height != live.height) {
    return UINT64_MAX;
  }
  if (entry.format != PixelFormat::Gray8) return UINT64_MAX;

  const std::size_t n = entry.payload.size();
  const std::uint8_t* a = entry.payload.data();
  const std::uint8_t* b = live.payload.data();

  if (!mean_normalise) {
    return sum_abs_diff(a, b, n);
  }

  const int ma = static_cast<int>(mean_gray8(a, n));
  const int mb = static_cast<int>(mean_gray8(b, n));
  const int delta = ma - mb;  // shift `b` by +delta to align means

  std::uint64_t s = 0;
  for (std::size_t i = 0; i < n; ++i) {
    int diff = static_cast<int>(a[i]) - (static_cast<int>(b[i]) + delta);
    s += static_cast<std::uint64_t>(diff >= 0 ? diff : -diff);
  }
  return s;
}

}  // namespace

RouteMatch Gray8RouteMatcher::match_stateless(const Frame& live) const {
  RouteMatch result;
  result.timestamp_ns = live.timestamp_ns;
  result.route_total = static_cast<std::uint32_t>(route_.size());

  if (route_.empty() || !live.valid() || live.format != PixelFormat::Gray8) {
    return result;
  }

  std::uint64_t best_sad = UINT64_MAX;
  std::uint32_t best_index = 0;
  std::size_t pixel_count = 0;

  // Determine the search range.
  std::size_t lo = 0;
  std::size_t hi = route_.size();
  if (cfg_.window_radius > 0 && last_best_index_.has_value()) {
    const std::int64_t prev = *last_best_index_;
    const std::int64_t r = cfg_.window_radius;
    lo = static_cast<std::size_t>(std::max<std::int64_t>(0, prev - r));
    hi = static_cast<std::size_t>(
        std::min<std::int64_t>(static_cast<std::int64_t>(route_.size()),
                               prev + r + 1));
  }

  for (std::size_t i = lo; i < hi; ++i) {
    const std::uint64_t sad =
        entry_sum_abs_diff(route_[i], live, cfg_.mean_normalise);
    if (sad < best_sad) {
      best_sad = sad;
      best_index = static_cast<std::uint32_t>(i);
      pixel_count = route_[i].payload.size();
    }
  }

  if (best_sad == UINT64_MAX || pixel_count == 0) {
    return result;
  }

  result.route_index = best_index;
  result.confidence_mille = mad_confidence_mille(best_sad, pixel_count, 255);
  result.confidence = result.confidence_mille / 1000.0f;

  const std::uint32_t denom = route_.size() > 1 ? (route_.size() - 1) : 1;
  result.progress_mille = static_cast<std::uint16_t>(
      (static_cast<std::uint64_t>(best_index) * 1000 + denom / 2) / denom);
  result.progress = result.progress_mille / 1000.0f;

  // Direction error: only when the match clears the confidence gate; below
  // that the shift estimate is meaningless and would mislead the navigator.
  result.valid = result.confidence_mille >= cfg_.min_confidence_mille;
  if (result.valid && cfg_.direction.max_shift_px > 0) {
    const DirectionResult dr =
        search_horizontal_shift(route_[best_index], live, cfg_.direction);
    if (dr.valid) {
      result.direction_shift_px = dr.shift_px;
      result.direction_error_millirad =
          shift_px_to_millirad(dr.shift_px, cfg_.microrad_per_pixel);
    }
  }
  return result;
}

RouteMatch Gray8RouteMatcher::match(const Frame& live) {
  RouteMatch m = match_stateless(live);
  if (m.valid) last_best_index_ = m.route_index;
  return m;
}

}  // namespace vh
