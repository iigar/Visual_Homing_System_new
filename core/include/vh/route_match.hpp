#pragma once

// RouteMatch — output of IRouteMatcher (M5) and input to BoundedNavigator
// (M7). All hot-path arithmetic that produces this struct must be
// integer-only (see DECISIONS.md D-007); the float fields are display
// projections of integer values for downstream consumers.

#include <cstdint>

namespace vh {

struct RouteMatch {
  std::int64_t timestamp_ns = 0;

  // Best matching route entry. Meaningful only when `valid` is true.
  std::uint32_t route_index = 0;
  std::uint32_t route_total = 0;

  // Progress along the route: route_index / max(1, route_total - 1).
  // Stored as both integer (mille = 0..1000) and float for callers that
  // prefer either representation.
  std::uint16_t progress_mille = 0;
  float progress = 0.0f;

  // Match confidence (1.0 = pixel-perfect, 0.0 = worst possible MAD).
  std::uint16_t confidence_mille = 0;
  float confidence = 0.0f;

  // Coarse direction error from horizontal-shift search (M5.2).
  std::int32_t direction_shift_px = 0;
  std::int32_t direction_error_millirad = 0;

  bool valid = false;

  static constexpr std::uint16_t kMille = 1000;
};

}  // namespace vh
