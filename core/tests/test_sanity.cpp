#include "vh/frame.hpp"
#include "vh/interfaces.hpp"
#include "vh_test.hpp"

namespace {

void test_frame_validity() {
  vh::Frame f;
  VH_EXPECT(!f.valid());  // zero-size frame is invalid

  f.width = 4;
  f.height = 2;
  f.format = vh::PixelFormat::Gray8;
  f.payload.assign(8, 0);
  VH_EXPECT(f.valid());

  f.payload.resize(7);  // payload mismatch must be invalid
  VH_EXPECT(!f.valid());

  f.format = vh::PixelFormat::Thermal16;
  f.payload.assign(16, 0);  // 4*2*2 bytes
  VH_EXPECT(f.valid());
}

}  // namespace

int main() {
  test_frame_validity();
  return vh::test::summary("test_sanity");
}
