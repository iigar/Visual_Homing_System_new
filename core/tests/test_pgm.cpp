#include "vh/pgm.hpp"
#include "vh_test.hpp"

#include <string>

namespace {

std::string make_pgm(unsigned w, unsigned h, unsigned maxval,
                     const std::string& payload, const char* extra_ws = " ") {
  std::string s = "P5\n";
  s += std::to_string(w);
  s += ' ';
  s += std::to_string(h);
  s += ' ';
  s += std::to_string(maxval);
  s += extra_ws;
  s += payload;
  return s;
}

void test_basic_4x2() {
  const std::string payload(8, '\xAB');
  const std::string bytes = make_pgm(4, 2, 255, payload);
  const auto r = vh::read_pgm_gray8(bytes, /*id=*/7, /*ts=*/123);
  VH_EXPECT(r.error == vh::PgmError::None);
  VH_EXPECT(r.frame.valid());
  VH_EXPECT(r.frame.id == 7);
  VH_EXPECT(r.frame.timestamp_ns == 123);
  VH_EXPECT(r.frame.width == 4);
  VH_EXPECT(r.frame.height == 2);
  VH_EXPECT(r.frame.payload.size() == 8);
  VH_EXPECT(r.frame.payload[0] == 0xAB);
}

void test_comments_in_header() {
  std::string s = "P5\n# pi camera\n";
  s += "4 2\n";
  s += "# tail comment before maxval\n";
  s += "255\n";
  s += std::string(8, '\x00');
  const auto r = vh::read_pgm_gray8(s, 0, 0);
  VH_EXPECT(r.error == vh::PgmError::None);
}

void test_bad_magic_rejected() {
  const std::string s = "P2\n4 2\n255 " + std::string(8, '\x00');
  const auto r = vh::read_pgm_gray8(s, 0, 0);
  VH_EXPECT(r.error == vh::PgmError::BadMagic);
}

void test_zero_dimension_rejected() {
  const auto r = vh::read_pgm_gray8("P5\n0 2 255 ", 0, 0);
  VH_EXPECT(r.error == vh::PgmError::InvalidDimensions);
}

void test_unsupported_maxval_rejected() {
  // 16-bit PGM (maxval=65535) must not be accepted by the Gray8 reader.
  const std::string s = "P5\n4 2 65535 " + std::string(16, '\x00');
  const auto r = vh::read_pgm_gray8(s, 0, 0);
  VH_EXPECT(r.error == vh::PgmError::UnsupportedMaxval);
}

void test_short_payload_rejected() {
  const std::string s = "P5\n4 2 255 " + std::string(5, '\x00');  // need 8
  const auto r = vh::read_pgm_gray8(s, 0, 0);
  VH_EXPECT(r.error == vh::PgmError::ShortPayload);
}

void test_trailing_bytes_rejected() {
  const std::string s = "P5\n4 2 255 " + std::string(9, '\x00');  // 1 byte over
  const auto r = vh::read_pgm_gray8(s, 0, 0);
  VH_EXPECT(r.error == vh::PgmError::TrailingBytes);
}

void test_missing_whitespace_before_payload() {
  // Replace the separating whitespace with a payload byte directly: header
  // parser cannot find a whitespace boundary.
  std::string s = "P5\n4 2 255";
  s += std::string(8, '\x00');
  const auto r = vh::read_pgm_gray8(s, 0, 0);
  // The character right after 255 is '\x00' — not whitespace — so this is
  // either MissingHeaderField or treated as no-separator. Both indicate bad
  // input; assert it does not silently succeed.
  VH_EXPECT(r.error != vh::PgmError::None);
}

}  // namespace

int main() {
  test_basic_4x2();
  test_comments_in_header();
  test_bad_magic_rejected();
  test_zero_dimension_rejected();
  test_unsupported_maxval_rejected();
  test_short_payload_rejected();
  test_trailing_bytes_rejected();
  test_missing_whitespace_before_payload();
  return vh::test::summary("test_pgm");
}
