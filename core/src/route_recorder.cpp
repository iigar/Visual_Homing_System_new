#include "vh/route_recorder.hpp"

namespace vh {

RouteSignatureRecorder::RouteSignatureRecorder(
    const std::filesystem::path& path)
    : writer_(std::make_unique<VhrsWriter>(path)) {
  last_error_ = writer_->last_error();
}

RouteSignatureRecorder::~RouteSignatureRecorder() = default;

bool RouteSignatureRecorder::record(const Frame& preprocessed,
                                    const PoseHint& pose) {
  if (!writer_ || writer_->finalized()) {
    last_error_ = RouteIoError::AlreadyFinalized;
    ++rejected_;
    return false;
  }
  if (!preprocessed.valid()) {
    last_error_ = RouteIoError::EntryInvalid;
    ++rejected_;
    return false;
  }
  RouteEntry e;
  e.frame_id = preprocessed.id;
  e.timestamp_ns = preprocessed.timestamp_ns;
  e.altitude_band_mm = pose.altitude_band_mm;
  e.heading_millirad = pose.heading_millirad;
  e.width = preprocessed.width;
  e.height = preprocessed.height;
  e.format = preprocessed.format;
  e.payload = preprocessed.payload;
  const bool ok = writer_->append(e);
  if (!ok) {
    last_error_ = writer_->last_error();
    ++rejected_;
  }
  return ok;
}

bool RouteSignatureRecorder::finalize() {
  if (!writer_) return false;
  const bool ok = writer_->finalize();
  if (!ok) last_error_ = writer_->last_error();
  return ok;
}

std::uint32_t RouteSignatureRecorder::appended() const noexcept {
  return writer_ ? writer_->entries_written() : 0;
}

bool RouteSignatureRecorder::finalized() const noexcept {
  return writer_ && writer_->finalized();
}

}  // namespace vh
