#pragma once

// BoundedNavigator — turns a RouteMatch + HealthSnapshot into a bounded
// yaw-rate-only NavigationCommand (M7). Every proposal passes a fixed chain
// of gates; any failure yields a zero, invalid command AND resets the slew
// memory so a later recovery starts from a standstill rather than resuming a
// stale rate. This is the deliberate fail-closed posture from the prompt:
// invalid input -> zero command.
//
// All bounding arithmetic (gain, clamp, slew) runs on integer microrad/s so
// behaviour is bit-exact and reproducible across desktop and Pi (D-007).

#include <cstdint>

#include "vh/health.hpp"
#include "vh/interfaces.hpp"
#include "vh/navigation_command.hpp"
#include "vh/route_match.hpp"

namespace vh {

struct NavigatorConfig {
  // Gate: reject matches below this confidence (mille, 0..1000).
  std::uint16_t min_confidence_mille = 600;

  // Gate: reject matches older than this. Age = health.now_ns -
  // match.timestamp_ns; a negative age (match timestamped in the future) is
  // also rejected as nonsensical.
  std::int64_t max_match_age_ns = 300'000'000;  // 300 ms

  // Proportional gain expressed in mille (gain * 1000). The yaw command is:
  //   yaw_rate_microradps = direction_error_millirad * gain_milli
  // Derivation: rate[rad/s] = error[rad] * gain[1/s]; with error in millirad
  // and output in microrad/s the 1e-3 and 1e6 factors cancel against the 1e3
  // in gain_milli, leaving an exact integer multiply. gain_milli=500 => a
  // 100-millirad (0.1 rad) error commands 50000 microrad/s (0.05 rad/s).
  std::int32_t gain_milli = 500;

  // Clamp: maximum magnitude of the commanded yaw rate (microrad/s).
  // 500000 microrad/s = 0.5 rad/s ~ 28.6 deg/s.
  std::int32_t max_yaw_rate_microradps = 500'000;

  // Slew limit: maximum change in commanded yaw rate per propose() call
  // (microrad/s per step). Bounds angular acceleration regardless of how
  // large a direction-error jump the matcher reports.
  std::int32_t max_slew_microradps = 100'000;
};

class BoundedNavigator : public INavigator {
 public:
  explicit BoundedNavigator(NavigatorConfig config = {}) noexcept;

  // Produce a bounded command. On any gate failure returns a zero invalid
  // command and resets slew memory.
  NavigationCommand propose(const RouteMatch& match,
                            const HealthSnapshot& health) override;

  // Clear slew memory (last commanded rate -> 0). Called internally on every
  // gate failure; exposed so an operator/supervisor can force a standstill
  // start without feeding an invalid match.
  void reset() noexcept;

  std::int32_t last_yaw_rate_microradps() const noexcept {
    return last_yaw_rate_microradps_;
  }
  const NavigatorConfig& config() const noexcept { return cfg_; }

 private:
  NavigatorConfig cfg_;
  std::int32_t last_yaw_rate_microradps_ = 0;
};

// ---------------------------------------------------------------------------
// Bounding kernels — exposed for direct unit testing of the integer math.
// ---------------------------------------------------------------------------

// Raw proportional rate before clamp/slew. int64 intermediate prevents
// overflow for any int32 millirad/gain pair; the caller clamps into int32.
std::int64_t yaw_rate_from_error(std::int32_t direction_error_millirad,
                                 std::int32_t gain_milli) noexcept;

// Clamp `value` into [-limit, +limit]. `limit` must be >= 0.
std::int64_t clamp_symmetric(std::int64_t value, std::int64_t limit) noexcept;

// Move `previous` toward `target` by at most `max_step` (max_step >= 0).
std::int32_t slew_limit(std::int32_t previous, std::int32_t target,
                        std::int32_t max_step) noexcept;

}  // namespace vh
