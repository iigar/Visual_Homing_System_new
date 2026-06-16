#include <string>

#include "vh/pi_camera.hpp"
#include "vh_test.hpp"

namespace {

vh::PiCameraConfig valid_config() {
  vh::PiCameraConfig c;
  c.capture_width = 640;
  c.capture_height = 480;
  c.buffer_count = 4;
  c.frame_timeout_ms = 1000;
  c.camera_index = 0;
  return c;
}

// --- Config validation (platform-independent) ------------------------------
void test_config_validation() {
  std::string err;
  VH_EXPECT(vh::validate_pi_camera_config(valid_config(), err));

  auto c = valid_config();
  c.capture_width = 0;
  VH_EXPECT(!vh::validate_pi_camera_config(c, err));

  c = valid_config();
  c.capture_height = 0;
  VH_EXPECT(!vh::validate_pi_camera_config(c, err));

  c = valid_config();
  c.capture_width = vh::kPiCameraMaxDim + 1;
  VH_EXPECT(!vh::validate_pi_camera_config(c, err));

  c = valid_config();
  c.buffer_count = 0;
  VH_EXPECT(!vh::validate_pi_camera_config(c, err));

  c = valid_config();
  c.frame_timeout_ms = 0;
  VH_EXPECT(!vh::validate_pi_camera_config(c, err));

  c = valid_config();
  c.frame_timeout_ms = -5;
  VH_EXPECT(!vh::validate_pi_camera_config(c, err));
}

// open() validates config before the compile-time backend gate, so an invalid
// config fails the same way on desktop and Pi.
void test_open_invalid_config() {
  auto c = valid_config();
  c.capture_width = 0;
  vh::PiCameraSource src(c);
  VH_EXPECT(!src.open());
  VH_EXPECT(src.last_error() == vh::PiCameraError::InvalidConfig);
  VH_EXPECT(!src.is_open());
}

// next_frame() before a successful open() is fail-closed everywhere.
void test_next_frame_before_open() {
  vh::PiCameraSource src(valid_config());
  VH_EXPECT(!src.next_frame().has_value());
  VH_EXPECT(src.last_error() == vh::PiCameraError::NotStarted);
  VH_EXPECT(src.frames_captured() == 0);
}

// close() is safe before open and idempotent.
void test_close_safe() {
  vh::PiCameraSource src(valid_config());
  src.close();
  src.close();
  VH_EXPECT(!src.is_open());
}

void test_error_strings() {
  VH_EXPECT(std::string(vh::to_string(vh::PiCameraError::None)) == "None");
  VH_EXPECT(std::string(vh::to_string(vh::PiCameraError::NotCompiledIn)) ==
            "NotCompiledIn");
  VH_EXPECT(std::string(vh::to_string(vh::PiCameraError::InvalidConfig)) ==
            "InvalidConfig");
  VH_EXPECT(std::string(vh::to_string(vh::PiCameraError::CaptureTimeout)) ==
            "CaptureTimeout");
}

#if !VH_ENABLE_LIBCAMERA
// Desktop builds have no libcamera backend: a valid config still refuses to
// open and reports NotCompiledIn — the whole capture path is fail-closed.
void test_desktop_fail_closed() {
  vh::PiCameraSource src(valid_config());
  VH_EXPECT(!src.open());
  VH_EXPECT(src.last_error() == vh::PiCameraError::NotCompiledIn);
  VH_EXPECT(!src.is_open());
  VH_EXPECT(!src.next_frame().has_value());
  VH_EXPECT(src.last_error() == vh::PiCameraError::NotStarted);
}
#endif

}  // namespace

int main() {
  test_config_validation();
  test_open_invalid_config();
  test_next_frame_before_open();
  test_close_safe();
  test_error_strings();
#if !VH_ENABLE_LIBCAMERA
  test_desktop_fail_closed();
#endif
  return ::vh::test::summary("pi_camera");
}
