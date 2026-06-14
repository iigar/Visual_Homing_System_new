#include "vh/mavlink.hpp"

#include <cstddef>
#include <cstdint>

namespace vh {

std::uint16_t mavlink_crc(const std::uint8_t* data, std::size_t n,
                          std::uint8_t crc_extra) noexcept {
  std::uint16_t crc = kMavlinkCrcInit;
  for (std::size_t i = 0; i < n; ++i) {
    mavlink_crc_accumulate(data[i], crc);
  }
  mavlink_crc_accumulate(crc_extra, crc);
  return crc;
}

bool mavlink_crc_extra(std::uint32_t msgid, std::uint8_t& out) noexcept {
  switch (msgid) {
    case kMsgHeartbeat:          out = 50;  return true;
    case kMsgAttitude:           out = 39;  return true;
    case kMsgGlobalPositionInt:  out = 104; return true;
    default:                     return false;
  }
}

void MavlinkParser::reset() noexcept {
  state_ = State::Idle;
  crc_ = kMavlinkCrcInit;
  payload_idx_ = 0;
  signature_idx_ = 0;
  signed_ = false;
}

void MavlinkParser::begin_frame(std::uint8_t version) noexcept {
  version_ = version;
  crc_ = kMavlinkCrcInit;
  payload_idx_ = 0;
  signature_idx_ = 0;
  incompat_ = 0;
  compat_ = 0;
  msgid_ = 0;
  signed_ = false;
  state_ = State::Len;
}

bool MavlinkParser::finish_crc() noexcept {
  std::uint8_t crc_extra = 0;
  // Unknown message id: cannot verify CRC, so never trust it. We still
  // consumed the declared payload, so the stream stays aligned.
  if (!mavlink_crc_extra(msgid_, crc_extra)) {
    ++counters_.frames_unknown_msgid;
    return false;
  }
  mavlink_crc_accumulate(crc_extra, crc_);
  if (crc_ != crc_recv_) {
    ++counters_.frames_crc_error;
    return false;
  }
  // Valid frame — buffer it; the caller receives it once signature (if any)
  // is consumed.
  pending_ = MavlinkMessage{};
  pending_.version = version_;
  pending_.incompat_flags = incompat_;
  pending_.compat_flags = compat_;
  pending_.seq = seq_;
  pending_.sysid = sysid_;
  pending_.compid = compid_;
  pending_.msgid = msgid_;
  pending_.payload_len = len_;
  pending_.signed_frame = signed_;
  for (std::size_t i = 0; i < len_; ++i) {
    pending_.payload[i] = payload_[i];
  }
  ++counters_.frames_ok;
  return true;
}

bool MavlinkParser::parse_byte(std::uint8_t b, MavlinkMessage& out) noexcept {
  ++counters_.bytes_seen;

  switch (state_) {
    case State::Idle:
      if (b == kMavlinkStxV1) {
        begin_frame(1);
      } else if (b == kMavlinkStxV2) {
        begin_frame(2);
      } else {
        ++counters_.bytes_discarded;
      }
      return false;

    case State::Len:
      len_ = b;
      accumulate(b);
      state_ = (version_ == 2) ? State::IncompatFlags : State::Seq;
      return false;

    case State::IncompatFlags:
      incompat_ = b;
      signed_ = (b & kMavlinkV2IflagSigned) != 0;
      accumulate(b);
      state_ = State::CompatFlags;
      return false;

    case State::CompatFlags:
      compat_ = b;
      accumulate(b);
      state_ = State::Seq;
      return false;

    case State::Seq:
      seq_ = b;
      accumulate(b);
      state_ = State::SysId;
      return false;

    case State::SysId:
      sysid_ = b;
      accumulate(b);
      state_ = State::CompId;
      return false;

    case State::CompId:
      compid_ = b;
      accumulate(b);
      state_ = State::MsgId1;
      return false;

    case State::MsgId1:
      accumulate(b);
      if (version_ == 2) {
        msgid_ = b;  // little-endian byte 0
        state_ = State::MsgId2;
      } else {
        msgid_ = b;
        state_ = (len_ > 0) ? State::Payload : State::CrcLo;
      }
      return false;

    case State::MsgId2:
      accumulate(b);
      msgid_ |= static_cast<std::uint32_t>(b) << 8;
      state_ = State::MsgId3;
      return false;

    case State::MsgId3:
      accumulate(b);
      msgid_ |= static_cast<std::uint32_t>(b) << 16;
      state_ = (len_ > 0) ? State::Payload : State::CrcLo;
      return false;

    case State::Payload:
      payload_[payload_idx_++] = b;
      accumulate(b);
      if (payload_idx_ >= len_) {
        state_ = State::CrcLo;
      }
      return false;

    case State::CrcLo:
      crc_recv_ = b;
      state_ = State::CrcHi;
      return false;

    case State::CrcHi: {
      crc_recv_ |= static_cast<std::uint16_t>(b) << 8;
      const bool ok = finish_crc();
      if (signed_) {
        // Consume + discard the 13-byte signature to stay aligned before
        // emitting. We do not verify signatures (read-only telemetry).
        pending_emit_ = ok;
        signature_idx_ = 0;
        state_ = State::Signature;
        return false;
      }
      state_ = State::Idle;
      if (ok) out = pending_;
      return ok;
    }

    case State::Signature:
      ++signature_idx_;
      if (signature_idx_ >= kMavlinkV2SignatureLen) {
        state_ = State::Idle;
        const bool emit = pending_emit_;
        pending_emit_ = false;
        if (emit) out = pending_;
        return emit;
      }
      return false;
  }
  return false;
}

}  // namespace vh
