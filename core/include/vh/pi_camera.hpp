#pragma once

// Pi hardware capture (M11). A libcamera-backed ICameraSource for the
// Raspberry Pi (Pi OS Trixie, libcamera). It is the FIRST milestone that
// touches real hardware, so it is gated twice:
//
//   1. Compile-time: the libcamera backend only exists when the build is
//      configured with -DVH_ENABLE_LIBCAMERA=ON (Pi builds only). Desktop
//      builds compile a fail-closed stub instead — no libcamera dependency,
//      no capture, open() always refuses with NotCompiledIn.
//   2. Runtime: even on a Pi, open() must succeed (camera present, stream
//      configured) before next_frame() yields anything. Any failure leaves
//      the source closed and next_frame() returns nullopt (fail-closed).
//
// Capture happens at the profile's *capture* dimensions (raw sensor frame);
// the downstream preprocessor (M2) reduces it to the matcher's *target*
// dimensions. This source never resizes — it hands up a Gray8 capture frame.
//
// The libcamera headers are hidden behind a pimpl so this public header (and
// every desktop test that includes it) stays free of any libcamera include.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "vh/interfaces.hpp"

namespace vh {

// --- Configuration ----------------------------------------------------------
// Capture geometry comes from a CameraProfile (M10); this struct is the small
// subset the capture backend needs. Pixel format is fixed to Gray8 (libcamera
// R8 / single-plane 8-bit) for the visible-light matcher path.
struct PiCameraConfig {
  std::uint32_t capture_width = 0;
  std::uint32_t capture_height = 0;
  std::uint32_t buffer_count = 4;     // request/framebuffer pool size
  std::int64_t frame_timeout_ms = 1000;  // per-frame dequeue timeout
  std::uint32_t camera_index = 0;     // which enumerated camera to acquire
};

// Hard caps — refuse pathological configs before any allocation (mirrors the
// VHRS dimension cap). Capture dims must be positive and within the cap.
inline constexpr std::uint32_t kPiCameraMaxDim = 8192;

enum class PiCameraError {
  None,
  NotCompiledIn,    // VH_ENABLE_LIBCAMERA=OFF — desktop fail-closed stub
  InvalidConfig,    // zero/oversized dims, zero buffers, non-positive timeout
  NoCamera,         // no camera enumerated at camera_index
  AcquireFailed,    // camera could not be acquired
  ConfigureFailed,  // stream configuration / Gray8 unsupported
  AllocateFailed,   // framebuffer allocation failed
  StartFailed,      // camera/request start failed
  NotStarted,       // next_frame() before a successful open()
  CaptureTimeout,   // no completed request within frame_timeout_ms
  CaptureError,     // request completed with an error / cancelled
};

const char* to_string(PiCameraError e) noexcept;

// Platform-independent config validation (runs before the compile-time gate,
// so an invalid config is reported as InvalidConfig on desktop and Pi alike).
bool validate_pi_camera_config(const PiCameraConfig& cfg,
                               std::string& error) noexcept;

// --- Source -----------------------------------------------------------------
class PiCameraSource : public ICameraSource {
 public:
  explicit PiCameraSource(PiCameraConfig cfg);
  ~PiCameraSource() override;

  PiCameraSource(const PiCameraSource&) = delete;
  PiCameraSource& operator=(const PiCameraSource&) = delete;

  // Runtime gate. Validates config, then (libcamera builds) acquires the
  // camera, configures a Gray8 stream at capture dims, allocates buffers and
  // starts capture. Desktop builds always return false with NotCompiledIn.
  // Returns true only when the source is ready to yield frames.
  bool open();

  // Releases the camera and buffers. Safe to call when not open.
  void close();

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // Next captured frame, or nullopt on timeout/error/not-open (fail-closed).
  std::optional<Frame> next_frame() override;

  // Diagnostics.
  [[nodiscard]] PiCameraError last_error() const noexcept { return last_error_; }
  [[nodiscard]] const std::string& last_error_detail() const noexcept {
    return last_error_detail_;
  }
  [[nodiscard]] std::uint64_t frames_captured() const noexcept {
    return frames_captured_;
  }

 private:
  void set_error(PiCameraError e, std::string detail) noexcept;

  PiCameraConfig cfg_;
  bool open_ = false;
  PiCameraError last_error_ = PiCameraError::None;
  std::string last_error_detail_;
  std::uint64_t frames_captured_ = 0;

#if VH_ENABLE_LIBCAMERA
  // libcamera state lives entirely inside the pimpl (Pi builds only).
  struct Impl;
  std::unique_ptr<Impl> impl_;

  bool open_libcamera();
  std::optional<Frame> capture_libcamera();
  void close_libcamera();
#endif
};

}  // namespace vh
