#include "vh/route_inspect.hpp"

#include <algorithm>
#include <sstream>

namespace vh {

InspectionReport inspect(const VhrsLoadResult& loaded) {
  InspectionReport r;
  r.entry_count = loaded.entries.size();
  r.file_digest = loaded.file_digest;
  r.version = loaded.version;

  std::int64_t prev_ts = std::numeric_limits<std::int64_t>::min();
  for (std::size_t i = 0; i < loaded.entries.size(); ++i) {
    const auto& e = loaded.entries[i];

    const auto dims = std::make_pair(e.width, e.height);
    if (std::find(r.dimensions_seen.begin(), r.dimensions_seen.end(), dims) ==
        r.dimensions_seen.end()) {
      r.dimensions_seen.push_back(dims);
    }
    const std::uint16_t code = static_cast<std::uint16_t>(e.format);
    if (std::find(r.pixel_format_codes_seen.begin(),
                  r.pixel_format_codes_seen.end(),
                  code) == r.pixel_format_codes_seen.end()) {
      r.pixel_format_codes_seen.push_back(code);
    }

    r.total_payload_bytes += e.payload.size();

    if (e.timestamp_ns <= prev_ts && r.timestamps_monotonic) {
      r.timestamps_monotonic = false;
      r.first_non_monotonic_index = i;
    }
    prev_ts = e.timestamp_ns;

    if (e.altitude_band_mm != RouteEntry::kAltitudeUnknown) {
      if (!r.any_known_altitude) {
        r.altitude_min_mm = e.altitude_band_mm;
        r.altitude_max_mm = e.altitude_band_mm;
        r.any_known_altitude = true;
      } else {
        r.altitude_min_mm = std::min(r.altitude_min_mm, e.altitude_band_mm);
        r.altitude_max_mm = std::max(r.altitude_max_mm, e.altitude_band_mm);
      }
    }
    if (e.heading_millirad != RouteEntry::kHeadingUnknown) {
      if (!r.any_known_heading) {
        r.heading_min_millirad = e.heading_millirad;
        r.heading_max_millirad = e.heading_millirad;
        r.any_known_heading = true;
      } else {
        r.heading_min_millirad =
            std::min(r.heading_min_millirad, e.heading_millirad);
        r.heading_max_millirad =
            std::max(r.heading_max_millirad, e.heading_millirad);
      }
    }
  }
  return r;
}

std::string format_report(const InspectionReport& r) {
  std::ostringstream s;
  s << "vhrs_version=" << r.version << '\n';
  s << "entry_count=" << r.entry_count << '\n';
  s << "file_digest_fnv1a64=0x" << std::hex << r.file_digest << std::dec
    << '\n';
  s << "total_payload_bytes=" << r.total_payload_bytes << '\n';
  s << "dimensions_unique=" << r.dimensions_seen.size() << '\n';
  for (const auto& d : r.dimensions_seen) {
    s << "dimension=" << d.first << "x" << d.second << '\n';
  }
  s << "pixel_format_codes_unique=" << r.pixel_format_codes_seen.size() << '\n';
  for (auto c : r.pixel_format_codes_seen) {
    s << "pixel_format_code=" << c << '\n';
  }
  s << "timestamps_monotonic=" << (r.timestamps_monotonic ? "true" : "false")
    << '\n';
  if (!r.timestamps_monotonic) {
    s << "first_non_monotonic_index=" << r.first_non_monotonic_index << '\n';
  }
  s << "altitude_known=" << (r.any_known_altitude ? "true" : "false") << '\n';
  if (r.any_known_altitude) {
    s << "altitude_min_mm=" << r.altitude_min_mm << '\n';
    s << "altitude_max_mm=" << r.altitude_max_mm << '\n';
  }
  s << "heading_known=" << (r.any_known_heading ? "true" : "false") << '\n';
  if (r.any_known_heading) {
    s << "heading_min_millirad=" << r.heading_min_millirad << '\n';
    s << "heading_max_millirad=" << r.heading_max_millirad << '\n';
  }
  return s.str();
}

}  // namespace vh
