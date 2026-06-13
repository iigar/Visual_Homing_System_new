#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vh/digest.hpp"
#include "vh/endian.hpp"
#include "vh/route_entry.hpp"
#include "vh/route_format.hpp"
#include "vh/route_io.hpp"
#include "vh_test.hpp"

namespace fs = std::filesystem;

namespace {

fs::path make_scratch_dir() {
  static std::atomic<unsigned> n{0};
  const unsigned k = n.fetch_add(1, std::memory_order_relaxed);
  auto dir = fs::temp_directory_path() /
             ("vh_test_route_io_" + std::to_string(::getpid()) + "_" +
              std::to_string(k));
  fs::create_directories(dir);
  return dir;
}

vh::RouteEntry make_entry(std::uint64_t id, std::int64_t ts,
                          std::uint32_t w, std::uint32_t h,
                          std::uint8_t fill) {
  vh::RouteEntry e;
  e.frame_id = id;
  e.timestamp_ns = ts;
  e.width = w;
  e.height = h;
  e.format = vh::PixelFormat::Gray8;
  e.payload.assign(static_cast<std::size_t>(w) * h, fill);
  return e;
}

std::string read_file(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream buf;
  buf << f.rdbuf();
  return buf.str();
}

void test_roundtrip_three_entries() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";

  {
    vh::VhrsWriter w(path);
    VH_EXPECT(w.last_error() == vh::RouteIoError::None);
    VH_EXPECT(w.append(make_entry(0, 1'000, 4, 2, 0x10)));
    VH_EXPECT(w.append(make_entry(1, 2'000, 4, 2, 0x20)));
    VH_EXPECT(w.append(make_entry(2, 3'000, 4, 2, 0x30)));
    VH_EXPECT(w.finalize());
    VH_EXPECT(w.entries_written() == 3);
  }

  vh::VhrsReader r(path);
  VH_EXPECT(r.error() == vh::RouteIoError::None);
  VH_EXPECT(r.entry_count() == 3);
  VH_EXPECT(r.result().version == 1);
  VH_EXPECT(r.result().flags == 0);

  auto e0 = r.entry(0);
  VH_EXPECT(e0 && e0->frame_id == 0 && e0->timestamp_ns == 1000);
  VH_EXPECT(e0->payload.size() == 8);
  VH_EXPECT(e0->payload[0] == 0x10);

  auto e2 = r.entry(2);
  VH_EXPECT(e2 && e2->payload[0] == 0x30);

  VH_EXPECT(!r.entry(3));  // out of range

  fs::remove_all(dir);
}

void test_writer_rejects_invalid_entry() {
  const auto dir = make_scratch_dir();
  vh::VhrsWriter w(dir / "bad.vhrs");
  vh::RouteEntry e;  // zero dims, invalid
  VH_EXPECT(!w.append(e));
  VH_EXPECT(w.last_error() == vh::RouteIoError::EntryInvalid);
  fs::remove_all(dir);
}

void test_writer_rejects_oversized() {
  const auto dir = make_scratch_dir();
  vh::VhrsWriter w(dir / "big.vhrs");
  vh::RouteEntry e;
  e.width = vh::kVhrsMaxDimension + 1;
  e.height = 1;
  e.format = vh::PixelFormat::Gray8;
  e.payload.assign(static_cast<std::size_t>(e.width) * e.height, 0);
  VH_EXPECT(!w.append(e));
  VH_EXPECT(w.last_error() == vh::RouteIoError::DimensionsTooLarge);
  fs::remove_all(dir);
}

void test_reader_rejects_bad_magic() {
  std::string bytes(vh::kVhrsFileHeaderSize, '\x00');
  bytes[0] = 'X';
  bytes[1] = 'H';
  bytes[2] = 'R';
  bytes[3] = 'S';
  const auto r = vh::parse_vhrs(bytes);
  VH_EXPECT(r.error == vh::RouteIoError::BadMagic);
}

void test_reader_rejects_short_file() {
  const std::string bytes(vh::kVhrsFileHeaderSize - 1, '\x00');
  const auto r = vh::parse_vhrs(bytes);
  VH_EXPECT(r.error == vh::RouteIoError::TruncatedFile);
}

void test_reader_rejects_wrong_version() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    w.append(make_entry(0, 1, 4, 2, 0x01));
    w.finalize();
  }
  // Bump version in-place, recompute header digest correctly so that the
  // version check (not the digest check) is what trips.
  std::string bytes = read_file(path);
  vh::store_le16(reinterpret_cast<std::uint8_t*>(bytes.data()) +
                     vh::kVhrsOffVersion,
                 2);
  // Recompute header digest over bytes[0..12).
  const auto* p = reinterpret_cast<const std::uint8_t*>(bytes.data());
  const std::uint32_t d =
      static_cast<std::uint32_t>(vh::fnv1a64(p, vh::kVhrsHeaderDigestRange) &
                                 0xFFFFFFFFu);
  vh::store_le32(reinterpret_cast<std::uint8_t*>(bytes.data()) +
                     vh::kVhrsOffHeaderDigest,
                 d);
  const auto r = vh::parse_vhrs(bytes);
  VH_EXPECT(r.error == vh::RouteIoError::UnsupportedVersion);
  fs::remove_all(dir);
}

void test_header_digest_detects_tamper() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    w.append(make_entry(0, 1, 4, 2, 0x01));
    w.finalize();
  }
  std::string bytes = read_file(path);
  // Flip a single bit in entry_count without recomputing digest.
  bytes[vh::kVhrsOffEntryCount] ^= 1;
  const auto r = vh::parse_vhrs(bytes);
  VH_EXPECT(r.error == vh::RouteIoError::HeaderDigestMismatch);
  fs::remove_all(dir);
}

void test_truncated_after_finalize_detected() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    w.append(make_entry(0, 1, 4, 2, 0x01));
    w.append(make_entry(1, 2, 4, 2, 0x02));
    w.finalize();
  }
  std::string bytes = read_file(path);
  // Drop the last 5 bytes of payload.
  bytes.resize(bytes.size() - 5);
  const auto r = vh::parse_vhrs(bytes);
  VH_EXPECT(r.error == vh::RouteIoError::TruncatedFile);
  fs::remove_all(dir);
}

void test_trailing_bytes_detected() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    w.append(make_entry(0, 1, 4, 2, 0x01));
    w.finalize();
  }
  std::string bytes = read_file(path);
  bytes.append(8, '\x00');
  const auto r = vh::parse_vhrs(bytes);
  VH_EXPECT(r.error == vh::RouteIoError::TrailingBytes);
  fs::remove_all(dir);
}

void test_file_digest_changes_on_payload_flip() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    w.append(make_entry(0, 1, 4, 2, 0x42));
    w.finalize();
  }
  const auto baseline = vh::VhrsReader(path);
  VH_EXPECT(baseline.error() == vh::RouteIoError::None);

  std::string bytes = read_file(path);
  // Flip a single payload byte. The header digest stays valid because the
  // header was not touched — this is exactly what the file-level digest is
  // for: detect payload tampering that the header digest cannot.
  const std::size_t flip_offset =
      vh::kVhrsFileHeaderSize + vh::kVhrsEntryHeaderSize + 2;
  bytes[flip_offset] ^= 0xFF;
  const auto modified = vh::parse_vhrs(bytes);
  VH_EXPECT(modified.error == vh::RouteIoError::None);  // structure still valid
  VH_EXPECT(modified.file_digest != baseline.result().file_digest);
  fs::remove_all(dir);
}

void test_unsupported_pixel_format_rejected() {
  // Build a minimal valid file then patch the pixel-format field to an
  // unknown value, restoring header digest so the per-entry check fires.
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    w.append(make_entry(0, 1, 4, 2, 0x01));
    w.finalize();
  }
  std::string bytes = read_file(path);
  // Patch entry pixel format -> 99 (unknown). Entry header starts at offset
  // kVhrsFileHeaderSize; pixel_format is at +32.
  vh::store_le16(reinterpret_cast<std::uint8_t*>(bytes.data()) +
                     vh::kVhrsFileHeaderSize + 32,
                 99);
  const auto r = vh::parse_vhrs(bytes);
  VH_EXPECT(r.error == vh::RouteIoError::UnsupportedPixelFormat);
  fs::remove_all(dir);
}

void test_already_finalized_writer_refuses_append() {
  const auto dir = make_scratch_dir();
  vh::VhrsWriter w(dir / "r.vhrs");
  w.append(make_entry(0, 1, 4, 2, 0x01));
  w.finalize();
  VH_EXPECT(!w.append(make_entry(1, 2, 4, 2, 0x02)));
  VH_EXPECT(w.last_error() == vh::RouteIoError::AlreadyFinalized);
  fs::remove_all(dir);
}

void test_metadata_round_trip_with_unknowns() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  vh::RouteEntry e = make_entry(7, 5'000, 2, 2, 0xAB);
  e.altitude_band_mm = vh::RouteEntry::kAltitudeUnknown;
  e.heading_millirad = vh::RouteEntry::kHeadingUnknown;
  {
    vh::VhrsWriter w(path);
    VH_EXPECT(w.append(e));
    VH_EXPECT(w.finalize());
  }
  vh::VhrsReader r(path);
  VH_EXPECT(r.error() == vh::RouteIoError::None);
  auto got = r.entry(0);
  VH_EXPECT(got);
  VH_EXPECT(got->altitude_band_mm == vh::RouteEntry::kAltitudeUnknown);
  VH_EXPECT(got->heading_millirad == vh::RouteEntry::kHeadingUnknown);
  fs::remove_all(dir);
}

void test_empty_route_finalizes_and_loads() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "empty.vhrs";
  {
    vh::VhrsWriter w(path);
    VH_EXPECT(w.finalize());
    VH_EXPECT(w.entries_written() == 0);
  }
  vh::VhrsReader r(path);
  VH_EXPECT(r.error() == vh::RouteIoError::None);
  VH_EXPECT(r.entry_count() == 0);
  fs::remove_all(dir);
}

}  // namespace

int main() {
  test_roundtrip_three_entries();
  test_writer_rejects_invalid_entry();
  test_writer_rejects_oversized();
  test_reader_rejects_bad_magic();
  test_reader_rejects_short_file();
  test_reader_rejects_wrong_version();
  test_header_digest_detects_tamper();
  test_truncated_after_finalize_detected();
  test_trailing_bytes_detected();
  test_file_digest_changes_on_payload_flip();
  test_unsupported_pixel_format_rejected();
  test_already_finalized_writer_refuses_append();
  test_metadata_round_trip_with_unknowns();
  test_empty_route_finalizes_and_loads();
  return vh::test::summary("test_route_io");
}
