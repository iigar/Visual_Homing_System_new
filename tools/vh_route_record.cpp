// vh_route_record: replay a CSV manifest through the block-average
// preprocessor and write a VHRS route artifact.
//
// Usage:
//   vh_route_record --manifest <csv> --output <route.vhrs>
//                   --target-w <N> --target-h <N>
//                   [--altitude-mm <u32>]
//                   [--heading-millirad <i32>]
//
// On any flag/parse error: writes a single `record_error=<reason>` line to
// stdout and returns 1. On success: writes `record_ok=true`,
// `appended=<N>`, `rejected=<N>` to stdout and returns 0.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "vh/manifest.hpp"
#include "vh/preprocess.hpp"
#include "vh/replay_camera.hpp"
#include "vh/route_recorder.hpp"

namespace {

struct Args {
  std::filesystem::path manifest;
  std::filesystem::path output;
  std::uint32_t target_w = 0;
  std::uint32_t target_h = 0;
  std::uint32_t altitude_mm = vh::RouteEntry::kAltitudeUnknown;
  std::int32_t heading_millirad = vh::RouteEntry::kHeadingUnknown;
};

bool parse_u32(const char* s, std::uint32_t& out) {
  char* end = nullptr;
  const unsigned long v = std::strtoul(s, &end, 10);
  if (!end || *end != '\0') return false;
  if (v > 0xFFFFFFFFul) return false;
  out = static_cast<std::uint32_t>(v);
  return true;
}

bool parse_i32(const char* s, std::int32_t& out) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (!end || *end != '\0') return false;
  if (v < std::numeric_limits<std::int32_t>::min() ||
      v > std::numeric_limits<std::int32_t>::max())
    return false;
  out = static_cast<std::int32_t>(v);
  return true;
}

bool parse_args(int argc, char** argv, Args& a, std::string& err) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view k = argv[i];
    auto need = [&](const char* what) {
      if (i + 1 >= argc) {
        err = std::string("missing_value_for=") + what;
        return false;
      }
      return true;
    };
    if (k == "--manifest") {
      if (!need("--manifest")) return false;
      a.manifest = argv[++i];
    } else if (k == "--output") {
      if (!need("--output")) return false;
      a.output = argv[++i];
    } else if (k == "--target-w") {
      if (!need("--target-w") || !parse_u32(argv[++i], a.target_w)) {
        err = "bad_target_w";
        return false;
      }
    } else if (k == "--target-h") {
      if (!need("--target-h") || !parse_u32(argv[++i], a.target_h)) {
        err = "bad_target_h";
        return false;
      }
    } else if (k == "--altitude-mm") {
      if (!need("--altitude-mm") || !parse_u32(argv[++i], a.altitude_mm)) {
        err = "bad_altitude_mm";
        return false;
      }
    } else if (k == "--heading-millirad") {
      if (!need("--heading-millirad") ||
          !parse_i32(argv[++i], a.heading_millirad)) {
        err = "bad_heading_millirad";
        return false;
      }
    } else {
      err = std::string("unknown_flag=") + std::string(k);
      return false;
    }
  }
  if (a.manifest.empty()) { err = "missing_manifest"; return false; }
  if (a.output.empty()) { err = "missing_output"; return false; }
  if (a.target_w == 0 || a.target_h == 0) {
    err = "missing_target_dimensions";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Args a;
  std::string err;
  if (!parse_args(argc, argv, a, err)) {
    std::printf("record_error=%s\n", err.c_str());
    return EXIT_FAILURE;
  }

  const auto manifest = vh::load_manifest(a.manifest);
  if (manifest.error != vh::ManifestError::None) {
    std::printf("record_error=manifest_%s\n", vh::to_string(manifest.error));
    if (manifest.error_line) {
      std::printf("manifest_error_line=%zu\n", manifest.error_line);
    }
    return EXIT_FAILURE;
  }

  vh::ReplayCameraSource src(manifest.entries);
  vh::BlockAveragePreprocessor pp(a.target_w, a.target_h);
  vh::RouteSignatureRecorder rec(a.output);
  if (rec.last_error() != vh::RouteIoError::None) {
    std::printf("record_error=open_%s\n", vh::to_string(rec.last_error()));
    return EXIT_FAILURE;
  }

  vh::PoseHint pose;
  pose.altitude_band_mm = a.altitude_mm;
  pose.heading_millirad = a.heading_millirad;

  std::uint32_t pp_dropped = 0;
  while (auto f = src.next_frame()) {
    auto small = pp.process(*f);
    if (!small.valid()) {
      ++pp_dropped;
      continue;
    }
    rec.record(small, pose);
  }
  if (src.last_error() != vh::ReplayError::None) {
    std::printf("record_error=source_%s\n", vh::to_string(src.last_error()));
    std::printf("source_detail=%s\n", src.last_error_detail().c_str());
    return EXIT_FAILURE;
  }
  if (!rec.finalize()) {
    std::printf("record_error=finalize_%s\n", vh::to_string(rec.last_error()));
    return EXIT_FAILURE;
  }

  std::printf("record_ok=true\n");
  std::printf("appended=%u\n", rec.appended());
  std::printf("preprocess_dropped=%u\n", pp_dropped);
  std::printf("rejected=%u\n", rec.rejected());
  std::printf("output=%s\n", a.output.string().c_str());
  return EXIT_SUCCESS;
}
