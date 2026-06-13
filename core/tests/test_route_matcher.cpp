#include <cstdint>
#include <vector>

#include "vh/direction_error.hpp"
#include "vh/frame.hpp"
#include "vh/route_entry.hpp"
#include "vh/route_match.hpp"
#include "vh/route_matcher.hpp"
#include "vh_test.hpp"

namespace {

// ---------------------------------------------------------------------------
// Deterministic synthetic-frame helpers
// ---------------------------------------------------------------------------

// "Texture seed": each pixel = (seed * 17 + x*31 + y*7) mod 256. Cheap, no
// FP, produces non-trivial variation between entries even at 8x8.
std::vector<std::uint8_t> texture_pattern(unsigned w, unsigned h,
                                          unsigned seed) {
  std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h);
  for (unsigned y = 0; y < h; ++y) {
    for (unsigned x = 0; x < w; ++x) {
      const unsigned v = (seed * 17 + x * 31 + y * 7) & 0xFFu;
      px[y * w + x] = static_cast<std::uint8_t>(v);
    }
  }
  return px;
}

vh::RouteEntry make_entry(std::uint64_t id, std::int64_t ts,
                          unsigned w, unsigned h,
                          std::vector<std::uint8_t> px) {
  vh::RouteEntry e;
  e.frame_id = id;
  e.timestamp_ns = ts;
  e.width = w;
  e.height = h;
  e.format = vh::PixelFormat::Gray8;
  e.payload = std::move(px);
  return e;
}

vh::Frame make_frame(unsigned w, unsigned h, std::vector<std::uint8_t> px) {
  vh::Frame f;
  f.width = w;
  f.height = h;
  f.format = vh::PixelFormat::Gray8;
  f.payload = std::move(px);
  f.timestamp_ns = 1000;
  return f;
}

std::vector<vh::RouteEntry> make_route(unsigned w, unsigned h, unsigned n) {
  std::vector<vh::RouteEntry> r;
  for (unsigned i = 0; i < n; ++i) {
    r.push_back(make_entry(i, (i + 1) * 1000, w, h, texture_pattern(w, h, i)));
  }
  return r;
}

// Shift a buffer horizontally by `s` pixels: positive s -> content moves
// right (left columns are zero-filled). Used to simulate camera yaw error.
std::vector<std::uint8_t> shift_horizontal(const std::vector<std::uint8_t>& in,
                                           unsigned w, unsigned h,
                                           int s) {
  std::vector<std::uint8_t> out(in.size(), 0);
  for (unsigned y = 0; y < h; ++y) {
    for (int x = 0; x < static_cast<int>(w); ++x) {
      const int sx = x - s;
      if (sx >= 0 && sx < static_cast<int>(w)) {
        out[y * w + x] = in[y * w + sx];
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Kernel tests
// ---------------------------------------------------------------------------

void test_sum_abs_diff_identical_is_zero() {
  const std::uint8_t a[] = {1, 2, 3, 4, 5};
  VH_EXPECT(vh::sum_abs_diff(a, a, 5) == 0);
}

void test_sum_abs_diff_known_value() {
  const std::uint8_t a[] = {10, 20, 30};
  const std::uint8_t b[] = {12, 18, 35};  // |2| + |2| + |5| = 9
  VH_EXPECT(vh::sum_abs_diff(a, b, 3) == 9);
}

void test_mad_confidence_extremes() {
  // Perfect: sad = 0 -> 1000
  VH_EXPECT(vh::mad_confidence_mille(0, 64, 255) == 1000);
  // Worst: sad = 64 * 255 -> 0
  VH_EXPECT(vh::mad_confidence_mille(64 * 255, 64, 255) == 0);
  // Midpoint: sad = 64 * 255 / 2 -> ~500
  const std::uint16_t mid =
      vh::mad_confidence_mille(64 * 255 / 2, 64, 255);
  VH_EXPECT(mid >= 499 && mid <= 501);
}

void test_mean_gray8_uniform() {
  std::vector<std::uint8_t> u(64, 128);
  VH_EXPECT(vh::mean_gray8(u.data(), u.size()) == 128);
}

void test_mean_gray8_two_halves() {
  std::vector<std::uint8_t> v;
  v.assign(32, 100);
  v.insert(v.end(), 32, 200);
  // (32*100 + 32*200) / 64 = 150
  VH_EXPECT(vh::mean_gray8(v.data(), v.size()) == 150);
}

// ---------------------------------------------------------------------------
// Matcher tests
// ---------------------------------------------------------------------------

void test_aligned_match_picks_correct_entry() {
  auto route = make_route(8, 8, 5);
  // Build a live frame equal to entry index 3.
  auto live = make_frame(8, 8, route[3].payload);
  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 800;
  vh::Gray8RouteMatcher m(route, cfg);
  const auto r = m.match(live);
  VH_EXPECT(r.valid);
  VH_EXPECT(r.route_index == 3);
  VH_EXPECT(r.confidence_mille == 1000);
  VH_EXPECT(r.progress_mille == 750);  // 3 / (5-1) = 0.75
}

void test_brightness_offset_without_normalisation_low_confidence() {
  auto route = make_route(8, 8, 3);
  auto px = route[1].payload;
  // Uniform +30 brightness offset (clip at 255).
  for (auto& b : px) {
    const int v = static_cast<int>(b) + 30;
    b = static_cast<std::uint8_t>(v > 255 ? 255 : v);
  }
  auto live = make_frame(8, 8, px);

  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 950;  // very strict
  cfg.mean_normalise = false;
  vh::Gray8RouteMatcher m(route, cfg);
  const auto r = m.match(live);
  // Best entry should still be 1 (texture similarity wins), but confidence
  // falls below the strict threshold without normalisation.
  VH_EXPECT(r.route_index == 1);
  VH_EXPECT(!r.valid);
}

void test_brightness_offset_with_normalisation_recovers() {
  auto route = make_route(8, 8, 3);
  auto px = route[1].payload;
  for (auto& b : px) {
    const int v = static_cast<int>(b) + 30;
    b = static_cast<std::uint8_t>(v > 255 ? 255 : v);
  }
  auto live = make_frame(8, 8, px);

  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 950;
  cfg.mean_normalise = true;
  vh::Gray8RouteMatcher m(route, cfg);
  const auto r = m.match(live);
  VH_EXPECT(r.valid);
  VH_EXPECT(r.route_index == 1);
  VH_EXPECT(r.confidence_mille >= 950);
}

void test_left_shift_produces_negative_direction() {
  auto route = make_route(16, 8, 1);
  // Live frame = entry 0 shifted left by 3 pixels (content moves left,
  // i.e. live content sits to the LEFT of the reference -> negative shift).
  auto live = make_frame(16, 8,
                         shift_horizontal(route[0].payload, 16, 8, -3));

  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 200;  // shifted frame has lower confidence
  cfg.direction.max_shift_px = 5;
  cfg.microrad_per_pixel = 10'000;  // 10 mrad/px
  vh::Gray8RouteMatcher m(route, cfg);
  const auto r = m.match(live);
  VH_EXPECT(r.valid);
  VH_EXPECT(r.direction_shift_px == -3);
  VH_EXPECT(r.direction_error_millirad == -30);  // -3 * 10
}

void test_right_shift_produces_positive_direction() {
  auto route = make_route(16, 8, 1);
  auto live = make_frame(16, 8,
                         shift_horizontal(route[0].payload, 16, 8, +2));

  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 200;
  cfg.direction.max_shift_px = 5;
  cfg.microrad_per_pixel = 10'000;
  vh::Gray8RouteMatcher m(route, cfg);
  const auto r = m.match(live);
  VH_EXPECT(r.valid);
  VH_EXPECT(r.direction_shift_px == +2);
  VH_EXPECT(r.direction_error_millirad == +20);
}

void test_low_confidence_match_does_not_emit_direction() {
  auto route = make_route(8, 8, 3);
  // Live frame is hand-picked to be visually far from any route entry: a
  // chequerboard at extreme contrast that no route pattern produced by
  // `texture_pattern(seed)` for small seeds can resemble.
  std::vector<std::uint8_t> garbage(64);
  for (unsigned i = 0; i < 64; ++i) {
    garbage[i] = ((i & 1) ^ ((i >> 3) & 1)) ? 0 : 255;
  }
  auto live = make_frame(8, 8, std::move(garbage));
  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 800;
  cfg.direction.max_shift_px = 2;
  cfg.microrad_per_pixel = 10'000;
  vh::Gray8RouteMatcher m(route, cfg);
  const auto r = m.match(live);
  VH_EXPECT(!r.valid);
  // No direction error is emitted when the match is invalid.
  VH_EXPECT(r.direction_shift_px == 0);
  VH_EXPECT(r.direction_error_millirad == 0);
}

void test_dimension_mismatch_returns_invalid() {
  auto route = make_route(8, 8, 2);
  auto live = make_frame(4, 4, texture_pattern(4, 4, 0));
  vh::MatcherConfig cfg;
  vh::Gray8RouteMatcher m(route, cfg);
  const auto r = m.match(live);
  VH_EXPECT(!r.valid);
}

void test_window_restricts_search() {
  auto route = make_route(8, 8, 10);
  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 500;
  cfg.window_radius = 1;  // ±1 around last best
  vh::Gray8RouteMatcher m(route, cfg);

  // Prime the matcher onto index 5.
  auto live5 = make_frame(8, 8, route[5].payload);
  auto r5 = m.match(live5);
  VH_EXPECT(r5.valid && r5.route_index == 5);

  // Now feed a frame that perfectly matches index 9 — outside the window,
  // so the matcher must pick from {4, 5, 6} instead.
  auto live9 = make_frame(8, 8, route[9].payload);
  auto r = m.match(live9);
  VH_EXPECT(r.route_index >= 4 && r.route_index <= 6);
}

void test_empty_route_returns_invalid() {
  std::vector<vh::RouteEntry> empty;
  vh::Gray8RouteMatcher m(empty, {});
  auto live = make_frame(8, 8, texture_pattern(8, 8, 0));
  const auto r = m.match(live);
  VH_EXPECT(!r.valid);
  VH_EXPECT(r.route_total == 0);
}

// ---------------------------------------------------------------------------
// Direction kernel (standalone)
// ---------------------------------------------------------------------------

void test_search_horizontal_shift_aligned() {
  auto ref = make_entry(0, 0, 16, 4, texture_pattern(16, 4, 1));
  auto live = make_frame(16, 4, ref.payload);
  vh::DirectionConfig cfg;
  cfg.max_shift_px = 4;
  const auto r = vh::search_horizontal_shift(ref, live, cfg);
  VH_EXPECT(r.valid);
  VH_EXPECT(r.shift_px == 0);
  VH_EXPECT(r.sum_abs_diff == 0);
}

void test_search_horizontal_shift_finds_left_offset() {
  auto ref = make_entry(0, 0, 16, 4, texture_pattern(16, 4, 1));
  auto live = make_frame(16, 4,
                         shift_horizontal(ref.payload, 16, 4, -2));
  vh::DirectionConfig cfg;
  cfg.max_shift_px = 4;
  const auto r = vh::search_horizontal_shift(ref, live, cfg);
  VH_EXPECT(r.valid);
  VH_EXPECT(r.shift_px == -2);
}

void test_shift_px_to_millirad_rounds_half_up() {
  VH_EXPECT(vh::shift_px_to_millirad(3, 10'000) == 30);
  VH_EXPECT(vh::shift_px_to_millirad(-2, 10'000) == -20);
  VH_EXPECT(vh::shift_px_to_millirad(1, 1500) == 2);  // 1500 -> +2 mrad
  VH_EXPECT(vh::shift_px_to_millirad(-1, 1500) == -2);
}

}  // namespace

int main() {
  test_sum_abs_diff_identical_is_zero();
  test_sum_abs_diff_known_value();
  test_mad_confidence_extremes();
  test_mean_gray8_uniform();
  test_mean_gray8_two_halves();

  test_aligned_match_picks_correct_entry();
  test_brightness_offset_without_normalisation_low_confidence();
  test_brightness_offset_with_normalisation_recovers();
  test_left_shift_produces_negative_direction();
  test_right_shift_produces_positive_direction();
  test_low_confidence_match_does_not_emit_direction();
  test_dimension_mismatch_returns_invalid();
  test_window_restricts_search();
  test_empty_route_returns_invalid();

  test_search_horizontal_shift_aligned();
  test_search_horizontal_shift_finds_left_offset();
  test_shift_px_to_millirad_rounds_half_up();

  return vh::test::summary("test_route_matcher");
}
