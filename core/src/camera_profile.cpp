#include "vh/camera_profile.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace vh {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string fmt_float(float v) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(v));
  return std::string(buf);
}

const char* pixel_format_str(PixelFormat f) noexcept {
  switch (f) {
    case PixelFormat::Gray8: return "gray8";
    case PixelFormat::Thermal16: return "thermal16";
  }
  return "unknown";
}

bool pixel_format_from_string(const std::string& s, PixelFormat& out) noexcept {
  if (s == "gray8") { out = PixelFormat::Gray8; return true; }
  if (s == "thermal16") { out = PixelFormat::Thermal16; return true; }
  return false;
}

}  // namespace

const char* to_string(SensorType s) noexcept {
  switch (s) {
    case SensorType::Visible: return "visible";
    case SensorType::Thermal: return "thermal";
    case SensorType::Other: return "other";
  }
  return "other";
}

bool sensor_type_from_string(const std::string& s, SensorType& out) noexcept {
  if (s == "visible") { out = SensorType::Visible; return true; }
  if (s == "thermal") { out = SensorType::Thermal; return true; }
  if (s == "other") { out = SensorType::Other; return true; }
  return false;
}

ProfileValidation validate_profile(const CameraProfile& p) noexcept {
  ProfileValidation v;
  if (p.id.empty()) { v.error = "error=empty_id"; return v; }
  if (p.capture_width == 0 || p.capture_height == 0) {
    v.error = "error=zero_capture_dims"; return v;
  }
  if (p.target_width == 0 || p.target_height == 0) {
    v.error = "error=zero_target_dims"; return v;
  }
  if (p.target_width > p.capture_width || p.target_height > p.capture_height) {
    v.error = "error=target_exceeds_capture"; return v;
  }
  for (float fov : {p.horizontal_fov_rad, p.vertical_fov_rad}) {
    if (!std::isfinite(fov) || fov <= 0.0f || fov >= kPi) {
      v.error = "error=fov_out_of_range"; return v;
    }
  }
  v.ok = true;
  return v;
}

std::string format_profile(const CameraProfile& p) {
  std::ostringstream o;
  o << "id=" << p.id << "\n"
    << "sensor=" << to_string(p.sensor) << "\n"
    << "capture_width=" << p.capture_width << "\n"
    << "capture_height=" << p.capture_height << "\n"
    << "target_width=" << p.target_width << "\n"
    << "target_height=" << p.target_height << "\n"
    << "pixel_format=" << pixel_format_str(p.pixel_format) << "\n"
    << "horizontal_fov_rad=" << fmt_float(p.horizontal_fov_rad) << "\n"
    << "vertical_fov_rad=" << fmt_float(p.vertical_fov_rad) << "\n"
    << "min_confidence_mille=" << p.min_confidence_mille << "\n"
    << "matcher_window_radius=" << p.matcher_window_radius << "\n"
    << "low_texture_fraction_mille=" << p.low_texture_fraction_mille << "\n"
    << "ambiguous_nearest_fraction_mille=" << p.ambiguous_nearest_fraction_mille
    << "\n"
    << "average_nearest_mad_min=" << p.average_nearest_mad_min << "\n"
    << "mean_normalise=" << (p.mean_normalise ? 1 : 0) << "\n";
  return o.str();
}

bool parse_profile(const std::string& text, CameraProfile& out,
                   std::string& error) {
  CameraProfile p;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    try {
      if (key == "id") p.id = val;
      else if (key == "sensor") {
        if (!sensor_type_from_string(val, p.sensor)) {
          error = "error=bad_sensor"; return false;
        }
      } else if (key == "capture_width") p.capture_width = std::stoul(val);
      else if (key == "capture_height") p.capture_height = std::stoul(val);
      else if (key == "target_width") p.target_width = std::stoul(val);
      else if (key == "target_height") p.target_height = std::stoul(val);
      else if (key == "pixel_format") {
        if (!pixel_format_from_string(val, p.pixel_format)) {
          error = "error=bad_pixel_format"; return false;
        }
      } else if (key == "horizontal_fov_rad") p.horizontal_fov_rad = std::stof(val);
      else if (key == "vertical_fov_rad") p.vertical_fov_rad = std::stof(val);
      else if (key == "min_confidence_mille")
        p.min_confidence_mille = static_cast<std::uint16_t>(std::stoul(val));
      else if (key == "matcher_window_radius")
        p.matcher_window_radius = std::stoul(val);
      else if (key == "low_texture_fraction_mille")
        p.low_texture_fraction_mille = static_cast<std::uint16_t>(std::stoul(val));
      else if (key == "ambiguous_nearest_fraction_mille")
        p.ambiguous_nearest_fraction_mille =
            static_cast<std::uint16_t>(std::stoul(val));
      else if (key == "average_nearest_mad_min")
        p.average_nearest_mad_min = std::stoull(val);
      else if (key == "mean_normalise")
        p.mean_normalise = (val == "1" || val == "true");
    } catch (...) {
      error = "error=bad_value key=" + key;
      return false;
    }
  }
  out = p;
  return true;
}

std::string profile_to_json(const CameraProfile& p) {
  std::ostringstream o;
  o << "{"
    << "\"id\":\"" << p.id << "\","
    << "\"sensor\":\"" << to_string(p.sensor) << "\","
    << "\"capture_width\":" << p.capture_width << ","
    << "\"capture_height\":" << p.capture_height << ","
    << "\"target_width\":" << p.target_width << ","
    << "\"target_height\":" << p.target_height << ","
    << "\"pixel_format\":\"" << pixel_format_str(p.pixel_format) << "\","
    << "\"horizontal_fov_rad\":" << fmt_float(p.horizontal_fov_rad) << ","
    << "\"vertical_fov_rad\":" << fmt_float(p.vertical_fov_rad) << ","
    << "\"min_confidence_mille\":" << p.min_confidence_mille << ","
    << "\"matcher_window_radius\":" << p.matcher_window_radius << ","
    << "\"low_texture_fraction_mille\":" << p.low_texture_fraction_mille << ","
    << "\"ambiguous_nearest_fraction_mille\":"
    << p.ambiguous_nearest_fraction_mille << ","
    << "\"average_nearest_mad_min\":" << p.average_nearest_mad_min << ","
    << "\"mean_normalise\":" << (p.mean_normalise ? "true" : "false")
    << "}";
  return o.str();
}

float rad_per_pixel_h(const CameraProfile& p, bool target) noexcept {
  const std::uint32_t w = target ? p.target_width : p.capture_width;
  if (w == 0) return 0.0f;
  return p.horizontal_fov_rad / static_cast<float>(w);
}

float rad_per_pixel_v(const CameraProfile& p, bool target) noexcept {
  const std::uint32_t h = target ? p.target_height : p.capture_height;
  if (h == 0) return 0.0f;
  return p.vertical_fov_rad / static_cast<float>(h);
}

std::int32_t matcher_microrad_per_pixel(const CameraProfile& p) noexcept {
  const float rpp = rad_per_pixel_h(p, /*target=*/true);
  if (!std::isfinite(rpp) || rpp <= 0.0f) return 0;
  return static_cast<std::int32_t>(std::lround(rpp * 1'000'000.0f));
}

GroundFootprint compute_ground_footprint(const CameraProfile& p,
                                         float altitude_m) noexcept {
  GroundFootprint g;
  if (!std::isfinite(altitude_m) || altitude_m <= 0.0f) return g;
  if (!validate_profile(p).ok) return g;

  g.ground_width_m =
      2.0f * altitude_m * std::tan(p.horizontal_fov_rad * 0.5f);
  g.ground_height_m =
      2.0f * altitude_m * std::tan(p.vertical_fov_rad * 0.5f);
  if (p.capture_width > 0) {
    g.meters_per_pixel_capture =
        g.ground_width_m / static_cast<float>(p.capture_width);
  }
  if (p.target_width > 0) {
    g.meters_per_pixel_target =
        g.ground_width_m / static_cast<float>(p.target_width);
  }
  g.valid = std::isfinite(g.ground_width_m) && std::isfinite(g.ground_height_m);
  return g;
}

VisualScaleDiagnostic visual_scale_mismatch(float route_altitude_m,
                                            float current_altitude_m,
                                            float tolerance) noexcept {
  VisualScaleDiagnostic d;
  if (!std::isfinite(route_altitude_m) || route_altitude_m <= 0.0f ||
      !std::isfinite(current_altitude_m) || current_altitude_m <= 0.0f ||
      !std::isfinite(tolerance) || tolerance <= 1.0f) {
    return d;
  }
  d.valid = true;
  d.scale_ratio = current_altitude_m / route_altitude_m;
  d.mismatch = d.scale_ratio > tolerance || d.scale_ratio < (1.0f / tolerance);
  return d;
}

MatcherConfig to_matcher_config(const CameraProfile& p) noexcept {
  MatcherConfig c;
  c.min_confidence_mille = p.min_confidence_mille;
  c.window_radius = p.matcher_window_radius;
  c.mean_normalise = p.mean_normalise;
  c.microrad_per_pixel = matcher_microrad_per_pixel(p);
  return c;
}

QualityPolicy to_quality_policy(const CameraProfile& p) noexcept {
  QualityPolicy q;
  q.low_texture_fraction_mille = p.low_texture_fraction_mille;
  q.ambiguous_nearest_fraction_mille = p.ambiguous_nearest_fraction_mille;
  q.average_nearest_mad_min = p.average_nearest_mad_min;
  return q;
}

CameraProfile imx219_profile() noexcept {
  CameraProfile p;
  p.id = "imx219";
  p.sensor = SensorType::Visible;
  p.capture_width = 1640;
  p.capture_height = 1232;
  p.target_width = 64;
  p.target_height = 48;
  p.pixel_format = PixelFormat::Gray8;
  // Nominal datasheet FOV — MUST be measured for the real lens/crop.
  p.horizontal_fov_rad = 1.193f;  // ~62.2 deg
  p.vertical_fov_rad = 0.852f;    // ~48.8 deg
  p.min_confidence_mille = 600;
  p.mean_normalise = true;
  return p;
}

bool ProfileRegistry::add(const CameraProfile& p) {
  if (!validate_profile(p).ok) return false;
  for (auto& existing : profiles_) {
    if (existing.id == p.id) {
      existing = p;
      return true;
    }
  }
  profiles_.push_back(p);
  if (active_id_.empty()) active_id_ = p.id;  // first added becomes active
  return true;
}

std::vector<std::string> ProfileRegistry::list() const {
  std::vector<std::string> ids;
  ids.reserve(profiles_.size());
  for (const auto& p : profiles_) ids.push_back(p.id);
  return ids;
}

const CameraProfile* ProfileRegistry::get(const std::string& id) const noexcept {
  for (const auto& p : profiles_) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

bool ProfileRegistry::set_active(const std::string& id) noexcept {
  if (get(id) == nullptr) return false;
  active_id_ = id;
  return true;
}

const CameraProfile* ProfileRegistry::active() const noexcept {
  if (active_id_.empty()) return nullptr;
  return get(active_id_);
}

}  // namespace vh
