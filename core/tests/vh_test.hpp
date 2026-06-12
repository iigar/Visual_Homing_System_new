#pragma once

// Minimal deterministic test helper. No external dependencies — GoogleTest is
// too heavy for Pi Zero builds. Each test file is its own CTest executable.

#include <cstdio>
#include <cstdlib>
#include <string>

namespace vh::test {

inline int failures = 0;

inline void expect(bool condition, const char* expr, const char* file, int line) {
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, expr);
  }
}

inline int summary(const char* suite) {
  if (failures == 0) {
    std::printf("PASS %s\n", suite);
    return EXIT_SUCCESS;
  }
  std::fprintf(stderr, "FAILED %s: %d failure(s)\n", suite, failures);
  return EXIT_FAILURE;
}

}  // namespace vh::test

#define VH_EXPECT(cond) ::vh::test::expect((cond), #cond, __FILE__, __LINE__)
#define VH_TEST_MAIN(suite_name)                      \
  int main() { return ::vh::test::summary(suite_name); }
