#include "vh/health.hpp"

#include "vh_test.hpp"

namespace {

void test_initial_state_is_booting() {
  vh::HealthMonitor h;
  VH_EXPECT(h.snapshot().state == vh::HealthState::Booting);
  VH_EXPECT(h.snapshot().frames_seen == 0);
}

void test_booting_to_ready_requires_all_signals() {
  vh::HealthMonitor h;
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);
  h.on_frame_seen(/*frame=*/1000, /*latency=*/1'000'000, /*now=*/1'001'000);
  h.update(1'001'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Ready);
}

void test_stays_in_booting_if_no_frame_yet() {
  vh::HealthMonitor h;
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);
  h.update(1'000'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Booting);
}

void test_ready_to_degraded_on_subsystem_loss() {
  vh::HealthMonitor h;
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);
  h.on_frame_seen(1000, 1'000'000, 1'001'000);
  h.update(1'001'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Ready);

  h.set_mavlink_ok(false);
  h.update(1'002'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Degraded);

  h.set_mavlink_ok(true);
  h.update(1'003'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Ready);
}

void test_stale_frame_degrades_health() {
  vh::HealthThresholds th;
  th.max_frame_age_ns = 100'000'000;  // 100 ms
  vh::HealthMonitor h(th);
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);
  h.on_frame_seen(1'000, 1'000'000, 1'001'000);
  h.update(1'001'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Ready);

  // No new frame for 200 ms — must degrade.
  h.update(201'001'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Degraded);
  VH_EXPECT(h.snapshot().frame_age_ns > th.max_frame_age_ns);
}

void test_slow_processing_degrades_health() {
  vh::HealthThresholds th;
  th.max_processing_latency_ns = 100'000'000;  // 100 ms
  vh::HealthMonitor h(th);
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);
  h.on_frame_seen(1'000, /*latency=*/200'000'000, /*now=*/1'001'000);
  h.update(1'001'000);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Degraded);
}

void test_failsafe_sticky_then_shutdown() {
  vh::HealthMonitor h;
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);
  h.on_frame_seen(1, 1, 2);
  h.update(2);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Ready);

  h.request_failsafe();
  h.update(3);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Failsafe);

  // Clearing the bad signals later does NOT exit Failsafe.
  h.update(4);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Failsafe);

  h.request_shutdown();
  h.update(5);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Shutdown);

  // Shutdown is terminal.
  h.update(6);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Shutdown);
}

void test_dropped_frame_counter() {
  vh::HealthMonitor h;
  h.on_frame_dropped(1000);
  h.on_frame_dropped(2000);
  VH_EXPECT(h.snapshot().frames_dropped == 2);
  VH_EXPECT(h.snapshot().frames_seen == 0);
}

void test_route_confidence_clamped() {
  vh::HealthMonitor h;
  h.set_route_confidence(-0.5f);
  VH_EXPECT(h.snapshot().route_match_confidence == 0.0f);
  h.set_route_confidence(1.5f);
  VH_EXPECT(h.snapshot().route_match_confidence == 1.0f);
  h.set_route_confidence(0.42f);
  VH_EXPECT(h.snapshot().route_match_confidence == 0.42f);
}

}  // namespace

int main() {
  test_initial_state_is_booting();
  test_booting_to_ready_requires_all_signals();
  test_stays_in_booting_if_no_frame_yet();
  test_ready_to_degraded_on_subsystem_loss();
  test_stale_frame_degrades_health();
  test_slow_processing_degrades_health();
  test_failsafe_sticky_then_shutdown();
  test_dropped_frame_counter();
  test_route_confidence_clamped();
  return vh::test::summary("test_health");
}
