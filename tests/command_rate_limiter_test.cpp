#include "command_rate_limiter.h"
#include <cassert>

static void check_limit(unsigned limit,unsigned expected_interval) {
  CommandRateLimiter limiter(limit);
  assert(limiter.allow(0));
  if(expected_interval==0) {
    assert(limiter.allow(1));
    assert(limiter.allow(1));
  } else {
    assert(!limiter.allow(expected_interval-1));
    assert(limiter.allow(expected_interval));
  }
  assert(limiter.limit()==limit);
}

int main() {
  check_limit(300,200);
  check_limit(500,120);
  check_limit(1000,60);
  check_limit(0,0); // Unlimited

  {
    CommandRateLimiter limiter(300);
    for(unsigned i=0;i<300;++i)
      assert(limiter.allow(static_cast<uint64_t>(i)*200));
    assert(!limiter.allow(59999));
    assert(limiter.allow(60000));
    assert(limiter.used_in_window()==300);
  }
  {
    CommandRateLimiter limiter(500);
    for(unsigned i=0;i<500;++i)
      assert(limiter.allow(static_cast<uint64_t>(i)*120));
    assert(!limiter.allow(59999));
    assert(limiter.allow(60000));
    assert(limiter.used_in_window()==500);
  }
  {
    CommandRateLimiter limiter(1000);
    for(unsigned i=0;i<1000;++i)
      assert(limiter.allow(static_cast<uint64_t>(i)*60));
    assert(!limiter.allow(59999));
    assert(limiter.allow(60000));
    assert(limiter.used_in_window()==1000);
  }
  {
    CommandRateLimiter limiter(0);
    for(unsigned i=0;i<1000;++i)assert(limiter.allow(1000));
    assert(limiter.used_in_window()==1000);
  }
  {
    CommandRateLimiter limiter;
    assert(limiter.allow(1000));
    limiter.reset();
    assert(limiter.used_in_window()==0);
    assert(limiter.allow(1000));
  }
  return 0;
}
