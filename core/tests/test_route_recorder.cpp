#include <atomic>
#include <filesystem>

#include "vh/frame.hpp"
#include "vh/route_io.hpp"
#include "vh/route_recorder.hpp"
#include "vh_test.hpp"

namespace fs = std::filesystem;

namespace {

fs::path make_scratch_dir() {
  static std::atomic<unsigned> n{0};
  const unsigned k = n.fetch_add(1, std::memory_order_relaxed);
  auto dir = fs::temp_directory_path() /
             ("vh_test_recorder_" + std::to_string(::getpid()) + "_" +
              std::to_string(k));
  fs::create_directories(dir);
  return dir;
}

vh::Frame make_frame(std::uint64_t id, std::int64_t ts, std::uint32_t w,
                     std::uint32_t h, std::uint8_t fill) {
  vh::Frame f;
  f.id = id;
  f.timestamp_ns = ts;
  f.width = w;
  f.height = h;
  f.format = vh::PixelFormat::Gray8;
  f.payload.assign(static_cast<std::size_t>(w) * h, fill);
  return f;
}

void test_record_three_frames_round_trip() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::RouteSignatureRecorder rec(path);
    vh::PoseHint pose;
    pose.altitude_band_mm = 25'000;
    pose.heading_millirad = 1500;
    VH_EXPECT(rec.record(make_frame(0, 1000, 4, 2, 0x11), pose));
    VH_EXPECT(rec.record(make_frame(1, 2000, 4, 2, 0x22), pose));
    VH_EXPECT(rec.record(make_frame(2, 3000, 4, 2, 0x33), pose));
    VH_EXPECT(rec.appended() == 3);
    VH_EXPECT(rec.rejected() == 0);
    VH_EXPECT(rec.finalize());
  }

  vh::VhrsReader r(path);
  VH_EXPECT(r.error() == vh::RouteIoError::None);
  VH_EXPECT(r.entry_count() == 3);
  auto e1 = r.entry(1);
  VH_EXPECT(e1 && e1->frame_id == 1);
  VH_EXPECT(e1->altitude_band_mm == 25'000);
  VH_EXPECT(e1->heading_millirad == 1500);
  VH_EXPECT(e1->payload[0] == 0x22);
  fs::remove_all(dir);
}

void test_invalid_frame_is_rejected() {
  const auto dir = make_scratch_dir();
  vh::RouteSignatureRecorder rec(dir / "r.vhrs");
  vh::Frame f;  // zero-size, invalid
  VH_EXPECT(!rec.record(f, vh::PoseHint{}));
  VH_EXPECT(rec.appended() == 0);
  VH_EXPECT(rec.rejected() == 1);
  VH_EXPECT(rec.last_error() == vh::RouteIoError::EntryInvalid);
  fs::remove_all(dir);
}

void test_record_after_finalize_refused() {
  const auto dir = make_scratch_dir();
  vh::RouteSignatureRecorder rec(dir / "r.vhrs");
  rec.record(make_frame(0, 1, 4, 2, 0x01), vh::PoseHint{});
  rec.finalize();
  VH_EXPECT(!rec.record(make_frame(1, 2, 4, 2, 0x02), vh::PoseHint{}));
  VH_EXPECT(rec.last_error() == vh::RouteIoError::AlreadyFinalized);
  fs::remove_all(dir);
}

void test_unknown_pose_round_trip() {
  const auto dir = make_scratch_dir();
  const auto path = dir / "r.vhrs";
  {
    vh::RouteSignatureRecorder rec(path);
    VH_EXPECT(rec.record(make_frame(0, 1000, 2, 2, 0x55), vh::PoseHint{}));
    rec.finalize();
  }
  vh::VhrsReader r(path);
  auto e = r.entry(0);
  VH_EXPECT(e);
  VH_EXPECT(e->altitude_band_mm == vh::RouteEntry::kAltitudeUnknown);
  VH_EXPECT(e->heading_millirad == vh::RouteEntry::kHeadingUnknown);
  fs::remove_all(dir);
}

}  // namespace

int main() {
  test_record_three_frames_round_trip();
  test_invalid_frame_is_rejected();
  test_record_after_finalize_refused();
  test_unknown_pose_round_trip();
  return vh::test::summary("test_route_recorder");
}
