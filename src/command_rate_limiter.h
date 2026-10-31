#pragma once
#include <cstdint>
#include <deque>

// Transparent command-rate limiter for custom-game testing.
// One translated game-command packet is counted as one action.
class CommandRateLimiter {
public:
  static constexpr unsigned kMaximumActionsPerMinute = 400;
  static constexpr uint64_t kWindowMilliseconds = 60000;
  static constexpr uint64_t kMinimumIntervalMilliseconds = 150;

  bool allow(uint64_t now_ms) {
    while (!dispatch_times_.empty() &&
           now_ms - dispatch_times_.front() >= kWindowMilliseconds) {
      dispatch_times_.pop_front();
    }
    if (dispatch_times_.size() >= kMaximumActionsPerMinute) return false;
    if (has_dispatched_ &&
        now_ms - last_dispatch_ms_ < kMinimumIntervalMilliseconds) return false;

    dispatch_times_.push_back(now_ms);
    last_dispatch_ms_ = now_ms;
    has_dispatched_ = true;
    return true;
  }

  void reset() {
    dispatch_times_.clear();
    last_dispatch_ms_ = 0;
    has_dispatched_ = false;
  }

  unsigned used_in_window() const {
    return static_cast<unsigned>(dispatch_times_.size());
  }

private:
  std::deque<uint64_t> dispatch_times_;
  uint64_t last_dispatch_ms_ = 0;
  bool has_dispatched_ = false;
};
