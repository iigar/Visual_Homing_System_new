#pragma once

// Deterministic Gray8 block-average resize. Input width/height must be exact
// integer multiples of the target — non-integer ratios fail closed (returns
// an invalid Frame). Floating-point math is avoided to keep results bit-exact
// across desktop and Pi.

#include <cstdint>

#include "vh/frame.hpp"
#include "vh/interfaces.hpp"

namespace vh {

class BlockAveragePreprocessor : public IPreprocessor {
 public:
  BlockAveragePreprocessor(std::uint32_t target_width,
                           std::uint32_t target_height) noexcept;

  // Returns a Gray8 Frame at the target dimensions, copying timestamp/id.
  // On failure (wrong format, non-divisible dimensions, invalid input) the
  // returned Frame has width=height=0 and an empty payload — Frame::valid()
  // returns false.
  Frame process(const Frame& input) override;

  std::uint32_t target_width() const noexcept { return target_w_; }
  std::uint32_t target_height() const noexcept { return target_h_; }

 private:
  std::uint32_t target_w_;
  std::uint32_t target_h_;
};

}  // namespace vh
