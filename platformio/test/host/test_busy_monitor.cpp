#include "epd/BusyPhaseMonitor.h"
#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; std::exit(1); \
} } while (false)

using epd::BusyPhaseMonitor;
using epd::BusyResult;
constexpr epd::BusyWindow window{1000, 2000, 5000};

void missing_and_stuck()
{
  BusyPhaseMonitor m;
  m.begin(0, false, window, 100);
  m.poll(999);
  CHECK(m.stats().result == BusyResult::Running);
  m.poll(1000);
  CHECK(m.stats().result == BusyResult::MissingBusy);
  m.begin(0, true, window, 100);
  m.poll(5000);
  CHECK(m.stats().result == BusyResult::Timeout);
}

void all_short_low_widths()
{
  BusyPhaseMonitor m;
  m.begin(0, false, {100000, 150000, 500000}, 100);
  uint32_t now = 1000;
  for (uint32_t width = 1; width < 100; ++width)
  {
    m.edge(now, true);
    m.edge(now + width, false);
    now += 500;
    CHECK(m.stats().result == BusyResult::Running);
  }
  CHECK(m.stats().low_glitches == 99);
  m.edge(60000, true);
  m.edge(250000, false);
  m.poll(250100);
  CHECK(m.stats().result == BusyResult::Ok);
  CHECK(m.stats().elapsed_us == 250100);
}

void all_short_high_widths()
{
  BusyPhaseMonitor m;
  m.begin(0, true, {1000, 150000, 500000}, 100);
  uint32_t now = 1000;
  for (uint32_t width = 1; width < 100; ++width)
  {
    m.edge(now, false);
    m.edge(now + width, true);
    now += 500;
    CHECK(m.stats().result == BusyResult::Running);
  }
  CHECK(m.stats().high_glitches == 99);
  CHECK(m.stats().early_ends == 0);
  m.edge(200000, false);
  m.poll(200100);
  CHECK(m.stats().result == BusyResult::Ok);
  CHECK(m.stats().elapsed_us == 200100); // no edge restarted the clock
}

void early_ready_is_not_success_at_minimum()
{
  BusyPhaseMonitor m;
  m.begin(0, true, window, 100);
  m.edge(1950, false);
  m.poll(2050);
  CHECK(m.stats().result == BusyResult::Running);
  CHECK(m.stats().early_ends == 1);
  m.poll(5000);
  CHECK(m.stats().result == BusyResult::Timeout);
}

void later_valid_response_uses_original_clock()
{
  BusyPhaseMonitor m;
  m.begin(0, true, window, 100);
  m.edge(500, false);
  m.edge(1500, true);
  CHECK(m.stats().early_ends == 1);
  m.edge(4000, false);
  m.poll(4100);
  CHECK(m.stats().result == BusyResult::Ok);
  CHECK(m.stats().elapsed_us == 4100);
}

void late_short_response_does_not_pass_minimum()
{
  BusyPhaseMonitor m;
  m.begin(0, false, {4000, 2000, 5000}, 100);
  m.edge(3000, true);
  m.edge(3200, false);
  m.poll(3300);
  CHECK(m.stats().early_ends == 1);
  CHECK(m.stats().result == BusyResult::Running);
  m.poll(5000);
  CHECK(m.stats().result == BusyResult::Timeout);
}

void glitch_does_not_extend_deadline()
{
  BusyPhaseMonitor m;
  m.begin(0, true, window, 100);
  m.edge(4800, false);
  m.edge(4850, true);
  m.poll(5000);
  CHECK(m.stats().high_glitches == 1);
  CHECK(m.stats().result == BusyResult::Timeout);
}

void threshold_and_boundaries()
{
  BusyPhaseMonitor m;
  m.begin(0, false, {1000, 100, 5000}, 100);
  m.edge(500, true);
  m.edge(600, false); // exactly 100 us is valid
  m.poll(700);
  CHECK(m.stats().low_glitches == 0);
  CHECK(m.stats().result == BusyResult::Ok);
  m.begin(0, true, window, 100);
  m.edge(2000, false);
  m.poll(2100);
  CHECK(m.stats().result == BusyResult::Ok);
  m.begin(0, true, window, 100);
  m.edge(4900, false);
  m.poll(5000);
  CHECK(m.stats().result == BusyResult::Ok);
  m.begin(0, true, window, 100);
  m.edge(4901, false);
  m.poll(5001);
  CHECK(m.stats().result == BusyResult::Timeout);
}

void pending_low_at_assertion_deadline()
{
  BusyPhaseMonitor m;
  m.begin(0, false, window, 100);
  m.edge(999, true);
  m.poll(1000);
  CHECK(m.stats().result == BusyResult::Running);
  m.edge(1049, false);
  CHECK(m.stats().low_glitches == 1);
  CHECK(m.stats().result == BusyResult::MissingBusy);
  m.begin(0, false, window, 100);
  m.edge(999, true);
  m.poll(1099);
  CHECK(m.stats().result == BusyResult::Running);
  m.edge(3000, false);
  m.poll(3100);
  CHECK(m.stats().result == BusyResult::Ok);
}

void delayed_poll_and_timer_wrap()
{
  BusyPhaseMonitor m;
  m.begin(0, true, window, 100);
  m.edge(3000, false);
  m.edge(9000, true); // task missed ready, ISR recognizes the previous stable HIGH
  CHECK(m.stats().result == BusyResult::Ok);
  CHECK(m.stats().elapsed_us == 3100);
  constexpr uint32_t start = 0xffffff00U;
  m.begin(start, true, window, 100);
  m.edge(start + 3000U, false);
  m.poll(start + 3100U);
  CHECK(m.stats().result == BusyResult::Ok);
  CHECK(m.stats().elapsed_us == 3100);
}

void invalid_configuration()
{
  BusyPhaseMonitor m;
  m.begin(0, false, window, 0);
  CHECK(m.stats().result == BusyResult::InvalidConfig);
  m.begin(0, false, {1000, 5000, 5000}, 100);
  CHECK(m.stats().result == BusyResult::InvalidConfig);
  m.begin(0, false, {5000, 2000, 5000}, 100);
  CHECK(m.stats().result == BusyResult::InvalidConfig);
}

int main()
{
  missing_and_stuck();
  all_short_low_widths();
  all_short_high_widths();
  early_ready_is_not_success_at_minimum();
  later_valid_response_uses_original_clock();
  glitch_does_not_extend_deadline();
  threshold_and_boundaries();
  pending_low_at_assertion_deadline();
  delayed_poll_and_timer_wrap();
  invalid_configuration();
  late_short_response_does_not_pass_minimum();
  std::cout << "BUSY monitor: 11 scenarios passed (all glitch widths 1..99 us included)\n";
}
