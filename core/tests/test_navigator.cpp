#include <cmath>
#include <cstdint>
#include <limits>

#include "vh/health.hpp"
#include "vh/navigation_command.hpp"
#include "vh/navigator.hpp"
#include "vh/route_match.hpp"
#include "vh_test.hpp"

namespace {

// A HealthSnapshot that passes every navigator gate. Tests flip one field at a
// time to exercise a single failing gate in isolation.
vh::HealthSnapshot healthy_snapshot(std::int64_t now_ns = 1'000'000'000) {
  vh::HealthSnapshot h;
  h.state = vh::HealthState::Ready;
  h.camera_ok = true;
  h.mavlink_ok = true;
  h.navigation_ok = true;
  h.now_ns = now_ns;
  h.route_match_confidence = 0.8f;
  return h;
}

// A RouteMatch that passes every navigator gate. timestamp set so age == 0.
vh::RouteMatch good_match(std::int64_t now_ns = 1'000'000'000,
                          std::int32_t err_millirad = 100) {
  vh::RouteMatch m;
  m.valid = true;
  m.timestamp_ns = now_ns;
  m.confidence_mille = 800;
  m.confidence = 0.8f;
  m.direction_error_millirad = err_millirad;
  return m;
}

// -------------------------------------------------------------------------
// Kernel-level math
// -------------------------------------------------------------------------
void test_kernels() {
  // yaw_rate_from_error: exact integer multiply, no overflow at extremes.
  VH_EXPECT(vh::yaw_rate_from_error(100, 500) == 50'000);
  VH_EXPECT(vh::yaw_rate_from_error(-100, 500) == -50'000);
  VH_EXPECT(vh::yaw_rate_from_error(0, 500) == 0);
  // 2^31-ish * 1000 would overflow int32 but not the int64 return.
  const std::int64_t big =
      vh::yaw_rate_from_error(2'000'000'000, 1000);
  VH_EXPECT(big == std::int64_t{2'000'000'000} * 1000);

  // clamp_symmetric
  VH_EXPECT(vh::clamp_symmetric(50, 100) == 50);
  VH_EXPECT(vh::clamp_symmetric(150, 100) == 100);
  VH_EXPECT(vh::clamp_symmetric(-150, 100) == -100);
  VH_EXPECT(vh::clamp_symmetric(7, 0) == 0);     // zero limit pins to zero
  VH_EXPECT(vh::clamp_symmetric(7, -5) == 0);    // negative limit treated as 0

  // slew_limit
  VH_EXPECT(vh::slew_limit(0, 1000, 100) == 100);    // step up capped
  VH_EXPECT(vh::slew_limit(0, -1000, 100) == -100);  // step down capped
  VH_EXPECT(vh::slew_limit(0, 40, 100) == 40);       // small move passes
  VH_EXPECT(vh::slew_limit(500, 500, 100) == 500);   // no move
}

// -------------------------------------------------------------------------
// Happy path: all gates pass, yaw rate from gain, vx/vy pinned to zero
// -------------------------------------------------------------------------
void test_happy_path() {
  vh::NavigatorConfig cfg;  // defaults: gain 500, slew 100000, clamp 500000
  vh::BoundedNavigator nav(cfg);

  // error 100 millirad * gain 500 = 50000 microradps target; slew cap 100000
  // so it lands on target in one step.
  auto cmd = nav.propose(good_match(), healthy_snapshot());
  VH_EXPECT(cmd.valid);
  VH_EXPECT(cmd.yaw_rate_microradps == 50'000);
  VH_EXPECT(cmd.vx_mps == 0.0f);
  VH_EXPECT(cmd.vy_mps == 0.0f);
  VH_EXPECT(cmd.confidence_mille == 800);
  // float projection: 50000 / 1e6 = 0.05 rad/s
  VH_EXPECT(std::fabs(cmd.yaw_rate_radps - 0.05f) < 1e-6f);
}

// -------------------------------------------------------------------------
// Zero-forward-speed policy holds even with a large direction error
// -------------------------------------------------------------------------
void test_zero_forward_speed_policy() {
  vh::BoundedNavigator nav;
  auto cmd = nav.propose(good_match(1'000'000'000, 100'000), healthy_snapshot());
  VH_EXPECT(cmd.valid);
  VH_EXPECT(cmd.vx_mps == 0.0f);
  VH_EXPECT(cmd.vy_mps == 0.0f);
}

// -------------------------------------------------------------------------
// Gate: low confidence -> zero invalid
// -------------------------------------------------------------------------
void test_low_confidence() {
  vh::NavigatorConfig cfg;
  cfg.min_confidence_mille = 600;
  vh::BoundedNavigator nav(cfg);

  auto m = good_match();
  m.confidence_mille = 500;  // below threshold
  auto cmd = nav.propose(m, healthy_snapshot());
  VH_EXPECT(!cmd.valid);
  VH_EXPECT(cmd.yaw_rate_microradps == 0);
}

// -------------------------------------------------------------------------
// Gate: stale match -> zero invalid; also future-timestamp (negative age)
// -------------------------------------------------------------------------
void test_stale_match() {
  vh::NavigatorConfig cfg;
  cfg.max_match_age_ns = 300'000'000;  // 300 ms
  vh::BoundedNavigator nav(cfg);

  const std::int64_t now = 2'000'000'000;
  auto h = healthy_snapshot(now);

  // 400 ms old -> too stale.
  auto stale = good_match(now - 400'000'000);
  VH_EXPECT(!nav.propose(stale, h).valid);

  // Future-stamped match (negative age) -> rejected as nonsensical.
  auto future = good_match(now + 10'000'000);
  VH_EXPECT(!nav.propose(future, h).valid);

  // Exactly at the age limit -> still accepted (inclusive bound).
  auto edge = good_match(now - 300'000'000);
  VH_EXPECT(nav.propose(edge, h).valid);
}

// -------------------------------------------------------------------------
// Gate: invalid match -> zero invalid
// -------------------------------------------------------------------------
void test_invalid_match() {
  vh::BoundedNavigator nav;
  auto m = good_match();
  m.valid = false;
  VH_EXPECT(!nav.propose(m, healthy_snapshot()).valid);
}

// -------------------------------------------------------------------------
// Gate: degraded / non-ready health and dropped stage flags
// -------------------------------------------------------------------------
void test_degraded_health() {
  vh::BoundedNavigator nav;

  // Non-Ready state.
  {
    auto h = healthy_snapshot();
    h.state = vh::HealthState::Degraded;
    VH_EXPECT(!nav.propose(good_match(), h).valid);
  }
  // Booting is not Ready either.
  {
    auto h = healthy_snapshot();
    h.state = vh::HealthState::Booting;
    VH_EXPECT(!nav.propose(good_match(), h).valid);
  }
  // Each stage flag individually gates.
  for (int which = 0; which < 3; ++which) {
    auto h = healthy_snapshot();  // state stays Ready
    if (which == 0) h.camera_ok = false;
    if (which == 1) h.mavlink_ok = false;
    if (which == 2) h.navigation_ok = false;
    VH_EXPECT(!nav.propose(good_match(), h).valid);
  }
}

// -------------------------------------------------------------------------
// Gate: non-finite float inputs -> zero invalid
// -------------------------------------------------------------------------
void test_non_finite() {
  vh::BoundedNavigator nav;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  {
    auto m = good_match();
    m.confidence = nan;
    VH_EXPECT(!nav.propose(m, healthy_snapshot()).valid);
  }
  {
    auto m = good_match();
    m.progress = inf;
    VH_EXPECT(!nav.propose(m, healthy_snapshot()).valid);
  }
  {
    auto h = healthy_snapshot();
    h.route_match_confidence = nan;
    VH_EXPECT(!nav.propose(good_match(), h).valid);
  }
}

// -------------------------------------------------------------------------
// Clamp: enormous error is bounded to max_yaw_rate before slew
// -------------------------------------------------------------------------
void test_clamp() {
  vh::NavigatorConfig cfg;
  cfg.gain_milli = 1000;
  cfg.max_yaw_rate_microradps = 200'000;
  cfg.max_slew_microradps = 1'000'000;  // large, so clamp (not slew) bounds it
  vh::BoundedNavigator nav(cfg);

  // error 100000 millirad * gain 1000 = 100,000,000 -> clamp to 200000.
  auto cmd = nav.propose(good_match(1'000'000'000, 100'000), healthy_snapshot());
  VH_EXPECT(cmd.valid);
  VH_EXPECT(cmd.yaw_rate_microradps == 200'000);

  // Negative error clamps to the negative bound.
  nav.reset();
  auto cmd2 = nav.propose(good_match(1'000'000'000, -100'000), healthy_snapshot());
  VH_EXPECT(cmd2.yaw_rate_microradps == -200'000);
}

// -------------------------------------------------------------------------
// Slew limit: rate ramps over successive calls, never jumps
// -------------------------------------------------------------------------
void test_slew_limit() {
  vh::NavigatorConfig cfg;
  cfg.gain_milli = 1000;
  cfg.max_yaw_rate_microradps = 1'000'000;
  cfg.max_slew_microradps = 30'000;  // 30000 microradps per step
  vh::BoundedNavigator nav(cfg);

  auto h = healthy_snapshot();
  // target = 100 * 1000 = 100000, far above the 30000 slew step.
  auto c1 = nav.propose(good_match(1'000'000'000, 100), h);
  VH_EXPECT(c1.yaw_rate_microradps == 30'000);
  auto c2 = nav.propose(good_match(1'000'000'000, 100), h);
  VH_EXPECT(c2.yaw_rate_microradps == 60'000);
  auto c3 = nav.propose(good_match(1'000'000'000, 100), h);
  VH_EXPECT(c3.yaw_rate_microradps == 90'000);
  auto c4 = nav.propose(good_match(1'000'000'000, 100), h);
  VH_EXPECT(c4.yaw_rate_microradps == 100'000);  // reaches target, holds
  auto c5 = nav.propose(good_match(1'000'000'000, 100), h);
  VH_EXPECT(c5.yaw_rate_microradps == 100'000);
}

// -------------------------------------------------------------------------
// Reset after invalid state: a gate failure zeroes slew memory, so recovery
// starts from a standstill rather than resuming the previous rate
// -------------------------------------------------------------------------
void test_reset_after_invalid() {
  vh::NavigatorConfig cfg;
  cfg.gain_milli = 1000;
  cfg.max_yaw_rate_microradps = 1'000'000;
  cfg.max_slew_microradps = 30'000;
  vh::BoundedNavigator nav(cfg);

  auto h = healthy_snapshot();
  nav.propose(good_match(1'000'000'000, 100), h);  // -> 30000
  nav.propose(good_match(1'000'000'000, 100), h);  // -> 60000
  VH_EXPECT(nav.last_yaw_rate_microradps() == 60'000);

  // One invalid match: must zero internal memory.
  auto bad = good_match(1'000'000'000, 100);
  bad.valid = false;
  auto blocked = nav.propose(bad, h);
  VH_EXPECT(!blocked.valid);
  VH_EXPECT(nav.last_yaw_rate_microradps() == 0);

  // Recovery starts from 0 again, not from 60000.
  auto recovered = nav.propose(good_match(1'000'000'000, 100), h);
  VH_EXPECT(recovered.yaw_rate_microradps == 30'000);

  // Explicit reset() works the same way.
  nav.reset();
  VH_EXPECT(nav.last_yaw_rate_microradps() == 0);
}

}  // namespace

int main() {
  test_kernels();
  test_happy_path();
  test_zero_forward_speed_policy();
  test_low_confidence();
  test_stale_match();
  test_invalid_match();
  test_degraded_health();
  test_non_finite();
  test_clamp();
  test_slew_limit();
  test_reset_after_invalid();
  return ::vh::test::summary("test_navigator");
}
