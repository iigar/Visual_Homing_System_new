#pragma once

// VHRS v1 — Visual Homing Route Signature, binary format.
//
// File layout:
//   FILE HEADER (32 bytes)
//     [0..4)   magic           "VHRS"
//     [4..6)   version         u16 LE  (current = 1)
//     [6..8)   flags           u16 LE  (reserved, MUST be 0 in v1)
//     [8..12)  entry_count     u32 LE
//     [12..16) header_digest   u32 LE  (FNV-1a low 32 bits of bytes 0..12)
//     [16..32) reserved        zero-filled
//
//   For each entry: ENTRY HEADER (40 bytes) then PAYLOAD (payload_length bytes)
//     [0..8)   frame_id            u64 LE
//     [8..16)  timestamp_ns        i64 LE
//     [16..20) altitude_band_mm    u32 LE  (0xFFFFFFFF = unknown)
//     [20..24) heading_millirad    i32 LE  (INT32_MIN  = unknown)
//     [24..28) width               u32 LE
//     [28..32) height              u32 LE
//     [32..34) pixel_format        u16 LE  (1 = Gray8, 2 = Thermal16)
//     [34..36) reserved            u16 LE  (MUST be 0)
//     [36..40) payload_length      u32 LE
//
// All multi-byte fields little-endian; integer-only metadata (bit-exact).
// Caller-side payload bound: payload_length must equal
//   width * height * bytes_per_pixel(format)
// and width/height must be > 0.

#include <cstddef>
#include <cstdint>

namespace vh {

inline constexpr std::uint8_t kVhrsMagic[4] = {'V', 'H', 'R', 'S'};
inline constexpr std::uint16_t kVhrsVersion = 1;
inline constexpr std::size_t kVhrsFileHeaderSize = 32;
inline constexpr std::size_t kVhrsEntryHeaderSize = 40;
inline constexpr std::size_t kVhrsHeaderDigestRange = 12;  // bytes covered

// File-header offsets.
inline constexpr std::size_t kVhrsOffMagic = 0;
inline constexpr std::size_t kVhrsOffVersion = 4;
inline constexpr std::size_t kVhrsOffFlags = 6;
inline constexpr std::size_t kVhrsOffEntryCount = 8;
inline constexpr std::size_t kVhrsOffHeaderDigest = 12;
inline constexpr std::size_t kVhrsOffReserved = 16;  // 16 bytes reserved tail

// Hard caps to refuse oversized claims before allocation.
inline constexpr std::uint32_t kVhrsMaxDimension = 8192;       // pixels
inline constexpr std::uint32_t kVhrsMaxPayloadBytes = 16 << 20;  // 16 MiB
inline constexpr std::uint32_t kVhrsMaxEntries = 1 << 20;       // ~1M entries

}  // namespace vh
