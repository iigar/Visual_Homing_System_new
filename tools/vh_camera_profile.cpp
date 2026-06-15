// vh_camera_profile: validate a camera profile, emit it as JSON, or compute
// the FOV/altitude ground footprint. Stable key=value / JSON output for the
// readiness checkers and future UI/API. Footprint is a DIAGNOSTIC — it never
// authorises flight (prompt M10).
//
// Usage:
//   vh_camera_profile validate <profile.txt|--imx219>
//   vh_camera_profile json     <profile.txt|--imx219>
//   vh_camera_profile footprint <profile.txt|--imx219> <altitude_m>
//
// Exit 0 on success; 1 on validation/parse failure (with error=... on stdout).

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "vh/camera_profile.hpp"

namespace {

bool load(const std::string& src, vh::CameraProfile& p, std::string& err) {
  if (src == "--imx219") {
    p = vh::imx219_profile();
    return true;
  }
  std::ifstream f(src);
  if (!f) { err = "error=file_open_failed"; return false; }
  std::ostringstream ss;
  ss << f.rdbuf();
  return vh::parse_profile(ss.str(), p, err);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: vh_camera_profile <validate|json|footprint> "
                 "<profile|--imx219> [altitude_m]\n");
    return EXIT_FAILURE;
  }
  const std::string cmd = argv[1];
  const std::string src = argv[2];

  vh::CameraProfile p;
  std::string err;
  if (!load(src, p, err)) {
    std::printf("%s\n", err.c_str());
    return EXIT_FAILURE;
  }

  const vh::ProfileValidation v = vh::validate_profile(p);

  if (cmd == "validate") {
    std::printf("id=%s\n", p.id.c_str());
    std::printf("valid=%d\n", v.ok ? 1 : 0);
    if (!v.ok) { std::printf("%s\n", v.error.c_str()); return EXIT_FAILURE; }
    std::printf("matcher_microrad_per_pixel=%d\n",
                vh::matcher_microrad_per_pixel(p));
    return EXIT_SUCCESS;
  }

  if (cmd == "json") {
    if (!v.ok) { std::printf("%s\n", v.error.c_str()); return EXIT_FAILURE; }
    std::printf("%s\n", vh::profile_to_json(p).c_str());
    return EXIT_SUCCESS;
  }

  if (cmd == "footprint") {
    if (argc < 4) {
      std::fprintf(stderr, "footprint requires <altitude_m>\n");
      return EXIT_FAILURE;
    }
    if (!v.ok) { std::printf("%s\n", v.error.c_str()); return EXIT_FAILURE; }
    const float alt = std::strtof(argv[3], nullptr);
    const vh::GroundFootprint g = vh::compute_ground_footprint(p, alt);
    std::printf("altitude_m=%.4g\n", static_cast<double>(alt));
    std::printf("footprint_valid=%d\n", g.valid ? 1 : 0);
    if (!g.valid) { std::printf("error=bad_altitude_or_profile\n"); return EXIT_FAILURE; }
    std::printf("ground_width_m=%.6g\n", static_cast<double>(g.ground_width_m));
    std::printf("ground_height_m=%.6g\n",
                static_cast<double>(g.ground_height_m));
    std::printf("meters_per_pixel_capture=%.6g\n",
                static_cast<double>(g.meters_per_pixel_capture));
    std::printf("meters_per_pixel_target=%.6g\n",
                static_cast<double>(g.meters_per_pixel_target));
    return EXIT_SUCCESS;
  }

  std::fprintf(stderr, "unknown command: %s\n", cmd.c_str());
  return EXIT_FAILURE;
}
