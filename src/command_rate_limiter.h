#pragma once
#include <cstdint>
#include <deque>

// One translated game-command packet is counted as one action.
// APM=0 disables the cap; otherwise commands are both window-limited and paced.
class CommandRateLimiter {
public:
  static constexpr uint64_t kWindowMilliseconds = 60000;

  explicit CommandRateLimiter(unsigned maximum_actions_per_minute = 400) {
    set_limit(maximum_actions_per_minute);
  }

  void set_limit(unsigned maximum_actions_per_minute) {
    maximum_actions_per_minute_ = maximum_actions_per_minute;
    minimum_interval_ms_ = maximum_actions_per_minute_ == 0
      ? 0
      : (kWindowMilliseconds + maximum_actions_per_minute_ - 1) /
          maximum_actions_per_minute_;
    reset();
  }

  bool allow(uint64_t now_ms) {
    while (!dispatch_times_.empty() &&
           now_ms >= dispatch_times_.front() &&
           now_ms - dispatch_times_.front() >= kWindowMilliseconds) {
      dispatch_times_.pop_front();
    }
    if (maximum_actions_per_minute_ != 0 &&
        dispatch_times_.size() >= maximum_actions_per_minute_) return false;
    if (has_dispatched_ && minimum_interval_ms_ != 0 &&
        now_ms >= last_dispatch_ms_ &&
        now_ms - last_dispatch_ms_ < minimum_interval_ms_) return false;

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

  unsigned limit() const { return maximum_actions_per_minute_; }

private:
  std::deque<uint64_t> dispatch_times_;
  uint64_t last_dispatch_ms_ = 0;
  uint64_t minimum_interval_ms_ = 150;
  unsigned maximum_actions_per_minute_ = 400;
  bool has_dispatched_ = false;
};
