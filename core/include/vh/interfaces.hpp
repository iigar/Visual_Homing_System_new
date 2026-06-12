#pragma once

// Pipeline stage interfaces. Architecture contract for the whole project:
// every stage is replaceable and testable independently, and future sensors
// or algorithms plug in behind these interfaces — never by rewriting the
// scheduler. See docs/ARCHITECTURE.md.

#include <cstdint>
#include <optional>
#include <string>

#include "vh/frame.hpp"

namespace vh {

// ---------------------------------------------------------------------------
// Forward data types (fleshed out in their milestone; declared here so the
// interface contracts are complete from day one).
// ---------------------------------------------------------------------------

struct HealthSnapshot;      // M2
struct RouteEntry;          // M3
struct RouteMatch;          // M5/M7
struct NavigationCommand;   // M7
struct TelemetrySnapshot;   // M8
struct GateDecision;        // M13

// ---------------------------------------------------------------------------
// Capture: replay (M1), Pi camera (M11), thermal (separate milestone).
// ---------------------------------------------------------------------------
class ICameraSource {
 public:
  virtual ~ICameraSource() = default;
  // Returns the next frame, or nullopt when the source is exhausted/stopped.
  virtual std::optional<Frame> next_frame() = 0;
};

// ---------------------------------------------------------------------------
// Preprocessing: deterministic Gray8 resize etc. (M2).
// ---------------------------------------------------------------------------
class IPreprocessor {
 public:
  virtual ~IPreprocessor() = default;
  virtual Frame process(const Frame& input) = 0;
};

// ---------------------------------------------------------------------------
// Route artifact I/O: VHRS reader/writer (M3) + recorder (M4).
// ---------------------------------------------------------------------------
class IRouteWriter {
 public:
  virtual ~IRouteWriter() = default;
  virtual bool append(const RouteEntry& entry) = 0;
  virtual bool finalize() = 0;
};

class IRouteReader {
 public:
  virtual ~IRouteReader() = default;
  virtual std::size_t entry_count() const = 0;
  virtual std::optional<RouteEntry> entry(std::size_t index) const = 0;
};

// ---------------------------------------------------------------------------
// Matching: Gray8 MAD baseline first (M5); fallback matchers plug in here.
// ---------------------------------------------------------------------------
class IRouteMatcher {
 public:
  virtual ~IRouteMatcher() = default;
  virtual RouteMatch match(const Frame& live_frame) = 0;
};

// ---------------------------------------------------------------------------
// Telemetry: read-only MAVLink v1/v2 (M8). Untrusted input by definition.
// ---------------------------------------------------------------------------
class ITelemetrySource {
 public:
  virtual ~ITelemetrySource() = default;
  virtual TelemetrySnapshot snapshot() const = 0;
};

// ---------------------------------------------------------------------------
// Navigation: bounded yaw-rate-only command proposals (M7).
// ---------------------------------------------------------------------------
class INavigator {
 public:
  virtual ~INavigator() = default;
  virtual NavigationCommand propose(const RouteMatch& match,
                                    const HealthSnapshot& health) = 0;
};

// ---------------------------------------------------------------------------
// Command sink: dry-run history (M9) or blocked/bench live bridge (M13/M17).
// ---------------------------------------------------------------------------
class ICommandSink {
 public:
  virtual ~ICommandSink() = default;
  virtual bool send(const NavigationCommand& command) = 0;
  virtual bool started() const = 0;
};

// ---------------------------------------------------------------------------
// Audit: every command decision is recorded; fail closed if not ready (M13).
// ---------------------------------------------------------------------------
class IAuditLog {
 public:
  virtual ~IAuditLog() = default;
  virtual bool record_start(const std::string& reason) = 0;
  virtual bool record_decision(const NavigationCommand& command,
                               const GateDecision& decision) = 0;
  virtual bool record_stop(const std::string& reason) = 0;
  virtual bool ready() const = 0;
};

// ---------------------------------------------------------------------------
// Safety gate: explicit block reasons; default answer is "blocked" (M13).
// ---------------------------------------------------------------------------
class ISafetyGate {
 public:
  virtual ~ISafetyGate() = default;
  virtual GateDecision evaluate(const NavigationCommand& command,
                                const TelemetrySnapshot& telemetry,
                                const HealthSnapshot& health) = 0;
};

}  // namespace vh
