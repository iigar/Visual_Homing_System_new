#pragma once

// Camera profiles (M10). A profile binds a sensor's capture/target geometry,
// field of view, and the matcher/route-quality thresholds tuned for it. The
// FOV is the source of radians-per-pixel for the matcher's direction error
// (M5 used a hand-passed microrad_per_pixel; M10 derives it). FOV + altitude
// also yields an approximate ground footprint and meters-per-pixel.
//
// SAFETY FRAMING (prompt M10): ground footprint, meters-per-pixel, and any
// visual/barometer scale mismatch are DIAGNOSTICS ONLY. They must not affect
// live commands without dry-run evidence and a separate safety decision. See
// DECISIONS D-024 and docs/CAMERA_PROFILES.md.

#include <cstdint>
#include <string>
#include <vector>

#include "vh/frame.hpp"          // PixelFormat
#include "vh/route_matcher.hpp"  // MatcherConfig
#include "vh/route_quality.hpp"  // QualityPolicy

namespace vh {

enum class SensorType : std::uint8_t {
  Visible = 0,
  Thermal = 1,
  Other = 2,
};

const char* to_string(SensorType s) noexcept;
bool sensor_type_from_string(const std::string& s, SensorType& out) noexcept;

struct CameraProfile {
  std::string id;
  SensorType sensor = SensorType::Visible;

  std::uint32_t capture_width = 0;
  std::uint32_t capture_height = 0;
  std::uint32_t target_width = 0;
  std::uint32_t target_height = 0;
  PixelFormat pixel_format = PixelFormat::Gray8;

  float horizontal_fov_rad = 0.0f;
  float vertical_fov_rad = 0.0f;

  // Matcher thresholds.
  std::uint16_t min_confidence_mille = 600;
  std::uint32_t matcher_window_radius = 0;

  // Route-quality thresholds (mirror QualityPolicy scalars).
  std::uint16_t low_texture_fraction_mille = 50;
  std::uint16_t ambiguous_nearest_fraction_mille = 100;
  std::uint64_t average_nearest_mad_min = 5;

  // Normalization hint: subtract per-frame mean before MAD (brightness
  // robustness). Feeds MatcherConfig.mean_normalise.
  bool mean_normalise = false;
};

// --- Validation -------------------------------------------------------------
struct ProfileValidation {
  bool ok = false;
  std::string error;  // key=value-ish reason when !ok
};

// Rejects empty id, zero/oversized dims (target must not exceed capture),
// and FOV outside (0, PI) or non-finite.
ProfileValidation validate_profile(const CameraProfile& p) noexcept;

// --- Text + JSON I/O --------------------------------------------------------
// Stable key=value text (consistent with route_inspect/route_quality output).
std::string format_profile(const CameraProfile& p);
bool parse_profile(const std::string& text, CameraProfile& out,
                   std::string& error);

// Flat JSON object for future UI/API consumers.
std::string profile_to_json(const CameraProfile& p);

// --- FOV-derived geometry ---------------------------------------------------
// Radians per pixel along the horizontal axis. `target` selects the target
// (matcher) frame width; otherwise the capture width. Returns 0 if the
// relevant width is 0.
float rad_per_pixel_h(const CameraProfile& p, bool target) noexcept;
float rad_per_pixel_v(const CameraProfile& p, bool target) noexcept;

// Microradians per pixel for the matcher's direction error (target frame,
// horizontal). Rounded to nearest. 0 if undefined.
std::int32_t matcher_microrad_per_pixel(const CameraProfile& p) noexcept;

struct GroundFootprint {
  bool valid = false;
  float ground_width_m = 0.0f;
  float ground_height_m = 0.0f;
  float meters_per_pixel_capture = 0.0f;  // ground_width / capture_width
  float meters_per_pixel_target = 0.0f;   // ground_width / target_width
};

// Approximate nadir ground footprint at a positive altitude/range (metres).
// Rejects non-finite or non-positive altitude and an invalid profile.
GroundFootprint compute_ground_footprint(const CameraProfile& p,
                                         float altitude_m) noexcept;

// --- Visual-scale mismatch diagnostic ---------------------------------------
// Compares the altitude a route was recorded at against the current altitude.
// DIAGNOSTIC ONLY — never gates a live command.
struct VisualScaleDiagnostic {
  bool valid = false;
  float scale_ratio = 1.0f;   // current_altitude / route_altitude
  bool mismatch = false;      // ratio outside [1/tol, tol]
};

VisualScaleDiagnostic visual_scale_mismatch(float route_altitude_m,
                                            float current_altitude_m,
                                            float tolerance = 1.25f) noexcept;

// --- Integration with matcher / quality -------------------------------------
// Build a MatcherConfig from the profile (fills microrad_per_pixel from FOV).
MatcherConfig to_matcher_config(const CameraProfile& p) noexcept;
QualityPolicy to_quality_policy(const CameraProfile& p) noexcept;

// --- Built-in profiles ------------------------------------------------------
// Initial IMX219 visible profile. NOTE: horizontal/vertical FOV here are
// nominal datasheet values — they MUST be measured for the real lens/crop
// before field use (see docs/CAMERA_PROFILES.md).
CameraProfile imx219_profile() noexcept;

// --- In-memory registry (list/get/set active) -------------------------------
class ProfileRegistry {
 public:
  // Adds/replaces a profile by id. Returns false if the profile is invalid.
  bool add(const CameraProfile& p);
  std::size_t size() const noexcept { return profiles_.size(); }
  std::vector<std::string> list() const;          // ids, insertion order
  const CameraProfile* get(const std::string& id) const noexcept;
  bool set_active(const std::string& id) noexcept;  // false if unknown id
  const CameraProfile* active() const noexcept;
  const std::string& active_id() const noexcept { return active_id_; }

 private:
  std::vector<CameraProfile> profiles_;
  std::string active_id_;
};

}  // namespace vh
