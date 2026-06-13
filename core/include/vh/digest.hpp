#pragma once

// FNV-1a 64-bit digest for accidental-corruption detection on VHRS artifacts.
// NOT a cryptographic hash — see DECISIONS.md (cryptographic trust requires
// signed metadata, planned as a future hardening, not S2 scope).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vh {

inline constexpr std::uint64_t kFnv1a64Offset = 0xcbf29ce484222325ull;
inline constexpr std::uint64_t kFnv1a64Prime = 0x100000001b3ull;

constexpr std::uint64_t fnv1a64_update(std::uint64_t seed,
                                       const std::uint8_t* data,
                                       std::size_t n) noexcept {
  for (std::size_t i = 0; i < n; ++i) {
    seed ^= static_cast<std::uint64_t>(data[i]);
    seed *= kFnv1a64Prime;
  }
  return seed;
}

constexpr std::uint64_t fnv1a64(const std::uint8_t* data,
                                std::size_t n) noexcept {
  return fnv1a64_update(kFnv1a64Offset, data, n);
}

inline std::uint64_t fnv1a64(std::string_view sv) noexcept {
  return fnv1a64(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size());
}

class Fnv1a64 {
 public:
  Fnv1a64() = default;
  void update(const std::uint8_t* data, std::size_t n) noexcept {
    state_ = fnv1a64_update(state_, data, n);
  }
  void update(std::string_view sv) noexcept {
    update(reinterpret_cast<const std::uint8_t*>(sv.data()), sv.size());
  }
  std::uint64_t value() const noexcept { return state_; }

 private:
  std::uint64_t state_ = kFnv1a64Offset;
};

}  // namespace vh
