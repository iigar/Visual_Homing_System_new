#include "vh/preprocess.hpp"

namespace vh {

BlockAveragePreprocessor::BlockAveragePreprocessor(
    std::uint32_t target_width, std::uint32_t target_height) noexcept
    : target_w_(target_width), target_h_(target_height) {}

Frame BlockAveragePreprocessor::process(const Frame& input) {
  Frame out;
  if (!input.valid()) return out;
  if (input.format != PixelFormat::Gray8) return out;
  if (target_w_ == 0 || target_h_ == 0) return out;
  if (input.width % target_w_ != 0 || input.height % target_h_ != 0) return out;

  const std::uint32_t bx = input.width / target_w_;
  const std::uint32_t by = input.height / target_h_;
  const std::uint64_t block_pixels =
      static_cast<std::uint64_t>(bx) * static_cast<std::uint64_t>(by);
  if (block_pixels == 0) return out;
  // Rounding additive: half-block, integer-only.
  const std::uint64_t half = block_pixels / 2;

  out.id = input.id;
  out.timestamp_ns = input.timestamp_ns;
  out.width = target_w_;
  out.height = target_h_;
  out.format = PixelFormat::Gray8;
  out.payload.resize(static_cast<std::size_t>(target_w_) * target_h_);

  const std::uint8_t* src = input.payload.data();
  const std::uint32_t src_w = input.width;
  std::uint8_t* dst = out.payload.data();

  for (std::uint32_t y = 0; y < target_h_; ++y) {
    for (std::uint32_t x = 0; x < target_w_; ++x) {
      std::uint64_t sum = 0;
      for (std::uint32_t dy = 0; dy < by; ++dy) {
        const std::uint32_t sy = y * by + dy;
        const std::uint8_t* row = src + static_cast<std::size_t>(sy) * src_w;
        for (std::uint32_t dx = 0; dx < bx; ++dx) {
          sum += row[x * bx + dx];
        }
      }
      const std::uint64_t avg = (sum + half) / block_pixels;
      dst[static_cast<std::size_t>(y) * target_w_ + x] =
          static_cast<std::uint8_t>(avg);
    }
  }
  return out;
}

}  // namespace vh
