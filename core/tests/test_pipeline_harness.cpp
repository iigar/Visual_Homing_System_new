// Integration test: replay source -> preprocess -> health monitor.
// Verifies the wiring of M1 + M2 end-to-end. Deterministic; no clock or
// filesystem dependency outside a per-test scratch directory.

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "vh/health.hpp"
#include "vh/manifest.hpp"
#include "vh/preprocess.hpp"
#include "vh/replay_camera.hpp"
#include "vh_test.hpp"

namespace fs = std::filesystem;

namespace {

fs::path make_scratch_dir() {
  static std::atomic<unsigned> n{0};
  const unsigned k = n.fetch_add(1, std::memory_order_relaxed);
  auto dir = fs::temp_directory_path() /
             ("vh_test_pipeline_" + std::to_string(::getpid()) + "_" +
              std::to_string(k));
  fs::create_directories(dir);
  return dir;
}

void write_pgm(const fs::path& path, unsigned w, unsigned h, std::uint8_t fill) {
  std::ofstream f(path, std::ios::binary);
  f << "P5\n" << w << ' ' << h << " 255\n";
  std::string payload(static_cast<std::size_t>(w) * h, static_cast<char>(fill));
  f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
}

void test_end_to_end_three_frames() {
  const auto dir = make_scratch_dir();
  write_pgm(dir / "a.pgm", 4, 4, 100);
  write_pgm(dir / "b.pgm", 4, 4, 150);
  write_pgm(dir / "c.pgm", 4, 4, 200);

  auto m = vh::parse_manifest(
      "0,1000,a.pgm\n"
      "1,2000,b.pgm\n"
      "2,3000,c.pgm\n",
      dir);
  VH_EXPECT(m.error == vh::ManifestError::None);

  vh::ReplayCameraSource src(m.entries);
  vh::BlockAveragePreprocessor pp(2, 2);
  vh::HealthMonitor h;
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);

  std::int64_t now = 1500;
  std::vector<std::uint8_t> centre_samples;
  while (auto f = src.next_frame()) {
    auto small = pp.process(*f);
    if (!small.valid()) {
      h.on_frame_dropped(now);
    } else {
      // "Latency" simulated as constant 1 us for determinism.
      h.on_frame_seen(small.timestamp_ns, /*latency=*/1'000, now);
      centre_samples.push_back(small.payload[0]);
    }
    h.update(now);
    now += 1'000;
  }

  VH_EXPECT(h.snapshot().frames_seen == 3);
  VH_EXPECT(h.snapshot().frames_dropped == 0);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Ready);
  VH_EXPECT(centre_samples.size() == 3);
  VH_EXPECT(centre_samples[0] == 100);
  VH_EXPECT(centre_samples[1] == 150);
  VH_EXPECT(centre_samples[2] == 200);

  fs::remove_all(dir);
}

void test_preprocess_rejection_counts_as_drop() {
  const auto dir = make_scratch_dir();
  // 5x5 PGM that the 2x2 preprocessor cannot evenly downscale.
  write_pgm(dir / "odd.pgm", 5, 5, 80);
  auto m = vh::parse_manifest("0,1000,odd.pgm\n", dir);
  VH_EXPECT(m.error == vh::ManifestError::None);

  vh::ReplayCameraSource src(m.entries);
  vh::BlockAveragePreprocessor pp(2, 2);
  vh::HealthMonitor h;
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);

  auto f = src.next_frame();
  VH_EXPECT(f.has_value());
  auto small = pp.process(*f);
  VH_EXPECT(!small.valid());
  h.on_frame_dropped(2000);
  h.update(2000);

  VH_EXPECT(h.snapshot().frames_seen == 0);
  VH_EXPECT(h.snapshot().frames_dropped == 1);
  // No frame ever seen successfully -> still Booting.
  VH_EXPECT(h.snapshot().state == vh::HealthState::Booting);

  fs::remove_all(dir);
}

void test_stale_pipeline_degrades_after_long_gap() {
  const auto dir = make_scratch_dir();
  write_pgm(dir / "x.pgm", 4, 4, 128);
  auto m = vh::parse_manifest("0,1000,x.pgm\n", dir);
  VH_EXPECT(m.error == vh::ManifestError::None);

  vh::HealthThresholds th;
  th.max_frame_age_ns = 100'000'000;  // 100 ms
  vh::HealthMonitor h(th);
  h.set_camera_ok(true);
  h.set_mavlink_ok(true);
  h.set_navigation_ok(true);

  vh::ReplayCameraSource src(m.entries);
  vh::BlockAveragePreprocessor pp(2, 2);
  auto f = src.next_frame();
  auto small = pp.process(*f);
  h.on_frame_seen(small.timestamp_ns, 1'000, /*now=*/1'500);
  h.update(1'500);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Ready);

  // No new frames for 500 ms — must degrade.
  h.update(500'001'500);
  VH_EXPECT(h.snapshot().state == vh::HealthState::Degraded);

  fs::remove_all(dir);
}

}  // namespace

int main() {
  test_end_to_end_three_frames();
  test_preprocess_rejection_counts_as_drop();
  test_stale_pipeline_degrades_after_long_gap();
  return vh::test::summary("test_pipeline_harness");
}
