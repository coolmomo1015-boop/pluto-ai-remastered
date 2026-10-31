#include "command_rate_limiter.h"
#include <cassert>

int main() {
  {
    CommandRateLimiter limiter;
    assert(limiter.allow(0));
    assert(!limiter.allow(149));
    assert(limiter.allow(150));
    assert(limiter.used_in_window() == 2);
  }
  {
    CommandRateLimiter limiter;
    for (unsigned i = 0; i < 400; ++i) {
      assert(limiter.allow(static_cast<uint64_t>(i) * 150));
    }
    assert(limiter.used_in_window() == 400);
    assert(!limiter.allow(59999));
    assert(limiter.allow(60000));
    assert(limiter.used_in_window() == 400);
  }
  {
    CommandRateLimiter limiter;
    assert(limiter.allow(1000));
    limiter.reset();
    assert(limiter.used_in_window() == 0);
    assert(limiter.allow(1000));
  }
  return 0;
}
