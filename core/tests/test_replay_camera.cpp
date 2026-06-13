#include "vh/replay_camera.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "vh/manifest.hpp"
#include "vh_test.hpp"

namespace fs = std::filesystem;

namespace {

fs::path make_scratch_dir() {
  // Deterministic-ish per-test directory under the OS temp area.
  static std::atomic<unsigned> n{0};
  const unsigned k = n.fetch_add(1, std::memory_order_relaxed);
  auto dir = fs::temp_directory_path() /
             ("vh_test_replay_" + std::to_string(::getpid()) + "_" +
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

void test_full_replay() {
  const auto dir = make_scratch_dir();
  write_pgm(dir / "a.pgm", 4, 2, 0x10);
  write_pgm(dir / "b.pgm", 4, 2, 0x20);
  write_pgm(dir / "c.pgm", 4, 2, 0x30);

  std::ostringstream csv;
  csv << "id,timestamp_ns,path\n"
      << "0,1000,a.pgm\n"
      << "1,2000,b.pgm\n"
      << "2,3000,c.pgm\n";
  const auto m = vh::parse_manifest(csv.str(), dir);
  VH_EXPECT(m.error == vh::ManifestError::None);

  vh::ReplayCameraSource src(m.entries);
  VH_EXPECT(src.total() == 3);

  auto f0 = src.next_frame();
  VH_EXPECT(f0.has_value());
  VH_EXPECT(f0->id == 0);
  VH_EXPECT(f0->payload[0] == 0x10);

  auto f1 = src.next_frame();
  VH_EXPECT(f1.has_value() && f1->payload[0] == 0x20);

  auto f2 = src.next_frame();
  VH_EXPECT(f2.has_value() && f2->payload[0] == 0x30);

  auto f3 = src.next_frame();
  VH_EXPECT(!f3.has_value());  // exhausted
  VH_EXPECT(src.last_error() == vh::ReplayError::None);

  fs::remove_all(dir);
}

void test_missing_file_reports_pgm_failed() {
  const auto dir = make_scratch_dir();
  // Manifest references a file that does not exist.
  const auto m = vh::parse_manifest("0,1000,nope.pgm\n", dir);
  VH_EXPECT(m.error == vh::ManifestError::None);

  vh::ReplayCameraSource src(m.entries);
  auto f = src.next_frame();
  VH_EXPECT(!f.has_value());
  VH_EXPECT(src.last_error() == vh::ReplayError::PgmFailed);
  VH_EXPECT(!src.last_error_detail().empty());

  fs::remove_all(dir);
}

void test_empty_source_returns_nullopt_immediately() {
  std::vector<vh::ManifestEntry> empty;
  vh::ReplayCameraSource src(empty);
  VH_EXPECT(!src.next_frame().has_value());
  VH_EXPECT(src.total() == 0);
}

}  // namespace

int main() {
  test_full_replay();
  test_missing_file_reports_pgm_failed();
  test_empty_source_returns_nullopt_immediately();
  return vh::test::summary("test_replay_camera");
}
