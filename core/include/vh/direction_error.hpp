#pragma once

// Coarse direction-error estimation: shift the live frame left/right by up
// to `max_shift_px` pixels and pick the offset that minimises MAD against
// the reference entry. Vertical shift is intentionally not searched here —
// our first scope is yaw correction only, not pitch.
//
// Conversion to milliradians needs a camera-profile `rad_per_pixel`
// (delivered by M10). Until then the caller can pass a microradian-precise
// integer parameter directly; we keep this purely arithmetic so the kernel
// is testable on its own.

#include <cstdint>

#include "vh/frame.hpp"
#include "vh/route_entry.hpp"

namespace vh {

struct DirectionResult {
  // Best horizontal shift in pixels. Sign convention: a positive shift_px
  // means the live frame's content has been displaced to the RIGHT compared
  // to the recorded reference (i.e. the live image needs to be moved LEFT
  // to align with reference). This is the same sign as the parameter `s`
  // in the test helper `shift_horizontal(in, +s)`.
  std::int32_t shift_px = 0;

  // sum_abs_diff at the best shift (over the overlapping region only).
  std::uint64_t sum_abs_diff = 0;

  // Number of pixels actually compared (overlap region of reference and
  // live after shifting). Always > 0 when `valid`.
  std::uint64_t overlap_pixels = 0;

  bool valid = false;
};

struct DirectionConfig {
  std::int32_t max_shift_px = 4;
};

// Search horizontal shifts in [-max_shift_px, +max_shift_px]. Both frames
// must be Gray8 and same dimensions. The non-overlapping border is excluded
// from MAD so a small shift does not unfairly penalise edge content.
DirectionResult search_horizontal_shift(const RouteEntry& reference,
                                        const Frame& live,
                                        const DirectionConfig& cfg) noexcept;

// Convert pixel shift to milliradians given the camera profile's
// rad-per-pixel (passed as microrad-per-pixel for integer-exact math).
// Returns the signed milliradian value.
std::int32_t shift_px_to_millirad(std::int32_t shift_px,
                                  std::int32_t microrad_per_pixel) noexcept;

}  // namespace vh
