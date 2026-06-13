// vh_route_quality: run quality checks on a VHRS file and emit a stable
// key=value report consumed by `scripts/check-route-quality-log.sh`.
//
// Exit code 0 if quality_pass=true, 1 otherwise (also 1 on parse failures).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "vh/route_io.hpp"
#include "vh/route_matcher.hpp"
#include "vh/route_quality.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: vh_route_quality <route.vhrs>\n");
    return EXIT_FAILURE;
  }
  vh::VhrsReader r((std::filesystem::path(argv[1])));
  if (r.error() != vh::RouteIoError::None) {
    std::printf("quality_error=%s\n", vh::to_string(r.error()));
    return EXIT_FAILURE;
  }

  vh::MatcherConfig mcfg;
  mcfg.min_confidence_mille = 800;
  vh::PerturbationConfig pcfg;
  vh::DistinctivenessConfig dcfg;
  vh::QualityPolicy policy;

  const auto sm = vh::self_match(r.result().entries, mcfg);
  const auto pr = vh::perturbation_check(r.result().entries, mcfg, pcfg);
  const auto dr = vh::distinctiveness(r.result().entries, dcfg);
  const auto v = vh::evaluate_quality(sm, pr, dr, policy);

  std::printf("entry_count=%zu\n", r.entry_count());
  std::printf("self_match_checked=%zu\n", sm.checked);
  std::printf("self_match_valid=%zu\n", sm.valid_matches);
  std::printf("self_match_exact=%zu\n", sm.exact_index_matches);
  std::printf("self_match_progress_monotonic=%s\n",
              sm.progress_monotonic ? "true" : "false");
  std::printf("self_match_confidence_min_mille=%u\n",
              static_cast<unsigned>(sm.confidence_min_mille));

  std::printf("perturbation_checked=%zu\n", pr.checked);
  std::printf("perturbation_brightness_valid=%zu\n", pr.brightness_valid);
  std::printf("perturbation_noise_valid=%zu\n", pr.noise_valid);
  std::printf("perturbation_shift_valid=%zu\n", pr.shift_valid);
  std::printf("perturbation_malformed_rejected=%zu\n", pr.malformed_rejected);

  std::printf("distinctiveness_checked=%zu\n", dr.checked);
  std::printf("distinctiveness_low_texture=%zu\n", dr.low_texture_entries);
  std::printf("distinctiveness_exact_duplicates=%zu\n",
              dr.exact_duplicate_entries);
  std::printf("distinctiveness_ambiguous_nearest=%zu\n",
              dr.ambiguous_nearest_entries);
  std::printf("distinctiveness_adjacent_pairs=%zu\n", dr.adjacent_pairs);
  if (dr.adjacent_pairs > 0) {
    std::printf("distinctiveness_adjacent_mad_avg=%llu\n",
                static_cast<unsigned long long>(
                    dr.adjacent_mad_sum / dr.adjacent_pairs));
  }
  if (dr.nearest_neighbour_pairs > 0) {
    std::printf("distinctiveness_nearest_mad_avg=%llu\n",
                static_cast<unsigned long long>(
                    dr.nearest_neighbour_mad_sum /
                    dr.nearest_neighbour_pairs));
  }

  std::printf("quality_pass=%s\n", v.quality_pass ? "true" : "false");
  for (const auto& f : v.failures) {
    std::printf("quality_failure=%s\n", f.c_str());
  }
  return v.quality_pass ? EXIT_SUCCESS : EXIT_FAILURE;
}
