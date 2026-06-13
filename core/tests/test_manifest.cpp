#include "vh/manifest.hpp"
#include "vh_test.hpp"

namespace {

void test_basic_three_rows() {
  const auto r = vh::parse_manifest(
      "id,timestamp_ns,path\n"
      "0,1000,frame_000.pgm\n"
      "1,2000,frame_001.pgm\n"
      "2,3000,frame_002.pgm\n");
  VH_EXPECT(r.error == vh::ManifestError::None);
  VH_EXPECT(r.entries.size() == 3);
  VH_EXPECT(r.entries[0].id == 0);
  VH_EXPECT(r.entries[0].timestamp_ns == 1000);
  VH_EXPECT(r.entries[2].path == "frame_002.pgm");
}

void test_comments_and_blank_lines() {
  const auto r = vh::parse_manifest(
      "# comment line\n"
      "\n"
      "0,100,a.pgm\n"
      "# another comment\n"
      "1,200,b.pgm\n");
  VH_EXPECT(r.error == vh::ManifestError::None);
  VH_EXPECT(r.entries.size() == 2);
}

void test_no_header_pure_data() {
  const auto r = vh::parse_manifest("5,500,x.pgm\n6,600,y.pgm\n");
  VH_EXPECT(r.error == vh::ManifestError::None);
  VH_EXPECT(r.entries.size() == 2);
  VH_EXPECT(r.entries[0].id == 5);
}

void test_missing_field_rejected() {
  const auto r = vh::parse_manifest("0,1000\n");
  VH_EXPECT(r.error == vh::ManifestError::MalformedRow);
  VH_EXPECT(r.entries.empty());
}

void test_non_numeric_id_rejected() {
  const auto r = vh::parse_manifest("0,1000,a.pgm\nXYZ,2000,b.pgm\n");
  VH_EXPECT(r.error == vh::ManifestError::MalformedRow);
  VH_EXPECT(r.error_line == 2);
}

void test_negative_timestamp_rejected() {
  const auto r = vh::parse_manifest("0,-100,a.pgm\n");
  VH_EXPECT(r.error == vh::ManifestError::NegativeTimestamp);
}

void test_non_monotonic_timestamp_rejected() {
  const auto r = vh::parse_manifest(
      "0,1000,a.pgm\n"
      "1,1000,b.pgm\n");  // equal — strict monotonic required
  VH_EXPECT(r.error == vh::ManifestError::NonMonotonicTimestamp);
}

void test_duplicate_id_rejected() {
  const auto r = vh::parse_manifest(
      "0,1000,a.pgm\n"
      "0,2000,b.pgm\n");
  VH_EXPECT(r.error == vh::ManifestError::DuplicateId);
}

void test_empty_path_rejected() {
  const auto r = vh::parse_manifest("0,1000,\n");
  VH_EXPECT(r.error == vh::ManifestError::EmptyPath);
}

void test_empty_file_rejected() {
  const auto r = vh::parse_manifest("");
  VH_EXPECT(r.error == vh::ManifestError::EmptyFile);
  const auto r2 = vh::parse_manifest("# only comments\n\n");
  VH_EXPECT(r2.error == vh::ManifestError::EmptyFile);
}

void test_base_dir_resolution() {
  const auto r = vh::parse_manifest("0,1000,frame.pgm\n", "/replay");
  VH_EXPECT(r.error == vh::ManifestError::None);
  // Path normalisation makes both '/' and '\' acceptable across platforms.
  const std::string& p = r.entries[0].path;
  VH_EXPECT(p.find("replay") != std::string::npos);
  VH_EXPECT(p.find("frame.pgm") != std::string::npos);
}

void test_crlf_line_endings() {
  const auto r = vh::parse_manifest(
      "0,1000,a.pgm\r\n"
      "1,2000,b.pgm\r\n");
  VH_EXPECT(r.error == vh::ManifestError::None);
  VH_EXPECT(r.entries.size() == 2);
}

}  // namespace

int main() {
  test_basic_three_rows();
  test_comments_and_blank_lines();
  test_no_header_pure_data();
  test_missing_field_rejected();
  test_non_numeric_id_rejected();
  test_negative_timestamp_rejected();
  test_non_monotonic_timestamp_rejected();
  test_duplicate_id_rejected();
  test_empty_path_rejected();
  test_empty_file_rejected();
  test_base_dir_resolution();
  test_crlf_line_endings();
  return vh::test::summary("test_manifest");
}
