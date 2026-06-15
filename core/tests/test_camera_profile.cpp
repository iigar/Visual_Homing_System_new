#include <cmath>
#include <limits>
#include <string>

#include "vh/camera_profile.hpp"
#include "vh/route_matcher.hpp"
#include "vh_test.hpp"

namespace {

vh::CameraProfile valid_profile() {
  vh::CameraProfile p;
  p.id = "test";
  p.sensor = vh::SensorType::Visible;
  p.capture_width = 640;
  p.capture_height = 480;
  p.target_width = 64;
  p.target_height = 48;
  p.pixel_format = vh::PixelFormat::Gray8;
  p.horizontal_fov_rad = 1.2f;
  p.vertical_fov_rad = 0.9f;
  return p;
}

bool approx(float a, float b, float eps = 1e-4f) {
  return std::fabs(a - b) < eps;
}

void test_validation() {
  VH_EXPECT(vh::validate_profile(valid_profile()).ok);

  auto p = valid_profile();
  p.id.clear();
  VH_EXPECT(!vh::validate_profile(p).ok);

  p = valid_profile();
  p.target_width = 1000;  // exceeds capture
  VH_EXPECT(!vh::validate_profile(p).ok);

  p = valid_profile();
  p.capture_height = 0;
  VH_EXPECT(!vh::validate_profile(p).ok);

  p = valid_profile();
  p.horizontal_fov_rad = 0.0f;  // out of (0, PI)
  VH_EXPECT(!vh::validate_profile(p).ok);

  p = valid_profile();
  p.vertical_fov_rad = std::numeric_limits<float>::infinity();
  VH_EXPECT(!vh::validate_profile(p).ok);
}

void test_text_roundtrip() {
  auto p = valid_profile();
  p.mean_normalise = true;
  p.min_confidence_mille = 700;
  const std::string text = vh::format_profile(p);
  vh::CameraProfile q;
  std::string err;
  VH_EXPECT(vh::parse_profile(text, q, err));
  VH_EXPECT(q.id == "test");
  VH_EXPECT(q.sensor == vh::SensorType::Visible);
  VH_EXPECT(q.capture_width == 640);
  VH_EXPECT(q.target_height == 48);
  VH_EXPECT(q.pixel_format == vh::PixelFormat::Gray8);
  VH_EXPECT(approx(q.horizontal_fov_rad, 1.2f));
  VH_EXPECT(q.min_confidence_mille == 700);
  VH_EXPECT(q.mean_normalise);
}

void test_parse_rejects_bad_sensor() {
  vh::CameraProfile q;
  std::string err;
  VH_EXPECT(!vh::parse_profile("id=x\nsensor=banana\n", q, err));
  VH_EXPECT(err == "error=bad_sensor");
}

void test_json_output() {
  auto p = valid_profile();
  const std::string j = vh::profile_to_json(p);
  VH_EXPECT(j.find("\"id\":\"test\"") != std::string::npos);
  VH_EXPECT(j.find("\"sensor\":\"visible\"") != std::string::npos);
  VH_EXPECT(j.find("\"target_width\":64") != std::string::npos);
  VH_EXPECT(j.front() == '{' && j.back() == '}');
}

void test_rad_per_pixel() {
  auto p = valid_profile();  // h_fov 1.2 rad, target_width 64, capture_width 640
  VH_EXPECT(approx(vh::rad_per_pixel_h(p, true), 1.2f / 64.0f));
  VH_EXPECT(approx(vh::rad_per_pixel_h(p, false), 1.2f / 640.0f));
  // microrad/pixel for matcher = target rpp * 1e6, rounded.
  const std::int32_t expected =
      static_cast<std::int32_t>(std::lround((1.2f / 64.0f) * 1e6f));
  VH_EXPECT(vh::matcher_microrad_per_pixel(p) == expected);
}

void test_ground_footprint() {
  auto p = valid_profile();
  auto g = vh::compute_ground_footprint(p, 10.0f);
  VH_EXPECT(g.valid);
  // ground_width = 2 * 10 * tan(0.6) ; tan(0.6) ~ 0.6841
  VH_EXPECT(approx(g.ground_width_m, 2.0f * 10.0f * std::tan(0.6f), 1e-2f));
  VH_EXPECT(g.meters_per_pixel_target > g.meters_per_pixel_capture);  // fewer px
  // mpp_target = ground_width / 64
  VH_EXPECT(approx(g.meters_per_pixel_target, g.ground_width_m / 64.0f, 1e-3f));
}

void test_ground_footprint_rejects_bad_altitude() {
  auto p = valid_profile();
  VH_EXPECT(!vh::compute_ground_footprint(p, 0.0f).valid);
  VH_EXPECT(!vh::compute_ground_footprint(p, -5.0f).valid);
  VH_EXPECT(
      !vh::compute_ground_footprint(
            p, std::numeric_limits<float>::quiet_NaN()).valid);
  VH_EXPECT(
      !vh::compute_ground_footprint(
            p, std::numeric_limits<float>::infinity()).valid);
}

void test_visual_scale_mismatch() {
  // Same altitude -> ratio 1, no mismatch.
  auto d = vh::visual_scale_mismatch(10.0f, 10.0f);
  VH_EXPECT(d.valid && !d.mismatch && approx(d.scale_ratio, 1.0f));
  // 2x higher -> mismatch (default tol 1.25).
  auto d2 = vh::visual_scale_mismatch(10.0f, 20.0f);
  VH_EXPECT(d2.valid && d2.mismatch && approx(d2.scale_ratio, 2.0f));
  // Within tolerance.
  auto d3 = vh::visual_scale_mismatch(10.0f, 11.0f);
  VH_EXPECT(d3.valid && !d3.mismatch);
  // Invalid input.
  VH_EXPECT(!vh::visual_scale_mismatch(0.0f, 10.0f).valid);
  VH_EXPECT(!vh::visual_scale_mismatch(10.0f, -1.0f).valid);
}

void test_to_matcher_config() {
  auto p = valid_profile();
  p.min_confidence_mille = 650;
  p.mean_normalise = true;
  p.matcher_window_radius = 3;
  vh::MatcherConfig c = vh::to_matcher_config(p);
  VH_EXPECT(c.min_confidence_mille == 650);
  VH_EXPECT(c.mean_normalise);
  VH_EXPECT(c.window_radius == 3);
  VH_EXPECT(c.microrad_per_pixel == vh::matcher_microrad_per_pixel(p));
  VH_EXPECT(c.microrad_per_pixel > 0);
}

void test_imx219_builtin() {
  auto p = vh::imx219_profile();
  VH_EXPECT(vh::validate_profile(p).ok);
  VH_EXPECT(p.id == "imx219");
  VH_EXPECT(p.sensor == vh::SensorType::Visible);
}

void test_registry() {
  vh::ProfileRegistry reg;
  auto a = valid_profile(); a.id = "a";
  auto b = valid_profile(); b.id = "b";
  VH_EXPECT(reg.add(a));
  VH_EXPECT(reg.add(b));
  VH_EXPECT(reg.size() == 2);
  VH_EXPECT(reg.active_id() == "a");  // first added is active
  VH_EXPECT(reg.list().size() == 2);
  VH_EXPECT(reg.get("b") != nullptr);
  VH_EXPECT(reg.get("missing") == nullptr);
  VH_EXPECT(reg.set_active("b"));
  VH_EXPECT(reg.active() != nullptr && reg.active()->id == "b");
  VH_EXPECT(!reg.set_active("missing"));
  // Invalid profile rejected.
  auto bad = valid_profile(); bad.id.clear();
  VH_EXPECT(!reg.add(bad));
  // Re-add same id replaces.
  auto a2 = valid_profile(); a2.id = "a"; a2.min_confidence_mille = 999;
  VH_EXPECT(reg.add(a2));
  VH_EXPECT(reg.size() == 2);
  VH_EXPECT(reg.get("a")->min_confidence_mille == 999);
}

}  // namespace

int main() {
  test_validation();
  test_text_roundtrip();
  test_parse_rejects_bad_sensor();
  test_json_output();
  test_rad_per_pixel();
  test_ground_footprint();
  test_ground_footprint_rejects_bad_altitude();
  test_visual_scale_mismatch();
  test_to_matcher_config();
  test_imx219_builtin();
  test_registry();
  return ::vh::test::summary("test_camera_profile");
}
