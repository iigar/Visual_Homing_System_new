#include <atomic>
#include <filesystem>
#include <string>

#include "vh/route_entry.hpp"
#include "vh/route_inspect.hpp"
#include "vh/route_io.hpp"
#include "vh_test.hpp"

namespace fs = std::filesystem;

namespace {

fs::path make_scratch_dir() {
  static std::atomic<unsigned> n{0};
  const unsigned k = n.fetch_add(1, std::memory_order_relaxed);
  auto dir = fs::temp_directory_path() /
             ("vh_test_inspect_" + std::to_string(::getpid()) + "_" +
              std::to_string(k));
  fs::create_directories(dir);
  return dir;
}

vh::RouteEntry make_entry(std::uint64_t id, std::int64_t ts,
                          std::uint32_t w, std::uint32_t h,
                          std::uint8_t fill) {
  vh::RouteEntry e;
  e.frame_id = id;
  e.timestamp_ns = ts;
  e.width = w;
  e.height = h;
  e.format = vh::PixelFormat::Gray8;
  e.payload.assign(static_cast<std::size_t>(w) * h, fill);
  return e;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

void test_inspect_uniform_route() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    for (int i = 0; i < 5; ++i) {
      auto e = make_entry(static_cast<std::uint64_t>(i),
                          (i + 1) * 1000, 4, 2, static_cast<std::uint8_t>(i));
      e.altitude_band_mm = 20'000 + i * 1000;  // 20m..24m
      e.heading_millirad = -1000 + i * 500;
      w.append(e);
    }
    w.finalize();
  }
  vh::VhrsReader r(path);
  VH_EXPECT(r.error() == vh::RouteIoError::None);
  const auto report = vh::inspect(r.result());

  VH_EXPECT(report.entry_count == 5);
  VH_EXPECT(report.dimensions_seen.size() == 1);
  VH_EXPECT(report.dimensions_seen[0].first == 4);
  VH_EXPECT(report.dimensions_seen[0].second == 2);
  VH_EXPECT(report.total_payload_bytes == 5 * 8);
  VH_EXPECT(report.timestamps_monotonic);
  VH_EXPECT(report.any_known_altitude);
  VH_EXPECT(report.altitude_min_mm == 20'000);
  VH_EXPECT(report.altitude_max_mm == 24'000);
  VH_EXPECT(report.any_known_heading);
  VH_EXPECT(report.heading_min_millirad == -1000);
  VH_EXPECT(report.heading_max_millirad == 1000);

  const std::string txt = vh::format_report(report);
  VH_EXPECT(contains(txt, "entry_count=5"));
  VH_EXPECT(contains(txt, "dimension=4x2"));
  VH_EXPECT(contains(txt, "timestamps_monotonic=true"));
  VH_EXPECT(contains(txt, "altitude_min_mm=20000"));
  VH_EXPECT(contains(txt, "file_digest_fnv1a64=0x"));
  fs::remove_all(dir);
}

void test_inspect_unknown_metadata() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::VhrsWriter w(path);
    w.append(make_entry(0, 1, 4, 2, 0));  // defaults to Unknown for alt/heading
    w.finalize();
  }
  vh::VhrsReader r(path);
  const auto report = vh::inspect(r.result());
  VH_EXPECT(!report.any_known_altitude);
  VH_EXPECT(!report.any_known_heading);
  const std::string txt = vh::format_report(report);
  VH_EXPECT(contains(txt, "altitude_known=false"));
  VH_EXPECT(contains(txt, "heading_known=false"));
  fs::remove_all(dir);
}

}  // namespace

int main() {
  test_inspect_uniform_route();
  test_inspect_unknown_metadata();
  return vh::test::summary("test_route_inspect");
}
