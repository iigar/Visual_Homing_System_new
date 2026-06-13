// vh_route_inspect: prints a stable key=value report describing a VHRS file.
// Exit code 0 on success; 1 on parse failure (with an `inspect_error=...`
// line on stdout for the readiness checker scripts to grep).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "vh/route_inspect.hpp"
#include "vh/route_io.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: vh_route_inspect <route.vhrs>\n");
    return EXIT_FAILURE;
  }
  vh::VhrsReader r((std::filesystem::path(argv[1])));
  if (r.error() != vh::RouteIoError::None) {
    std::printf("inspect_error=%s\n", vh::to_string(r.error()));
    if (r.result().error_entry_index != 0) {
      std::printf("inspect_error_entry_index=%zu\n",
                  r.result().error_entry_index);
    }
    return EXIT_FAILURE;
  }
  const std::string txt = vh::format_report(vh::inspect(r.result()));
  std::fputs(txt.c_str(), stdout);
  return EXIT_SUCCESS;
}
