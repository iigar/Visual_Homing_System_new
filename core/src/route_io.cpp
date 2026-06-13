#include "vh/route_io.hpp"

#include <cstring>
#include <sstream>

#include "vh/digest.hpp"
#include "vh/endian.hpp"
#include "vh/route_format.hpp"

namespace vh {

namespace {

std::uint16_t pixel_format_code(PixelFormat f) noexcept {
  return static_cast<std::uint16_t>(f);
}

bool pixel_format_from_code(std::uint16_t code, PixelFormat& out) noexcept {
  switch (code) {
    case 1: out = PixelFormat::Gray8; return true;
    case 2: out = PixelFormat::Thermal16; return true;
    default: return false;
  }
}

// Encode entry header into `buf` (kVhrsEntryHeaderSize bytes).
void encode_entry_header(std::uint8_t* buf, const RouteEntry& e) noexcept {
  store_le64(buf + 0, e.frame_id);
  store_le_i64(buf + 8, e.timestamp_ns);
  store_le32(buf + 16, e.altitude_band_mm);
  store_le_i32(buf + 20, e.heading_millirad);
  store_le32(buf + 24, e.width);
  store_le32(buf + 28, e.height);
  store_le16(buf + 32, pixel_format_code(e.format));
  store_le16(buf + 34, 0);  // reserved
  store_le32(buf + 36, static_cast<std::uint32_t>(e.payload.size()));
}

std::uint32_t header_digest_for(const std::uint8_t* header_first_12) noexcept {
  return static_cast<std::uint32_t>(
      fnv1a64(header_first_12, kVhrsHeaderDigestRange) & 0xFFFFFFFFu);
}

}  // namespace

const char* to_string(RouteIoError e) noexcept {
  switch (e) {
    case RouteIoError::None: return "None";
    case RouteIoError::IoOpenFailed: return "IoOpenFailed";
    case RouteIoError::IoWriteFailed: return "IoWriteFailed";
    case RouteIoError::IoReadFailed: return "IoReadFailed";
    case RouteIoError::AlreadyFinalized: return "AlreadyFinalized";
    case RouteIoError::EntryInvalid: return "EntryInvalid";
    case RouteIoError::DimensionsTooLarge: return "DimensionsTooLarge";
    case RouteIoError::PayloadTooLarge: return "PayloadTooLarge";
    case RouteIoError::TooManyEntries: return "TooManyEntries";
    case RouteIoError::BadMagic: return "BadMagic";
    case RouteIoError::UnsupportedVersion: return "UnsupportedVersion";
    case RouteIoError::NonZeroReservedFlags: return "NonZeroReservedFlags";
    case RouteIoError::HeaderDigestMismatch: return "HeaderDigestMismatch";
    case RouteIoError::TruncatedFile: return "TruncatedFile";
    case RouteIoError::TrailingBytes: return "TrailingBytes";
    case RouteIoError::PayloadLengthMismatch: return "PayloadLengthMismatch";
    case RouteIoError::UnsupportedPixelFormat: return "UnsupportedPixelFormat";
    case RouteIoError::EntryReservedNonZero: return "EntryReservedNonZero";
  }
  return "Unknown";
}

// =============================================================================
// Writer
// =============================================================================

VhrsWriter::VhrsWriter(const std::filesystem::path& path)
    : out_(path, std::ios::binary | std::ios::trunc) {
  if (!out_) {
    set_error(RouteIoError::IoOpenFailed);
    return;
  }
  // Reserve space for the file header — written for real in finalize().
  std::uint8_t zero[kVhrsFileHeaderSize] = {};
  out_.write(reinterpret_cast<const char*>(zero), kVhrsFileHeaderSize);
  if (!out_) set_error(RouteIoError::IoWriteFailed);
}

VhrsWriter::~VhrsWriter() {
  if (!finalized_ && out_.is_open()) {
    // Best-effort finalize on destruction; ignore failure (caller should have
    // called finalize() explicitly and inspected the result).
    finalize();
  }
}

bool VhrsWriter::append(const RouteEntry& entry) {
  if (finalized_) {
    set_error(RouteIoError::AlreadyFinalized);
    return false;
  }
  if (!out_) {
    set_error(RouteIoError::IoWriteFailed);
    return false;
  }
  if (!entry.valid()) {
    set_error(RouteIoError::EntryInvalid);
    return false;
  }
  if (entry.width > kVhrsMaxDimension || entry.height > kVhrsMaxDimension) {
    set_error(RouteIoError::DimensionsTooLarge);
    return false;
  }
  if (entry.payload.size() > kVhrsMaxPayloadBytes) {
    set_error(RouteIoError::PayloadTooLarge);
    return false;
  }
  if (entries_written_ >= kVhrsMaxEntries) {
    set_error(RouteIoError::TooManyEntries);
    return false;
  }

  std::uint8_t hdr[kVhrsEntryHeaderSize];
  encode_entry_header(hdr, entry);
  out_.write(reinterpret_cast<const char*>(hdr), kVhrsEntryHeaderSize);
  if (!out_) {
    set_error(RouteIoError::IoWriteFailed);
    return false;
  }
  if (!entry.payload.empty()) {
    out_.write(reinterpret_cast<const char*>(entry.payload.data()),
               static_cast<std::streamsize>(entry.payload.size()));
    if (!out_) {
      set_error(RouteIoError::IoWriteFailed);
      return false;
    }
  }
  ++entries_written_;
  return true;
}

bool VhrsWriter::finalize() {
  if (finalized_) return true;  // idempotent
  if (!out_) {
    set_error(RouteIoError::IoWriteFailed);
    return false;
  }

  std::uint8_t hdr[kVhrsFileHeaderSize] = {};
  std::memcpy(hdr + kVhrsOffMagic, kVhrsMagic, sizeof(kVhrsMagic));
  store_le16(hdr + kVhrsOffVersion, kVhrsVersion);
  store_le16(hdr + kVhrsOffFlags, 0);
  store_le32(hdr + kVhrsOffEntryCount, entries_written_);
  const std::uint32_t digest = header_digest_for(hdr);
  store_le32(hdr + kVhrsOffHeaderDigest, digest);
  // bytes [16..32) already zero.

  out_.seekp(0, std::ios::beg);
  out_.write(reinterpret_cast<const char*>(hdr), kVhrsFileHeaderSize);
  out_.flush();
  if (!out_) {
    set_error(RouteIoError::IoWriteFailed);
    return false;
  }
  out_.close();
  finalized_ = true;
  return true;
}

// =============================================================================
// Reader
// =============================================================================

VhrsLoadResult parse_vhrs(std::string_view bytes_view) {
  VhrsLoadResult r;
  const auto* bytes =
      reinterpret_cast<const std::uint8_t*>(bytes_view.data());
  const std::size_t size = bytes_view.size();

  // Whole-file digest first (always meaningful even on failure).
  r.file_digest = fnv1a64(bytes, size);

  if (size < kVhrsFileHeaderSize) {
    r.error = RouteIoError::TruncatedFile;
    return r;
  }
  if (std::memcmp(bytes + kVhrsOffMagic, kVhrsMagic, sizeof(kVhrsMagic)) != 0) {
    r.error = RouteIoError::BadMagic;
    return r;
  }
  r.version = load_le16(bytes + kVhrsOffVersion);
  if (r.version != kVhrsVersion) {
    r.error = RouteIoError::UnsupportedVersion;
    return r;
  }
  r.flags = load_le16(bytes + kVhrsOffFlags);
  if (r.flags != 0) {
    r.error = RouteIoError::NonZeroReservedFlags;
    return r;
  }
  const std::uint32_t entry_count = load_le32(bytes + kVhrsOffEntryCount);
  const std::uint32_t stored_digest = load_le32(bytes + kVhrsOffHeaderDigest);

  // Recompute header digest with the digest field temporarily zeroed.
  std::uint8_t hdr_copy[kVhrsHeaderDigestRange];
  std::memcpy(hdr_copy, bytes, kVhrsHeaderDigestRange);
  const std::uint32_t expected_digest = header_digest_for(hdr_copy);
  if (expected_digest != stored_digest) {
    r.error = RouteIoError::HeaderDigestMismatch;
    return r;
  }

  if (entry_count > kVhrsMaxEntries) {
    r.error = RouteIoError::TooManyEntries;
    return r;
  }
  // Reserved tail must be zero.
  for (std::size_t i = kVhrsOffReserved; i < kVhrsFileHeaderSize; ++i) {
    if (bytes[i] != 0) {
      r.error = RouteIoError::NonZeroReservedFlags;
      return r;
    }
  }

  std::size_t cursor = kVhrsFileHeaderSize;
  r.entries.reserve(entry_count);
  for (std::uint32_t i = 0; i < entry_count; ++i) {
    r.error_entry_index = i;
    if (size - cursor < kVhrsEntryHeaderSize) {
      r.error = RouteIoError::TruncatedFile;
      return r;
    }
    const std::uint8_t* eh = bytes + cursor;
    RouteEntry e;
    e.frame_id = load_le64(eh + 0);
    e.timestamp_ns = load_le_i64(eh + 8);
    e.altitude_band_mm = load_le32(eh + 16);
    e.heading_millirad = load_le_i32(eh + 20);
    e.width = load_le32(eh + 24);
    e.height = load_le32(eh + 28);
    const std::uint16_t fmt_code = load_le16(eh + 32);
    const std::uint16_t reserved = load_le16(eh + 34);
    const std::uint32_t payload_length = load_le32(eh + 36);

    if (reserved != 0) {
      r.error = RouteIoError::EntryReservedNonZero;
      return r;
    }
    if (!pixel_format_from_code(fmt_code, e.format)) {
      r.error = RouteIoError::UnsupportedPixelFormat;
      return r;
    }
    if (e.width == 0 || e.height == 0 ||
        e.width > kVhrsMaxDimension || e.height > kVhrsMaxDimension) {
      r.error = RouteIoError::DimensionsTooLarge;
      return r;
    }
    if (payload_length > kVhrsMaxPayloadBytes) {
      r.error = RouteIoError::PayloadTooLarge;
      return r;
    }
    const std::size_t expected =
        static_cast<std::size_t>(e.width) * e.height *
        Frame::bytes_per_pixel(e.format);
    if (expected != payload_length) {
      r.error = RouteIoError::PayloadLengthMismatch;
      return r;
    }
    cursor += kVhrsEntryHeaderSize;
    if (size - cursor < payload_length) {
      r.error = RouteIoError::TruncatedFile;
      return r;
    }
    e.payload.assign(bytes + cursor, bytes + cursor + payload_length);
    cursor += payload_length;
    r.entries.push_back(std::move(e));
  }

  r.error_entry_index = 0;
  if (cursor != size) {
    r.error = RouteIoError::TrailingBytes;
    return r;
  }
  return r;
}

VhrsReader::VhrsReader(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    result_.error = RouteIoError::IoOpenFailed;
    return;
  }
  std::ostringstream buf;
  buf << f.rdbuf();
  if (!f && !f.eof()) {
    result_.error = RouteIoError::IoReadFailed;
    return;
  }
  result_ = parse_vhrs(buf.str());
}

std::optional<RouteEntry> VhrsReader::entry(std::size_t index) const {
  if (index >= result_.entries.size()) return std::nullopt;
  return result_.entries[index];
}

}  // namespace vh
