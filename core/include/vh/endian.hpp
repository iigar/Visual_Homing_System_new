#pragma once

// Explicit little-endian integer load/store. Avoids any reliance on host
// endianness — VHRS files written on one platform must be bit-identical
// when read on another (desktop, Pi, future thermal devices).
//
// All helpers operate on raw byte buffers; the caller is responsible for
// bounds checking.

#include <cstdint>

namespace vh {

inline void store_le16(std::uint8_t* p, std::uint16_t v) noexcept {
  p[0] = static_cast<std::uint8_t>(v & 0xFF);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

inline void store_le32(std::uint8_t* p, std::uint32_t v) noexcept {
  p[0] = static_cast<std::uint8_t>(v & 0xFF);
  p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

inline void store_le64(std::uint8_t* p, std::uint64_t v) noexcept {
  for (int i = 0; i < 8; ++i) {
    p[i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
  }
}

inline std::uint16_t load_le16(const std::uint8_t* p) noexcept {
  return static_cast<std::uint16_t>(p[0]) |
         (static_cast<std::uint16_t>(p[1]) << 8);
}

inline std::uint32_t load_le32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t load_le64(const std::uint8_t* p) noexcept {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
  }
  return v;
}

// Signed variants — two's complement on all supported platforms.
inline void store_le_i32(std::uint8_t* p, std::int32_t v) noexcept {
  store_le32(p, static_cast<std::uint32_t>(v));
}
inline void store_le_i64(std::uint8_t* p, std::int64_t v) noexcept {
  store_le64(p, static_cast<std::uint64_t>(v));
}
inline std::int32_t load_le_i32(const std::uint8_t* p) noexcept {
  return static_cast<std::int32_t>(load_le32(p));
}
inline std::int64_t load_le_i64(const std::uint8_t* p) noexcept {
  return static_cast<std::int64_t>(load_le64(p));
}

}  // namespace vh
