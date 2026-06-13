#pragma once

// RouteSignatureRecorder: takes preprocessed frames + (altitude, heading)
// hints from the navigation estimator and produces VHRS entries through an
// owned VhrsWriter.
//
// Live recording (M11) is explicit hardware-validation mode, not part of the
// realtime command loop — that constraint is enforced by the caller (a
// dedicated recording script), not by this class.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "vh/frame.hpp"
#include "vh/route_entry.hpp"
#include "vh/route_io.hpp"

namespace vh {

struct PoseHint {
  // 0xFFFFFFFF / INT32_MIN if the navigator does not have a fix.
  std::uint32_t altitude_band_mm = RouteEntry::kAltitudeUnknown;
  std::int32_t heading_millirad = RouteEntry::kHeadingUnknown;
};

class RouteSignatureRecorder {
 public:
  explicit RouteSignatureRecorder(const std::filesystem::path& path);
  ~RouteSignatureRecorder();

  // Returns true if the frame was accepted and written.
  // Returns false (and increments `rejected()`) for invalid frames, wrong
  // formats, or writer errors. `last_error()` describes the cause.
  bool record(const Frame& preprocessed, const PoseHint& pose);

  // Finalises the underlying VHRS writer. Idempotent.
  bool finalize();

  std::uint32_t appended() const noexcept;
  std::uint32_t rejected() const noexcept { return rejected_; }
  RouteIoError last_error() const noexcept { return last_error_; }
  bool finalized() const noexcept;

 private:
  std::unique_ptr<VhrsWriter> writer_;
  std::uint32_t rejected_ = 0;
  RouteIoError last_error_ = RouteIoError::None;
};

}  // namespace vh
