#pragma once

// NavigationCommand — output of INavigator (M7). The only command shape this
// project produces during the dry-run buildout: yaw-rate-only. vx_mps and
// vy_mps are structurally present (the downstream MAVLink encoder, M17, fills
// a SET_POSITION_TARGET_LOCAL_NED body-frame message) but are hard-wired to
// zero here — no forward/lateral velocity authority exists before review.
//
// Following D-014, the authoritative quantities are integer (yaw_rate is
// produced by integer-exact arithmetic from direction_error_millirad); the
// float fields are display projections for downstream consumers that prefer
// SI units. See DECISIONS.md D-017.

#include <cstdint>

namespace vh {

struct NavigationCommand {
  std::int64_t timestamp_ns = 0;

  // Forward/lateral velocity — always zero in the yaw-rate-only scope. Present
  // for the command contract; never assigned a non-zero value by M7.
  float vx_mps = 0.0f;
  float vy_mps = 0.0f;

  // Yaw rate. Integer microrad/s is authoritative (bit-exact, clamp/slew all
  // operate on it); the float rad/s is microradps / 1e6.
  std::int32_t yaw_rate_microradps = 0;
  float yaw_rate_radps = 0.0f;

  // Confidence carried through from the originating RouteMatch.
  std::uint16_t confidence_mille = 0;
  float confidence = 0.0f;

  // false => fail-closed zero command (a gate rejected the proposal). A valid
  // command may still carry yaw_rate_microradps == 0 (e.g. zero direction
  // error); callers must check `valid`, not the rate, to detect a gate block.
  bool valid = false;

  static constexpr std::int32_t kMicroPerRad = 1'000'000;
};

}  // namespace vh
