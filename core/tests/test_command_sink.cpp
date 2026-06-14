#include <cstdint>

#include "vh/command_sink.hpp"
#include "vh/navigation_command.hpp"
#include "vh_test.hpp"

namespace {

vh::NavigationCommand cmd(std::int64_t ts, std::int32_t yaw, bool valid,
                          std::uint16_t conf = 800) {
  vh::NavigationCommand c;
  c.timestamp_ns = ts;
  c.yaw_rate_microradps = yaw;
  c.confidence_mille = conf;
  c.valid = valid;
  return c;
}

void test_stopped_by_default_rejects() {
  vh::DryRunCommandSink sink;
  VH_EXPECT(!sink.started());
  VH_EXPECT(!sink.send(cmd(1, 100, true)));  // refused while stopped
  VH_EXPECT(sink.history_size() == 0);
  VH_EXPECT(sink.counters().rejected_stopped == 1);
  VH_EXPECT(sink.counters().accepted == 0);
}

void test_single_writer() {
  vh::DryRunCommandSink sink;
  VH_EXPECT(sink.start());        // opens
  VH_EXPECT(!sink.start());       // second writer refused
  sink.stop();
  VH_EXPECT(!sink.started());
  VH_EXPECT(sink.start());        // can reopen after stop
}

void test_send_records_and_counts() {
  vh::DryRunCommandSink sink;
  sink.start();
  VH_EXPECT(sink.send(cmd(10, 50, true)));
  VH_EXPECT(sink.send(cmd(20, 0, false)));  // invalid still recorded
  VH_EXPECT(sink.history_size() == 2);
  VH_EXPECT(sink.counters().accepted == 2);
  VH_EXPECT(sink.counters().accepted_valid == 1);
  VH_EXPECT(sink.latest().timestamp_ns == 20);
  VH_EXPECT(sink.latest().valid == false);
  VH_EXPECT(sink.history_at(0).timestamp_ns == 10);  // oldest
  VH_EXPECT(sink.history_at(0).seq == 0);
  VH_EXPECT(sink.history_at(1).seq == 1);
}

void test_bounded_history_drops_oldest() {
  vh::DryRunCommandSink sink(3);  // capacity 3
  sink.start();
  for (int i = 0; i < 5; ++i) sink.send(cmd(i, i * 10, true));
  // Only the last 3 retained; total accepted counts all 5.
  VH_EXPECT(sink.history_size() == 3);
  VH_EXPECT(sink.counters().accepted == 5);
  VH_EXPECT(sink.history_at(0).timestamp_ns == 2);  // oldest retained
  VH_EXPECT(sink.history_at(2).timestamp_ns == 4);  // newest
  VH_EXPECT(sink.latest().seq == 4);                // all-time seq preserved
}

void test_stop_then_send_rejected() {
  vh::DryRunCommandSink sink;
  sink.start();
  sink.send(cmd(1, 10, true));
  sink.stop();
  VH_EXPECT(!sink.send(cmd(2, 20, true)));
  VH_EXPECT(sink.history_size() == 1);
  VH_EXPECT(sink.counters().rejected_stopped == 1);
}

}  // namespace

int main() {
  test_stopped_by_default_rejects();
  test_single_writer();
  test_send_records_and_counts();
  test_bounded_history_drops_oldest();
  test_stop_then_send_rejected();
  return ::vh::test::summary("test_command_sink");
}
