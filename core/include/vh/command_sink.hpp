#pragma once

// DryRunCommandSink (M9) — the command boundary during the dry-run buildout.
// It NEVER transmits anything: send() only appends to a bounded in-memory
// history and bumps counters. This is the fail-closed substitute for a live
// MAVLink writer, which does not exist and is out of scope until M16/M17 after
// review. See DECISIONS D-022.
//
// Two invariants the tests pin down:
//   * Stopped-by-default: a freshly constructed sink rejects send() until
//     start() is called — no command is recorded before the boundary is open.
//   * Single-writer: start() succeeds only from the stopped state; a second
//     start() while already started returns false (one writer at a time).

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vh/interfaces.hpp"
#include "vh/navigation_command.hpp"

namespace vh {

struct CommandRecord {
  std::uint64_t seq = 0;            // monotonic, all-time index of this command
  std::int64_t timestamp_ns = 0;
  std::int32_t yaw_rate_microradps = 0;
  std::uint16_t confidence_mille = 0;
  bool valid = false;
};

struct CommandSinkCounters {
  std::uint64_t accepted = 0;          // send() recorded while started
  std::uint64_t accepted_valid = 0;    // of those, command.valid == true
  std::uint64_t rejected_stopped = 0;  // send() refused because not started
};

class DryRunCommandSink : public ICommandSink {
 public:
  explicit DryRunCommandSink(std::size_t history_capacity = 64);

  // Open the boundary. Returns false if already started (single writer).
  bool start() noexcept;
  // Close the boundary; subsequent send() calls are rejected.
  void stop() noexcept;

  // Record a command. Returns false (and counts a rejection) when stopped.
  // Never transmits — dry-run only.
  bool send(const NavigationCommand& command) override;
  bool started() const override { return started_; }

  // --- Bounded history (retains the most recent `history_capacity`). --------
  std::size_t history_size() const noexcept { return size_; }
  std::size_t history_capacity() const noexcept { return cap_; }
  // index 0 = oldest retained, size-1 = newest.
  const CommandRecord& history_at(std::size_t i) const noexcept;
  const CommandRecord& latest() const noexcept;

  const CommandSinkCounters& counters() const noexcept { return counters_; }

 private:
  bool started_ = false;
  std::size_t cap_;
  std::vector<CommandRecord> ring_;
  std::size_t head_ = 0;   // index where the next record is written
  std::size_t size_ = 0;   // number of valid entries (<= cap_)
  std::uint64_t seq_ = 0;  // all-time command counter
  CommandSinkCounters counters_;
};

}  // namespace vh
