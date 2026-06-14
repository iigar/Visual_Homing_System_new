#pragma once

// Read-only MAVLink frame parser (M8). Byte-streaming state machine that
// accepts both v1 (0xFE) and v2 (0xFD) framing and validates the 16-bit
// CRC (CRC-16/MCRF4XX with the per-message CRC_EXTRA byte). This is the
// untrusted-input boundary: serial bytes may be garbage, injected, or
// truncated, so the parser never trusts a frame whose CRC it cannot verify,
// never reads outside its payload buffer, and exposes counters for every
// outcome. It NEVER produces commands — telemetry only (see DECISIONS D-019).
//
// NOTE: MAVLink message framing and CRC are well-established and stable; the
// CRC_EXTRA constants and field offsets here follow the standard dialect
// definitions and must be cross-checked against pymavlink/NotebookLM before
// hardware bring-up (M11/M15). Desktop tests validate the parser by round-
// tripping frames built with the same documented algorithm.

#include <cstddef>
#include <cstdint>

namespace vh {

// --- Framing constants -------------------------------------------------------
inline constexpr std::uint8_t kMavlinkStxV1 = 0xFE;
inline constexpr std::uint8_t kMavlinkStxV2 = 0xFD;
inline constexpr std::uint16_t kMavlinkCrcInit = 0xFFFF;
inline constexpr std::uint8_t kMavlinkV2IflagSigned = 0x01;  // INCOMPAT bit 0
inline constexpr std::size_t kMavlinkV2SignatureLen = 13;
inline constexpr std::size_t kMavlinkMaxPayload = 255;

// Message IDs this milestone decodes.
inline constexpr std::uint32_t kMsgHeartbeat = 0;
inline constexpr std::uint32_t kMsgAttitude = 30;
inline constexpr std::uint32_t kMsgGlobalPositionInt = 33;

// --- CRC-16/MCRF4XX ----------------------------------------------------------
// MAVLink's crc_accumulate, one byte at a time. Seed with kMavlinkCrcInit.
inline void mavlink_crc_accumulate(std::uint8_t data,
                                   std::uint16_t& crc) noexcept {
  std::uint8_t tmp = static_cast<std::uint8_t>(data ^ (crc & 0xFF));
  tmp = static_cast<std::uint8_t>(tmp ^ (tmp << 4));
  crc = static_cast<std::uint16_t>(
      (crc >> 8) ^ (static_cast<std::uint16_t>(tmp) << 8) ^
      (static_cast<std::uint16_t>(tmp) << 3) ^
      (static_cast<std::uint16_t>(tmp) >> 4));
}

// CRC over a buffer plus the trailing CRC_EXTRA byte, seeded from init.
std::uint16_t mavlink_crc(const std::uint8_t* data, std::size_t n,
                          std::uint8_t crc_extra) noexcept;

// CRC_EXTRA for a known message id. Returns false for unknown ids (whose CRC
// cannot be verified, so the frame must not be trusted).
bool mavlink_crc_extra(std::uint32_t msgid, std::uint8_t& out) noexcept;

// --- Parsed frame ------------------------------------------------------------
struct MavlinkMessage {
  std::uint8_t version = 0;          // 1 or 2
  std::uint8_t incompat_flags = 0;   // v2 only
  std::uint8_t compat_flags = 0;     // v2 only
  std::uint8_t seq = 0;
  std::uint8_t sysid = 0;
  std::uint8_t compid = 0;
  std::uint32_t msgid = 0;
  std::uint8_t payload_len = 0;      // bytes actually received (v2 truncated)
  // Zero-filled to full size so decoders can read fixed offsets even when v2
  // empty-byte truncation dropped trailing zeros.
  std::uint8_t payload[kMavlinkMaxPayload] = {};
  bool signed_frame = false;         // v2 IFLAG_SIGNED was set
};

struct MavlinkCounters {
  std::uint64_t bytes_seen = 0;
  std::uint64_t frames_ok = 0;            // CRC-valid, known msgid
  std::uint64_t frames_crc_error = 0;     // known msgid, CRC mismatch
  std::uint64_t frames_unknown_msgid = 0; // structurally complete, no CRC_EXTRA
  std::uint64_t bytes_discarded = 0;      // dropped while hunting for STX
};

// --- Streaming parser --------------------------------------------------------
// Feed bytes one at a time; parse_byte returns true exactly on the byte that
// completes a CRC-valid, known-msgid frame (filled into `out`). Malformed,
// CRC-failed, and unknown-msgid frames update counters and return false.
class MavlinkParser {
 public:
  MavlinkParser() = default;

  bool parse_byte(std::uint8_t b, MavlinkMessage& out) noexcept;

  const MavlinkCounters& counters() const noexcept { return counters_; }
  void reset() noexcept;

 private:
  enum class State : std::uint8_t {
    Idle,        // hunting for STX
    Len,         // v1/v2: length
    IncompatFlags,  // v2
    CompatFlags,    // v2
    Seq,
    SysId,
    CompId,
    MsgId1,      // v1: full 8-bit msgid; v2: byte 0
    MsgId2,      // v2
    MsgId3,      // v2
    Payload,
    CrcLo,
    CrcHi,
    Signature,   // v2 signed frames: consume + discard 13 bytes
  };

  void begin_frame(std::uint8_t version) noexcept;
  void accumulate(std::uint8_t b) noexcept { mavlink_crc_accumulate(b, crc_); }
  // Validates CRC and, on success, fills pending_ + returns true. Emission to
  // the caller's buffer is deferred until any v2 signature is consumed.
  bool finish_crc() noexcept;

  State state_ = State::Idle;
  MavlinkCounters counters_;

  std::uint8_t version_ = 0;
  std::uint16_t crc_ = kMavlinkCrcInit;       // running CRC over header+payload
  std::uint16_t crc_recv_ = 0;
  std::uint8_t len_ = 0;
  std::uint8_t incompat_ = 0;
  std::uint8_t compat_ = 0;
  std::uint8_t seq_ = 0;
  std::uint8_t sysid_ = 0;
  std::uint8_t compid_ = 0;
  std::uint32_t msgid_ = 0;
  std::uint8_t payload_[kMavlinkMaxPayload] = {};
  std::size_t payload_idx_ = 0;
  std::size_t signature_idx_ = 0;
  bool signed_ = false;

  // Emission is deferred for signed frames until the 13-byte signature is
  // consumed, so the validated message is buffered here meanwhile.
  MavlinkMessage pending_{};
  bool pending_emit_ = false;
};

}  // namespace vh
