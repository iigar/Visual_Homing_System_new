#include "vh/preprocess.hpp"

#include <vector>

#include "vh/frame.hpp"
#include "vh_test.hpp"

namespace {

vh::Frame make_gray8(unsigned w, unsigned h, std::vector<std::uint8_t> bytes) {
  vh::Frame f;
  f.width = w;
  f.height = h;
  f.format = vh::PixelFormat::Gray8;
  f.payload = std::move(bytes);
  return f;
}

void test_uniform_block_average() {
  // 4x4 of value 100 -> 2x2 of value 100.
  auto in = make_gray8(4, 4, std::vector<std::uint8_t>(16, 100));
  vh::BlockAveragePreprocessor p(2, 2);
  auto out = p.process(in);
  VH_EXPECT(out.valid());
  VH_EXPECT(out.width == 2 && out.height == 2);
  for (auto b : out.payload) VH_EXPECT(b == 100);
}

void test_block_average_two_values() {
  // 4x4 split: top half=200, bottom half=100 -> 2x2 = {200,200,100,100}.
  std::vector<std::uint8_t> px;
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x) px.push_back(y < 2 ? 200 : 100);
  auto in = make_gray8(4, 4, std::move(px));
  vh::BlockAveragePreprocessor p(2, 2);
  auto out = p.process(in);
  VH_EXPECT(out.valid());
  VH_EXPECT(out.payload[0] == 200);  // top-left
  VH_EXPECT(out.payload[1] == 200);  // top-right
  VH_EXPECT(out.payload[2] == 100);  // bottom-left
  VH_EXPECT(out.payload[3] == 100);  // bottom-right
}

void test_id_and_timestamp_propagate() {
  auto in = make_gray8(2, 2, std::vector<std::uint8_t>(4, 50));
  in.id = 42;
  in.timestamp_ns = 7777;
  vh::BlockAveragePreprocessor p(1, 1);
  auto out = p.process(in);
  VH_EXPECT(out.id == 42);
  VH_EXPECT(out.timestamp_ns == 7777);
  VH_EXPECT(out.payload[0] == 50);
}

void test_non_divisible_rejected() {
  auto in = make_gray8(5, 4, std::vector<std::uint8_t>(20, 0));
  vh::BlockAveragePreprocessor p(2, 2);  // 5 % 2 != 0
  auto out = p.process(in);
  VH_EXPECT(!out.valid());
}

void test_invalid_input_rejected() {
  vh::Frame in;  // zero-size, invalid
  vh::BlockAveragePreprocessor p(2, 2);
  auto out = p.process(in);
  VH_EXPECT(!out.valid());
}

void test_wrong_format_rejected() {
  auto in = make_gray8(2, 2, std::vector<std::uint8_t>(4, 0));
  in.format = vh::PixelFormat::Thermal16;
  in.payload.assign(8, 0);  // 2*2*2 bytes — Frame::valid() ok
  vh::BlockAveragePreprocessor p(1, 1);
  auto out = p.process(in);
  VH_EXPECT(!out.valid());
}

void test_rounding_half_block() {
  // 2x1 of {0,1} block-averaged into 1x1: (0+1+1)/2 = 1 with half-rounding.
  auto in = make_gray8(2, 1, {0, 1});
  vh::BlockAveragePreprocessor p(1, 1);
  auto out = p.process(in);
  VH_EXPECT(out.valid());
  // (0+1+half(1))/2 = (0+1+1)/2 = 1
  VH_EXPECT(out.payload[0] == 1);
}

void test_identity_when_dimensions_match() {
  auto in = make_gray8(3, 3, {0, 1, 2, 3, 4, 5, 6, 7, 8});
  vh::BlockAveragePreprocessor p(3, 3);
  auto out = p.process(in);
  VH_EXPECT(out.valid());
  for (std::size_t i = 0; i < out.payload.size(); ++i) {
    VH_EXPECT(out.payload[i] == in.payload[i]);
  }
}

}  // namespace

int main() {
  test_uniform_block_average();
  test_block_average_two_values();
  test_id_and_timestamp_propagate();
  test_non_divisible_rejected();
  test_invalid_input_rejected();
  test_wrong_format_rejected();
  test_rounding_half_block();
  test_identity_when_dimensions_match();
  return vh::test::summary("test_preprocess");
}
