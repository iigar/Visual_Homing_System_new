#pragma once

// A single entry in a VHRS route artifact. One entry per recorded frame.
// Metadata is intentionally coarse — millimetre altitude, milliradian
// heading — to keep the binary format integer-only and bit-exact across
// desktop and Pi.

#include <cstdint>
#include <limits>
#include <vector>

#include "vh/frame.hpp"

namespace vh {

struct RouteEntry {
  std::uint64_t frame_id = 0;
  std::int64_t timestamp_ns = 0;

  // Altitude above home, millimetres. 0xFFFFFFFF means "unknown".
  std::uint32_t altitude_band_mm = 0xFFFFFFFFu;

  // Heading hint, milliradians, signed (-pi..+pi range expected but not
  // enforced here). INT32_MIN means "unknown".
  std::int32_t heading_millirad = std::numeric_limits<std::int32_t>::min();

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  PixelFormat format = PixelFormat::Gray8;
  std::vector<std::uint8_t> payload;

  [[nodiscard]] bool valid() const noexcept {
    if (width == 0 || height == 0) return false;
    const std::size_t expected = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) *
                                 Frame::bytes_per_pixel(format);
    return payload.size() == expected;
  }

  static constexpr std::uint32_t kAltitudeUnknown = 0xFFFFFFFFu;
  static constexpr std::int32_t kHeadingUnknown =
      std::numeric_limits<std::int32_t>::min();
};

}  // namespace vh
