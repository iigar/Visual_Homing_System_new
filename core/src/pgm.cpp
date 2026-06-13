#include "vh/pgm.hpp"

#include <charconv>
#include <fstream>
#include <sstream>

namespace vh {

namespace {

bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void skip_ws_and_comments(std::string_view bytes, std::size_t& i) noexcept {
  while (i < bytes.size()) {
    if (is_space(bytes[i])) {
      ++i;
    } else if (bytes[i] == '#') {
      while (i < bytes.size() && bytes[i] != '\n') ++i;
    } else {
      break;
    }
  }
}

bool read_uint(std::string_view bytes, std::size_t& i,
               std::uint32_t& out) noexcept {
  const std::size_t start = i;
  while (i < bytes.size() && bytes[i] >= '0' && bytes[i] <= '9') ++i;
  if (i == start) return false;
  auto [ptr, ec] = std::from_chars(bytes.data() + start, bytes.data() + i, out);
  (void)ptr;
  return ec == std::errc();
}

}  // namespace

const char* to_string(PgmError e) noexcept {
  switch (e) {
    case PgmError::None: return "None";
    case PgmError::IoOpenFailed: return "IoOpenFailed";
    case PgmError::BadMagic: return "BadMagic";
    case PgmError::MissingHeaderField: return "MissingHeaderField";
    case PgmError::InvalidDimensions: return "InvalidDimensions";
    case PgmError::UnsupportedMaxval: return "UnsupportedMaxval";
    case PgmError::ShortPayload: return "ShortPayload";
    case PgmError::TrailingBytes: return "TrailingBytes";
  }
  return "Unknown";
}

PgmReadResult read_pgm_gray8(std::string_view bytes,
                             std::uint64_t id,
                             std::int64_t timestamp_ns) {
  PgmReadResult r;

  if (bytes.size() < 2 || bytes[0] != 'P' || bytes[1] != '5') {
    r.error = PgmError::BadMagic;
    r.detail = "expected magic 'P5' (binary PGM)";
    return r;
  }
  std::size_t i = 2;

  std::uint32_t width = 0, height = 0, maxval = 0;
  skip_ws_and_comments(bytes, i);
  if (!read_uint(bytes, i, width)) {
    r.error = PgmError::MissingHeaderField;
    r.detail = "width";
    return r;
  }
  skip_ws_and_comments(bytes, i);
  if (!read_uint(bytes, i, height)) {
    r.error = PgmError::MissingHeaderField;
    r.detail = "height";
    return r;
  }
  skip_ws_and_comments(bytes, i);
  if (!read_uint(bytes, i, maxval)) {
    r.error = PgmError::MissingHeaderField;
    r.detail = "maxval";
    return r;
  }

  if (width == 0 || height == 0) {
    r.error = PgmError::InvalidDimensions;
    return r;
  }
  if (maxval == 0 || maxval > 255) {
    r.error = PgmError::UnsupportedMaxval;
    r.detail = "Gray8 reader requires 1..255 maxval";
    return r;
  }

  // Spec: exactly one whitespace byte separates header from binary payload.
  if (i >= bytes.size() || !is_space(bytes[i])) {
    r.error = PgmError::MissingHeaderField;
    r.detail = "missing whitespace before payload";
    return r;
  }
  ++i;

  const std::size_t expected =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (bytes.size() - i < expected) {
    r.error = PgmError::ShortPayload;
    return r;
  }
  if (bytes.size() - i > expected) {
    r.error = PgmError::TrailingBytes;
    return r;
  }

  r.frame.id = id;
  r.frame.timestamp_ns = timestamp_ns;
  r.frame.width = width;
  r.frame.height = height;
  r.frame.format = PixelFormat::Gray8;
  r.frame.payload.assign(reinterpret_cast<const std::uint8_t*>(bytes.data() + i),
                         reinterpret_cast<const std::uint8_t*>(bytes.data() + i +
                                                               expected));
  return r;
}

PgmReadResult load_pgm_gray8(const std::filesystem::path& path,
                             std::uint64_t id,
                             std::int64_t timestamp_ns) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    PgmReadResult r;
    r.error = PgmError::IoOpenFailed;
    r.detail = path.string();
    return r;
  }
  std::ostringstream buf;
  buf << f.rdbuf();
  return read_pgm_gray8(buf.str(), id, timestamp_ns);
}

}  // namespace vh
