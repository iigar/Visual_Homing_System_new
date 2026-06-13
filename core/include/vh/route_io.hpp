#pragma once

// VHRS v1 streaming writer and in-memory reader.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vh/interfaces.hpp"
#include "vh/route_entry.hpp"

namespace vh {

enum class RouteIoError {
  None,
  IoOpenFailed,
  IoWriteFailed,
  IoReadFailed,
  AlreadyFinalized,
  EntryInvalid,
  DimensionsTooLarge,
  PayloadTooLarge,
  TooManyEntries,
  BadMagic,
  UnsupportedVersion,
  NonZeroReservedFlags,
  HeaderDigestMismatch,
  TruncatedFile,
  TrailingBytes,
  PayloadLengthMismatch,
  UnsupportedPixelFormat,
  EntryReservedNonZero,
};

const char* to_string(RouteIoError e) noexcept;

class VhrsWriter : public IRouteWriter {
 public:
  // Opens `path` and writes a placeholder header. The actual entry_count
  // and header_digest are written by `finalize()`.
  explicit VhrsWriter(const std::filesystem::path& path);
  ~VhrsWriter() override;

  bool append(const RouteEntry& entry) override;
  bool finalize() override;

  RouteIoError last_error() const noexcept { return last_error_; }
  std::uint32_t entries_written() const noexcept { return entries_written_; }
  bool finalized() const noexcept { return finalized_; }

 private:
  std::ofstream out_;
  std::uint32_t entries_written_ = 0;
  bool finalized_ = false;
  RouteIoError last_error_ = RouteIoError::None;

  void set_error(RouteIoError e) noexcept { last_error_ = e; }
};

struct VhrsLoadResult {
  std::vector<RouteEntry> entries;
  std::uint64_t file_digest = 0;   // FNV-1a 64-bit over the whole file
  std::uint16_t version = 0;
  std::uint16_t flags = 0;
  RouteIoError error = RouteIoError::None;
  std::size_t error_entry_index = 0;  // populated on per-entry errors
};

class VhrsReader : public IRouteReader {
 public:
  // Reads and validates the file. After construction, check `error()`.
  explicit VhrsReader(const std::filesystem::path& path);

  std::size_t entry_count() const override { return result_.entries.size(); }
  std::optional<RouteEntry> entry(std::size_t index) const override;

  const VhrsLoadResult& result() const noexcept { return result_; }
  RouteIoError error() const noexcept { return result_.error; }

 private:
  VhrsLoadResult result_;
};

// Parses VHRS bytes directly (used by the file reader and by tests).
VhrsLoadResult parse_vhrs(std::string_view bytes);

}  // namespace vh
