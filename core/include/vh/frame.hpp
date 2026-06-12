#pragma once

#include <cstdint>
#include <vector>

namespace vh {

enum class PixelFormat : std::uint8_t {
  Gray8 = 1,
  // Reserved for future sensor payloads (VHRS keeps room for these).
  Thermal16 = 2,
};

struct Frame {
  std::uint64_t id = 0;
  std::int64_t timestamp_ns = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  PixelFormat format = PixelFormat::Gray8;
  std::vector<std::uint8_t> payload;

  [[nodiscard]] bool valid() const noexcept {
    if (width == 0 || height == 0) return false;
    const std::size_t expected =
        static_cast<std::size_t>(width) * height * bytes_per_pixel(format);
    return payload.size() == expected;
  }

  static constexpr std::size_t bytes_per_pixel(PixelFormat f) noexcept {
    switch (f) {
      case PixelFormat::Gray8: return 1;
      case PixelFormat::Thermal16: return 2;
    }
    return 0;
  }
};

}  // namespace vh
