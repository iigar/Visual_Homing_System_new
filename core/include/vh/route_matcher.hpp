#pragma once

// Gray8RouteMatcher — first deterministic baseline matcher (see promp M5).
// Normalised mean-absolute-byte-difference (MAD) between the live frame and
// every entry of a recorded VHRS route. The best entry whose MAD-derived
// confidence clears `min_confidence_mille` wins.
//
// MAD is the chosen baseline because it is cheap, deterministic, integer-
// only, and easy to reason about. It is not the final perception algorithm
// (promp explicitly warns about monotonic outdoor terrain failure modes);
// fallback matchers will plug in behind this same IRouteMatcher contract.

#include <cstdint>
#include <optional>
#include <vector>

#include "vh/direction_error.hpp"
#include "vh/frame.hpp"
#include "vh/interfaces.hpp"
#include "vh/route_entry.hpp"
#include "vh/route_match.hpp"

namespace vh {

struct MatcherConfig {
  // Confidence threshold in mille (0..1000). A match below this is reported
  // with valid=false. 600 = "agree on at least ~60% of pixel range".
  std::uint16_t min_confidence_mille = 600;

  // Optional sliding window around the previous best index. 0 disables the
  // window (whole-route search every call). Window radius is in entries:
  // search range = [prev - radius, prev + radius].
  std::uint32_t window_radius = 0;

  // Optional mean-normalisation before MAD: subtract the mean Gray8 value of
  // each frame so a uniform brightness offset does not destroy confidence.
  bool mean_normalise = false;

  // Direction error (M5.2) — horizontal shift search around the best entry.
  DirectionConfig direction;

  // Camera profile rad-per-pixel arrives in M10; until then the caller
  // supplies microrad-per-pixel directly. 0 disables the millirad output
  // (only pixel shift is filled).
  std::int32_t microrad_per_pixel = 0;
};

class Gray8RouteMatcher : public IRouteMatcher {
 public:
  Gray8RouteMatcher(std::vector<RouteEntry> route, MatcherConfig config);

  // Match a live frame against the route. Live frame dimensions/format must
  // match the route entries; otherwise the match is valid=false.
  RouteMatch match(const Frame& live_frame) override;

  std::size_t route_size() const noexcept { return route_.size(); }
  const MatcherConfig& config() const noexcept { return cfg_; }

  // Stateless variant: used by self-match and quality diagnostics so the
  // matcher does not "remember" the previous index between calls.
  RouteMatch match_stateless(const Frame& live_frame) const;

 private:
  std::vector<RouteEntry> route_;
  MatcherConfig cfg_;
  std::optional<std::uint32_t> last_best_index_;
};

// ---------------------------------------------------------------------------
// Low-level kernels exposed for tests and for the quality module (M6).
// ---------------------------------------------------------------------------

// Returns the sum of |a[i] - b[i]| over a..a+n. Requires both buffers of
// length n. Integer-only.
std::uint64_t sum_abs_diff(const std::uint8_t* a, const std::uint8_t* b,
                           std::size_t n) noexcept;

// Mean of the Gray8 buffer, rounded to nearest integer (half away from zero
// for non-negative values).
std::uint8_t mean_gray8(const std::uint8_t* p, std::size_t n) noexcept;

// Confidence in mille given an absolute-difference sum, pixel count, and
// maximum per-pixel value (255 for Gray8). 1000 = perfect, 0 = worst.
std::uint16_t mad_confidence_mille(std::uint64_t sum_abs_diff_value,
                                   std::size_t pixel_count,
                                   std::uint8_t max_value) noexcept;

}  // namespace vh
