#include "vh/replay_camera.hpp"

#include "vh/pgm.hpp"

namespace vh {

const char* to_string(ReplayError e) noexcept {
  switch (e) {
    case ReplayError::None: return "None";
    case ReplayError::ManifestInvalid: return "ManifestInvalid";
    case ReplayError::PgmFailed: return "PgmFailed";
  }
  return "Unknown";
}

ReplayCameraSource::ReplayCameraSource(std::vector<ManifestEntry> entries)
    : entries_(std::move(entries)) {}

std::optional<Frame> ReplayCameraSource::next_frame() {
  if (cursor_ >= entries_.size()) return std::nullopt;

  const ManifestEntry& e = entries_[cursor_++];
  const auto pgm = load_pgm_gray8(e.path, e.id, e.timestamp_ns);
  if (pgm.error != PgmError::None) {
    last_error_ = ReplayError::PgmFailed;
    last_error_detail_ =
        std::string(to_string(pgm.error)) + ": " + e.path +
        (pgm.detail.empty() ? std::string{} : " (" + pgm.detail + ")");
    return std::nullopt;
  }
  return pgm.frame;
}

}  // namespace vh
