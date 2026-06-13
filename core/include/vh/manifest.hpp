#pragma once

// Replay manifest: CSV file with columns `id,timestamp_ns,path`.
// One row per frame. Timestamps must be strictly monotonically increasing.
// Comments (lines starting with '#') and a leading header row are accepted.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vh {

struct ManifestEntry {
  std::uint64_t id = 0;
  std::int64_t timestamp_ns = 0;
  std::string path;  // relative or absolute; resolved against manifest dir
};

enum class ManifestError {
  None,
  IoOpenFailed,
  EmptyFile,
  MalformedRow,
  DuplicateId,
  NonMonotonicTimestamp,
  NegativeTimestamp,
  EmptyPath,
};

struct ManifestParseResult {
  std::vector<ManifestEntry> entries;
  ManifestError error = ManifestError::None;
  std::size_t error_line = 0;  // 1-based line number where error was detected
  std::string detail;          // human-readable hint for logs
};

// Parses manifest from text. `base_dir` is used only to resolve relative paths
// later — parsing itself does not touch the filesystem.
ManifestParseResult parse_manifest(std::string_view text,
                                   const std::filesystem::path& base_dir = {});

// Reads file from disk and parses. Returns IoOpenFailed if the file cannot be
// opened.
ManifestParseResult load_manifest(const std::filesystem::path& csv_path);

const char* to_string(ManifestError e) noexcept;

}  // namespace vh
