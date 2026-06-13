#include <cstdint>
#include <vector>

#include "vh/frame.hpp"
#include "vh/route_entry.hpp"
#include "vh/route_matcher.hpp"
#include "vh/route_quality.hpp"
#include "vh_test.hpp"

namespace {

std::vector<std::uint8_t> texture(unsigned w, unsigned h, unsigned seed) {
  std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h);
  for (unsigned y = 0; y < h; ++y) {
    for (unsigned x = 0; x < w; ++x) {
      const unsigned v = (seed * 53 + x * 31 + y * 17) & 0xFFu;
      px[y * w + x] = static_cast<std::uint8_t>(v);
    }
  }
  return px;
}

vh::RouteEntry make_entry(std::uint64_t id, unsigned w, unsigned h,
                          std::vector<std::uint8_t> px) {
  vh::RouteEntry e;
  e.frame_id = id;
  e.timestamp_ns = static_cast<std::int64_t>(id + 1) * 1000;
  e.width = w;
  e.height = h;
  e.format = vh::PixelFormat::Gray8;
  e.payload = std::move(px);
  return e;
}

std::vector<vh::RouteEntry> diverse_route(unsigned n) {
  std::vector<vh::RouteEntry> r;
  for (unsigned i = 0; i < n; ++i) {
    r.push_back(make_entry(i, 8, 8, texture(8, 8, i + 1)));
  }
  return r;
}

vh::MatcherConfig strict_cfg() {
  vh::MatcherConfig c;
  c.min_confidence_mille = 800;
  c.mean_normalise = false;
  return c;
}

// ---------------------------------------------------------------------------
// Self-match
// ---------------------------------------------------------------------------

void test_self_match_clean_route_perfect() {
  auto route = diverse_route(6);
  const auto r = vh::self_match(route, strict_cfg());
  VH_EXPECT(r.checked == 6);
  VH_EXPECT(r.valid_matches == 6);
  VH_EXPECT(r.exact_index_matches == 6);
  VH_EXPECT(r.confidence_min_mille == 1000);
  VH_EXPECT(r.progress_monotonic);
}

void test_self_match_with_duplicates_loses_exactness() {
  auto route = diverse_route(4);
  // Make entry 3 identical to entry 1 -> matcher will prefer the lower
  // index, so entry 3 stops matching its own index.
  route[3].payload = route[1].payload;
  const auto r = vh::self_match(route, strict_cfg());
  VH_EXPECT(r.checked == 4);
  VH_EXPECT(r.exact_index_matches < 4);
}

void test_self_match_empty_route() {
  std::vector<vh::RouteEntry> empty;
  const auto r = vh::self_match(empty, strict_cfg());
  VH_EXPECT(r.checked == 0);
  VH_EXPECT(r.valid_matches == 0);
}

// ---------------------------------------------------------------------------
// Perturbation checks
// ---------------------------------------------------------------------------

void test_perturbation_brightness_with_normalisation_passes() {
  auto route = diverse_route(5);
  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 700;
  cfg.mean_normalise = true;
  vh::PerturbationConfig pcfg;
  pcfg.brightness_delta = 25;
  pcfg.noise_amplitude = 2;
  pcfg.horizontal_shift_px = 0;
  const auto r = vh::perturbation_check(route, cfg, pcfg);
  VH_EXPECT(r.checked == 5);
  VH_EXPECT(r.brightness_valid == 5);
}

void test_perturbation_malformed_always_rejected() {
  auto route = diverse_route(5);
  const auto r = vh::perturbation_check(route, strict_cfg(), {});
  VH_EXPECT(r.malformed_rejected == 5);
}

void test_perturbation_noise_small_amplitude_keeps_match() {
  auto route = diverse_route(5);
  vh::PerturbationConfig pcfg;
  pcfg.brightness_delta = 0;
  pcfg.noise_amplitude = 3;  // very small
  pcfg.horizontal_shift_px = 0;
  vh::MatcherConfig cfg;
  cfg.min_confidence_mille = 700;
  const auto r = vh::perturbation_check(route, cfg, pcfg);
  VH_EXPECT(r.noise_index_kept == 5);
}

// ---------------------------------------------------------------------------
// Distinctiveness
// ---------------------------------------------------------------------------

void test_distinctiveness_uniform_low_texture() {
  std::vector<vh::RouteEntry> route;
  for (unsigned i = 0; i < 4; ++i) {
    // Uniform-ish payload: tiny variations only.
    std::vector<std::uint8_t> px(64, static_cast<std::uint8_t>(100 + i));
    route.push_back(make_entry(i, 8, 8, std::move(px)));
  }
  vh::DistinctivenessConfig cfg;
  const auto r = vh::distinctiveness(route, cfg);
  VH_EXPECT(r.low_texture_entries == r.checked);
  VH_EXPECT(r.low_texture_indices.size() > 0);
}

void test_distinctiveness_detects_exact_duplicates() {
  auto route = diverse_route(4);
  route[2].payload = route[0].payload;
  const auto r = vh::distinctiveness(route, {});
  VH_EXPECT(r.exact_duplicate_entries >= 2);
}

void test_distinctiveness_diverse_route_passes_thresholds() {
  auto route = diverse_route(8);
  vh::DistinctivenessConfig cfg;
  cfg.low_texture_range = 20;
  const auto r = vh::distinctiveness(route, cfg);
  VH_EXPECT(r.low_texture_entries == 0);
  VH_EXPECT(r.exact_duplicate_entries == 0);
}

void test_distinctiveness_edge_trim_excludes_boundary_entries() {
  auto route = diverse_route(6);
  // Make boundary entries (0 and 5) low-texture.
  route[0].payload.assign(64, 100);
  route[5].payload.assign(64, 100);
  vh::DistinctivenessConfig cfg;
  cfg.edge_trim = 1;
  const auto r = vh::distinctiveness(route, cfg);
  VH_EXPECT(r.checked == 4);
  VH_EXPECT(r.low_texture_entries == 0);
}

// ---------------------------------------------------------------------------
// Quality verdict
// ---------------------------------------------------------------------------

void test_clean_route_passes_quality() {
  auto route = diverse_route(8);
  const auto sm = vh::self_match(route, strict_cfg());
  const auto pr = vh::perturbation_check(route, strict_cfg(), {});
  const auto dr = vh::distinctiveness(route, {});
  vh::QualityPolicy policy;
  // 8x8 synthetic patterns are naturally a bit ambiguous; the real-world
  // 64x48 capture has far more entropy. Use loosened thresholds here that
  // still keep the pass/fail semantics meaningful.
  policy.average_nearest_mad_min = 1;
  policy.ambiguous_nearest_fraction_mille = 400;
  const auto v = vh::evaluate_quality(sm, pr, dr, policy);
  VH_EXPECT(v.quality_pass);
  VH_EXPECT(v.failures.empty());
}

void test_duplicate_route_fails_quality() {
  auto route = diverse_route(6);
  route[3].payload = route[1].payload;
  const auto sm = vh::self_match(route, strict_cfg());
  const auto pr = vh::perturbation_check(route, strict_cfg(), {});
  const auto dr = vh::distinctiveness(route, {});
  const auto v = vh::evaluate_quality(sm, pr, dr, vh::QualityPolicy{});
  VH_EXPECT(!v.quality_pass);
  VH_EXPECT(!v.failures.empty());
}

void test_low_texture_route_fails_quality() {
  std::vector<vh::RouteEntry> route;
  for (unsigned i = 0; i < 6; ++i) {
    std::vector<std::uint8_t> px(64, static_cast<std::uint8_t>(100 + i));
    route.push_back(make_entry(i, 8, 8, std::move(px)));
  }
  const auto sm = vh::self_match(route, strict_cfg());
  const auto pr = vh::perturbation_check(route, strict_cfg(), {});
  const auto dr = vh::distinctiveness(route, {});
  const auto v = vh::evaluate_quality(sm, pr, dr, vh::QualityPolicy{});
  VH_EXPECT(!v.quality_pass);
}

}  // namespace

int main() {
  test_self_match_clean_route_perfect();
  test_self_match_with_duplicates_loses_exactness();
  test_self_match_empty_route();

  test_perturbation_brightness_with_normalisation_passes();
  test_perturbation_malformed_always_rejected();
  test_perturbation_noise_small_amplitude_keeps_match();

  test_distinctiveness_uniform_low_texture();
  test_distinctiveness_detects_exact_duplicates();
  test_distinctiveness_diverse_route_passes_thresholds();
  test_distinctiveness_edge_trim_excludes_boundary_entries();

  test_clean_route_passes_quality();
  test_duplicate_route_fails_quality();
  test_low_texture_route_fails_quality();

  return vh::test::summary("test_route_quality");
}
