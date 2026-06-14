#include "vh/command_sink.hpp"

#include <cstddef>

namespace vh {

DryRunCommandSink::DryRunCommandSink(std::size_t history_capacity)
    : cap_(history_capacity == 0 ? 1 : history_capacity), ring_(cap_) {}

bool DryRunCommandSink::start() noexcept {
  if (started_) return false;  // single writer: refuse a second opener
  started_ = true;
  return true;
}

void DryRunCommandSink::stop() noexcept { started_ = false; }

bool DryRunCommandSink::send(const NavigationCommand& command) {
  if (!started_) {
    ++counters_.rejected_stopped;
    return false;
  }

  CommandRecord r;
  r.seq = seq_++;
  r.timestamp_ns = command.timestamp_ns;
  r.yaw_rate_microradps = command.yaw_rate_microradps;
  r.confidence_mille = command.confidence_mille;
  r.valid = command.valid;

  ring_[head_] = r;
  head_ = (head_ + 1) % cap_;
  if (size_ < cap_) ++size_;

  ++counters_.accepted;
  if (command.valid) ++counters_.accepted_valid;
  return true;
}

const CommandRecord& DryRunCommandSink::history_at(std::size_t i) const noexcept {
  // Oldest retained entry is at (head_ - size_) modulo capacity.
  const std::size_t start = (head_ + cap_ - size_) % cap_;
  return ring_[(start + i) % cap_];
}

const CommandRecord& DryRunCommandSink::latest() const noexcept {
  return ring_[(head_ + cap_ - 1) % cap_];
}

}  // namespace vh
