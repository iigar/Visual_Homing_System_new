#pragma once

// Replay ICameraSource: manifest + PGM files on disk → Frame stream.
// Exhausts at end of manifest; returns nullopt when a row's payload fails to
// load (caller can read last_error() for diagnostics).

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "vh/interfaces.hpp"
#include "vh/manifest.hpp"

namespace vh {

enum class ReplayError {
  None,
  ManifestInvalid,
  PgmFailed,
};

class ReplayCameraSource : public ICameraSource {
 public:
  ReplayCameraSource() = default;
  explicit ReplayCameraSource(std::vector<ManifestEntry> entries);

  std::optional<Frame> next_frame() override;

  // Diagnostics.
  std::size_t cursor() const noexcept { return cursor_; }
  std::size_t total() const noexcept { return entries_.size(); }
  ReplayError last_error() const noexcept { return last_error_; }
  const std::string& last_error_detail() const noexcept {
    return last_error_detail_;
  }

 private:
  std::vector<ManifestEntry> entries_;
  std::size_t cursor_ = 0;
  ReplayError last_error_ = ReplayError::None;
  std::string last_error_detail_;
};

const char* to_string(ReplayError e) noexcept;

}  // namespace vh
