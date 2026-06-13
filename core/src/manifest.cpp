#include "vh/manifest.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace vh {

namespace {

std::string_view trim(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                        s.front() == '\r' || s.front() == '\n')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                        s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

bool parse_u64(std::string_view s, std::uint64_t& out) noexcept {
  s = trim(s);
  if (s.empty()) return false;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc() && ptr == s.data() + s.size();
}

bool parse_i64(std::string_view s, std::int64_t& out) noexcept {
  s = trim(s);
  if (s.empty()) return false;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
  return ec == std::errc() && ptr == s.data() + s.size();
}

}  // namespace

const char* to_string(ManifestError e) noexcept {
  switch (e) {
    case ManifestError::None: return "None";
    case ManifestError::IoOpenFailed: return "IoOpenFailed";
    case ManifestError::EmptyFile: return "EmptyFile";
    case ManifestError::MalformedRow: return "MalformedRow";
    case ManifestError::DuplicateId: return "DuplicateId";
    case ManifestError::NonMonotonicTimestamp: return "NonMonotonicTimestamp";
    case ManifestError::NegativeTimestamp: return "NegativeTimestamp";
    case ManifestError::EmptyPath: return "EmptyPath";
  }
  return "Unknown";
}

ManifestParseResult parse_manifest(std::string_view text,
                                   const std::filesystem::path& base_dir) {
  ManifestParseResult result;
  std::unordered_set<std::uint64_t> seen_ids;
  std::int64_t last_ts = std::numeric_limits<std::int64_t>::min();

  std::size_t line_no = 0;
  std::size_t cursor = 0;
  bool any_data = false;

  while (cursor <= text.size()) {
    ++line_no;
    const std::size_t newline = text.find('\n', cursor);
    std::string_view line = (newline == std::string_view::npos)
                                ? text.substr(cursor)
                                : text.substr(cursor, newline - cursor);
    cursor = (newline == std::string_view::npos) ? text.size() + 1 : newline + 1;

    line = trim(line);
    if (line.empty()) continue;
    if (line.front() == '#') continue;

    // Optional header row: skip a single non-numeric first column once.
    if (!any_data) {
      std::uint64_t probe = 0;
      const std::size_t comma = line.find(',');
      if (comma != std::string_view::npos &&
          !parse_u64(line.substr(0, comma), probe)) {
        continue;
      }
    }

    const std::size_t c1 = line.find(',');
    const std::size_t c2 = (c1 == std::string_view::npos)
                               ? std::string_view::npos
                               : line.find(',', c1 + 1);
    if (c1 == std::string_view::npos || c2 == std::string_view::npos) {
      result.error = ManifestError::MalformedRow;
      result.error_line = line_no;
      result.detail = "expected 3 comma-separated fields";
      result.entries.clear();
      return result;
    }

    ManifestEntry entry;
    if (!parse_u64(line.substr(0, c1), entry.id)) {
      result.error = ManifestError::MalformedRow;
      result.error_line = line_no;
      result.detail = "id is not a non-negative integer";
      result.entries.clear();
      return result;
    }
    if (!parse_i64(line.substr(c1 + 1, c2 - c1 - 1), entry.timestamp_ns)) {
      result.error = ManifestError::MalformedRow;
      result.error_line = line_no;
      result.detail = "timestamp_ns is not an integer";
      result.entries.clear();
      return result;
    }
    if (entry.timestamp_ns < 0) {
      result.error = ManifestError::NegativeTimestamp;
      result.error_line = line_no;
      result.entries.clear();
      return result;
    }

    std::string_view raw_path = trim(line.substr(c2 + 1));
    if (raw_path.empty()) {
      result.error = ManifestError::EmptyPath;
      result.error_line = line_no;
      result.entries.clear();
      return result;
    }

    if (!seen_ids.insert(entry.id).second) {
      result.error = ManifestError::DuplicateId;
      result.error_line = line_no;
      result.entries.clear();
      return result;
    }
    if (entry.timestamp_ns <= last_ts) {
      result.error = ManifestError::NonMonotonicTimestamp;
      result.error_line = line_no;
      result.detail = "timestamps must be strictly increasing";
      result.entries.clear();
      return result;
    }
    last_ts = entry.timestamp_ns;

    std::filesystem::path p{std::string(raw_path)};
    if (!base_dir.empty() && p.is_relative()) {
      p = base_dir / p;
    }
    entry.path = p.lexically_normal().string();

    result.entries.push_back(std::move(entry));
    any_data = true;
  }

  if (result.entries.empty()) {
    result.error = ManifestError::EmptyFile;
    return result;
  }
  return result;
}

ManifestParseResult load_manifest(const std::filesystem::path& csv_path) {
  std::ifstream f(csv_path, std::ios::binary);
  if (!f) {
    ManifestParseResult r;
    r.error = ManifestError::IoOpenFailed;
    r.detail = csv_path.string();
    return r;
  }
  std::ostringstream buf;
  buf << f.rdbuf();
  return parse_manifest(buf.str(), csv_path.parent_path());
}

}  // namespace vh
