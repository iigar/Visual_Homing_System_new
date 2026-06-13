#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "vh/digest.hpp"
#include "vh/endian.hpp"
#include "vh_test.hpp"

namespace {

void test_le16_roundtrip() {
  std::uint8_t buf[2] = {};
  vh::store_le16(buf, 0x1234);
  VH_EXPECT(buf[0] == 0x34);
  VH_EXPECT(buf[1] == 0x12);
  VH_EXPECT(vh::load_le16(buf) == 0x1234);
}

void test_le32_roundtrip() {
  std::uint8_t buf[4] = {};
  vh::store_le32(buf, 0xDEADBEEF);
  VH_EXPECT(buf[0] == 0xEF);
  VH_EXPECT(buf[1] == 0xBE);
  VH_EXPECT(buf[2] == 0xAD);
  VH_EXPECT(buf[3] == 0xDE);
  VH_EXPECT(vh::load_le32(buf) == 0xDEADBEEFu);
}

void test_le64_roundtrip() {
  std::uint8_t buf[8] = {};
  const std::uint64_t v = 0x0123456789ABCDEFull;
  vh::store_le64(buf, v);
  VH_EXPECT(buf[0] == 0xEF);
  VH_EXPECT(buf[7] == 0x01);
  VH_EXPECT(vh::load_le64(buf) == v);
}

void test_signed_roundtrip() {
  std::uint8_t buf[8] = {};
  vh::store_le_i32(buf, -42);
  VH_EXPECT(vh::load_le_i32(buf) == -42);
  vh::store_le_i64(buf, std::numeric_limits<std::int64_t>::min());
  VH_EXPECT(vh::load_le_i64(buf) == std::numeric_limits<std::int64_t>::min());
}

void test_fnv1a_known_values() {
  // RFC 5 "" -> offset basis.
  const std::uint8_t empty[1] = {0};
  VH_EXPECT(vh::fnv1a64(empty, 0) == vh::kFnv1a64Offset);
  // FNV-1a 64-bit of "a" = 0xaf63dc4c8601ec8c.
  const std::uint8_t a = 'a';
  VH_EXPECT(vh::fnv1a64(&a, 1) == 0xaf63dc4c8601ec8cull);
  // "foobar"
  VH_EXPECT(vh::fnv1a64(std::string_view("foobar")) ==
            0x85944171f73967e8ull);
}

void test_fnv1a_streaming_matches_oneshot() {
  std::string data = "the quick brown fox jumps over the lazy dog";
  const auto whole = vh::fnv1a64(data);
  vh::Fnv1a64 stream;
  stream.update(std::string_view(data.data(), 10));
  stream.update(std::string_view(data.data() + 10, data.size() - 10));
  VH_EXPECT(stream.value() == whole);
}

void test_fnv1a_single_bit_flip_detected() {
  std::vector<std::uint8_t> a(1024, 0x42);
  std::vector<std::uint8_t> b = a;
  b[512] ^= 1;
  const auto da = vh::fnv1a64(a.data(), a.size());
  const auto db = vh::fnv1a64(b.data(), b.size());
  VH_EXPECT(da != db);
}

}  // namespace

int main() {
  test_le16_roundtrip();
  test_le32_roundtrip();
  test_le64_roundtrip();
  test_signed_roundtrip();
  test_fnv1a_known_values();
  test_fnv1a_streaming_matches_oneshot();
  test_fnv1a_single_bit_flip_detected();
  return vh::test::summary("test_endian_digest");
}
