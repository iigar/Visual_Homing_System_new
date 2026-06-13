#include "vh/direction_error.hpp"

#include <algorithm>

namespace vh {

namespace {

// MAD of live shifted by `shift` pixels horizontally against reference.
// Only the overlapping columns are counted. Returns UINT64_MAX on invalid
// geometry.
std::uint64_t sad_at_shift(const RouteEntry& ref, const Frame& live,
                           std::int32_t shift,
                           std::uint64_t& overlap_pixels) noexcept {
  const std::uint32_t w = ref.width;
  const std::uint32_t h = ref.height;
  if (w == 0 || h == 0) return UINT64_MAX;

  // After shifting live by `shift`, only columns where both ref and live
  // exist overlap. For shift > 0: ref columns [shift, w); live columns
  // [0, w - shift). For shift < 0: ref [0, w + shift); live [-shift, w).
  std::int32_t cols = static_cast<std::int32_t>(w) - std::abs(shift);
  if (cols <= 0) return UINT64_MAX;

  std::int32_t ref_x0 = shift > 0 ? shift : 0;
  std::int32_t live_x0 = shift > 0 ? 0 : -shift;

  std::uint64_t s = 0;
  const std::uint8_t* rp = ref.payload.data();
  const std::uint8_t* lp = live.payload.data();
  for (std::uint32_t y = 0; y < h; ++y) {
    const std::uint8_t* rrow = rp + static_cast<std::size_t>(y) * w;
    const std::uint8_t* lrow = lp + static_cast<std::size_t>(y) * w;
    for (std::int32_t x = 0; x < cols; ++x) {
      const int a = rrow[ref_x0 + x];
      const int b = lrow[live_x0 + x];
      const int d = a - b;
      s += static_cast<std::uint64_t>(d >= 0 ? d : -d);
    }
  }
  overlap_pixels = static_cast<std::uint64_t>(cols) * h;
  return s;
}

}  // namespace

DirectionResult search_horizontal_shift(const RouteEntry& ref,
                                        const Frame& live,
                                        const DirectionConfig& cfg) noexcept {
  DirectionResult out;
  if (!ref.valid() || !live.valid()) return out;
  if (ref.format != PixelFormat::Gray8 || live.format != PixelFormat::Gray8) {
    return out;
  }
  if (ref.width != live.width || ref.height != live.height) return out;
  if (cfg.max_shift_px < 0) return out;
  if (static_cast<std::int32_t>(ref.width) <= cfg.max_shift_px) {
    // No useful overlap is possible — refuse.
    return out;
  }

  std::uint64_t best_sad = UINT64_MAX;
  std::int32_t best_shift = 0;
  std::uint64_t best_overlap = 0;

  for (std::int32_t s = -cfg.max_shift_px; s <= cfg.max_shift_px; ++s) {
    std::uint64_t overlap = 0;
    const std::uint64_t sad = sad_at_shift(ref, live, s, overlap);
    if (sad == UINT64_MAX || overlap == 0) continue;
    // Normalise by overlap so shifts that touch fewer pixels are not
    // mechanically rewarded.
    // Integer normalisation: compare sad * 1000 / overlap (lower is better).
    const std::uint64_t scaled = (sad * 1000) / overlap;
    const std::uint64_t best_scaled =
        best_sad == UINT64_MAX ? UINT64_MAX
                               : (best_sad * 1000) / best_overlap;
    if (scaled < best_scaled) {
      best_sad = sad;
      best_shift = s;
      best_overlap = overlap;
    }
  }

  if (best_sad == UINT64_MAX) return out;
  // Convention: positive shift_px means the live frame content was shifted
  // RIGHT relative to the reference (e.g. produced by horizontally
  // translating live pixels rightwards before matching). This matches how
  // the recorder helper `shift_horizontal(in, +k)` moves content right.
  // Internally `sad_at_shift` parameterises by the *reference-side*
  // offset, which is the opposite sign of the live-side displacement, so
  // we negate before reporting.
  out.shift_px = -best_shift;
  out.sum_abs_diff = best_sad;
  out.overlap_pixels = best_overlap;
  out.valid = true;
  return out;
}

std::int32_t shift_px_to_millirad(std::int32_t shift_px,
                                  std::int32_t microrad_per_pixel) noexcept {
  // shift_px * microrad_per_pixel = total microradians; convert to
  // milliradians with half-up rounding (signed).
  const std::int64_t total =
      static_cast<std::int64_t>(shift_px) * microrad_per_pixel;
  const std::int64_t half = total >= 0 ? 500 : -500;
  return static_cast<std::int32_t>((total + half) / 1000);
}

}  // namespace vh
