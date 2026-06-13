#pragma once

// Stateless inspection of a parsed VHRS route. Used by the CLI tool and by
// readiness checker scripts (M6/M14) which grep stable key=value lines.

#include <cstdint>
#include <string>
#include <vector>

#include "vh/route_entry.hpp"
#include "vh/route_io.hpp"

namespace vh {

struct InspectionReport {
  std::size_t entry_count = 0;
  std::uint64_t file_digest = 0;
  std::uint16_t version = 0;

  // Dimensions are reported as the set of (width,height) pairs observed.
  // For a well-formed recorded route they should all be identical.
  std::vector<std::pair<std::uint32_t, std::uint32_t>> dimensions_seen;

  std::vector<std::uint16_t> pixel_format_codes_seen;
  std::size_t total_payload_bytes = 0;

  // Timestamp monotonicity.
  bool timestamps_monotonic = true;
  std::size_t first_non_monotonic_index = 0;

  // Altitude range (mm). 0xFFFFFFFF in altitude_min == "no entry had known
  // altitude". Same for heading.
  std::uint32_t altitude_min_mm = 0xFFFFFFFFu;
  std::uint32_t altitude_max_mm = 0;
  bool any_known_altitude = false;

  std::int32_t heading_min_millirad = 0;
  std::int32_t heading_max_millirad = 0;
  bool any_known_heading = false;
};

InspectionReport inspect(const VhrsLoadResult& loaded);

// Render report as one stable `key=value` line per fact, joined with '\n'.
// Designed to be grep-friendly for checker scripts.
std::string format_report(const InspectionReport& r);

}  // namespace vh
