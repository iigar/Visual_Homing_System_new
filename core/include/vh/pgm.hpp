#pragma once

// Binary PGM P5 Gray8 reader. ASCII PGM (P2) and 16-bit PGM (maxval > 255)
// are rejected — Gray8 only at this milestone.
//
// Header grammar (whitespace-separated): "P5" width height maxval, then a
// single whitespace byte, then width*height raw bytes.
// Comment lines start with '#' and run to end-of-line.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "vh/frame.hpp"

namespace vh {

enum class PgmError {
  None,
  IoOpenFailed,
  BadMagic,
  MissingHeaderField,
  InvalidDimensions,
  UnsupportedMaxval,
  ShortPayload,
  TrailingBytes,
};

struct PgmReadResult {
  Frame frame;  // populated only when error == None
  PgmError error = PgmError::None;
  std::string detail;
};

// Reads PGM P5 bytes into a Frame. `id` and `timestamp_ns` are caller-provided
// (the PGM format itself carries neither).
PgmReadResult read_pgm_gray8(std::string_view bytes,
                             std::uint64_t id,
                             std::int64_t timestamp_ns);

PgmReadResult load_pgm_gray8(const std::filesystem::path& path,
                             std::uint64_t id,
                             std::int64_t timestamp_ns);

const char* to_string(PgmError e) noexcept;

}  // namespace vh
