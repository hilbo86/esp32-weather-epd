#include "epd/RobustGxEPD2_730c.h"
#include "epd/DisplayUpdate.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n" << Serial.output; std::exit(1); \
} } while (false)

FakeSerial Serial;
namespace fake
{
struct Edge { uint32_t time; int value; };
uint32_t now;
int busy = HIGH, rst = HIGH;
void (*callback)(void*);
void* context;
std::vector<Edge> edges;
std::vector<uint8_t> commands;
unsigned resets, power_ons, refreshes, power_offs, bytes;
unsigned reset_failures, power_on_failures, refresh_failures, power_off_failures;
bool short_low_reset, high_glitches, measured_timings;

void reset()
{
  now = 0; busy = HIGH; rst = HIGH; callback = nullptr; context = nullptr;
  edges.clear(); commands.clear(); Serial.output.clear();
  resets = power_ons = refreshes = power_offs = bytes = 0;
  reset_failures = power_on_failures = refresh_failures = power_off_failures = 0;
  short_low_reset = high_glitches = measured_timings = false;
}

void schedule(uint32_t after, int value)
{
  edges.push_back({now + after, value});
  std::sort(edges.begin(), edges.end(), [](Edge a, Edge b) { return a.time < b.time; });
}

void advance(uint32_t duration)
{
  const uint32_t target = now + duration;
  while (!edges.empty() && edges.front().time <= target)
  {
    const Edge e = edges.front();
    edges.erase(edges.begin());
    now = e.time;
    busy = e.value;
    if (callback) callback(context);
  }
  now = target;
}

void command(uint8_t value)
{
  commands.push_back(value);
  if (value == 0x04)
  {
    ++power_ons;
    schedule(100, LOW);
    schedule(power_ons <= power_on_failures ? 1000 : (measured_timings ? 172040 : 173000), HIGH);
    if (high_glitches && power_ons > power_on_failures)
    {
      schedule(10000, HIGH); schedule(10020, LOW);
      schedule(20000, HIGH); schedule(20099, LOW);
    }
  }
  else if (value == 0x12)
  {
    ++refreshes;
    if (refreshes > refresh_failures)
    {
      schedule(100, LOW); schedule(measured_timings ? 35058748 : 35000000, HIGH);
    }
  }
  else if (value == 0x02)
  {
    ++power_offs;
    schedule(100, LOW);
    schedule(power_offs <= power_off_failures ? 1000 : (measured_timings ? 144998 : 146000), HIGH);
  }
  advance(4);
}
void data(uint8_t) { advance(4); }
void transfer(uint8_t) { ++bytes; advance(4); }
} // namespace fake

uint32_t micros() { return fake::now; }
void delay(unsigned long ms) { fake::advance(uint32_t(ms * 1000)); }
void pinMode(int, int) {}
int digitalRead(int) { return fake::busy; }
void digitalWrite(int pin, int value)
{
  if (pin != 21) return;
  if (value == LOW)
  {
    fake::edges.clear();
    fake::busy = HIGH;
  }
  if (fake::rst == LOW && value == HIGH)
  {
    ++fake::resets;
    if (fake::resets > fake::reset_failures)
    {
      if (fake::measured_timings)
      {
        // Representative early reset response followed by a valid interval.
        // Only the total elapsed time/early-end count were measured on hardware.
        fake::schedule(100, LOW); fake::schedule(1000, HIGH);
        fake::schedule(12000, LOW); fake::schedule(16087, HIGH);
      }
      else { fake::schedule(100, LOW); fake::schedule(7000, HIGH); }
    }
    else if (fake::short_low_reset)
    {
      fake::schedule(1000, LOW); fake::schedule(1050, HIGH);
      fake::schedule(2000, LOW); fake::schedule(2099, HIGH);
    }
  }
  fake::rst = value;
}
void attachInterruptArg(int, void (*cb)(void*), void* arg, int) { fake::callback = cb; fake::context = arg; }
void detachInterrupt(int) { fake::callback = nullptr; }

constexpr EpdBusyConfig config{100, {20000, 2000, 40000}, {50000, 150000, 250000},
  {100000, 30000000, 45000000}, {50000, 120000, 250000}, 2};

struct PagedDisplay
{
  RobustGxEPD2_730c epd2{13, 22, 21, 14};
  unsigned page = 0, starts = 0;
  void firstPage() { page = 0; ++starts; }
  bool nextPage()
  {
    const uint8_t pixels[] = {0x11};
    epd2.writeNative(pixels, nullptr, 0, page, 2, 1, false, false, false);
    if (++page < 4) return true;
    epd2.refresh(false);
    epd2.powerOff();
    return false;
  }
};

void reset_failures_are_bounded_and_stop_spi()
{
  fake::reset();
  fake::reset_failures = 10;
  RobustGxEPD2_730c driver(13, 22, 21, 14);
  driver.beginUpdate(config);
  CHECK(!driver.prepareUpdate());
  CHECK(driver.attempts() == 3 && fake::resets == 3);
  driver.refresh(false); driver.powerOff(); driver.hibernate();
  CHECK(fake::commands.empty());
}

void reset_glitches_are_counted_then_retried()
{
  fake::reset();
  fake::reset_failures = 1; fake::short_low_reset = true;
  RobustGxEPD2_730c driver(13, 22, 21, 14);
  driver.beginUpdate(config);
  CHECK(driver.prepareUpdate());
  CHECK(driver.attempts() == 2 && fake::resets == 2);
  CHECK(std::count(fake::commands.begin(), fake::commands.end(), 0xaa) == 1);
  CHECK(Serial.output.find("glitches LOW=2 HIGH=0") != std::string::npos);
}

void early_power_on_resets_and_high_glitches_do_not_end_phase()
{
  fake::reset();
  fake::power_on_failures = 1; fake::high_glitches = true;
  RobustGxEPD2_730c driver(13, 22, 21, 14);
  driver.beginUpdate(config);
  CHECK(driver.prepareUpdate());
  CHECK(driver.attempts() == 2 && fake::resets == 2 && fake::power_ons == 2);
  CHECK(Serial.output.find("early ends=1") != std::string::npos);
  CHECK(Serial.output.find("glitches LOW=0 HIGH=2") != std::string::npos);
}

void refresh_failure_replays_all_four_pages()
{
  fake::reset(); fake::refresh_failures = 1;
  PagedDisplay display;
  display.epd2.beginUpdate(config);
  unsigned draws = 0;
  CHECK(epd::renderPagesWithRecovery(display, [&] { ++draws; }));
  CHECK(draws == 8 && display.starts == 2);
  CHECK(fake::resets == 2 && fake::refreshes == 2 && fake::power_offs == 1);
  CHECK(display.epd2.attempts() == 2 && display.epd2.healthy());
  display.epd2.hibernate();
  CHECK(fake::commands.back() == 0x07);
}

void initialization_and_refresh_share_budget()
{
  fake::reset(); fake::reset_failures = 1; fake::refresh_failures = 10;
  PagedDisplay display;
  display.epd2.beginUpdate(config);
  unsigned draws = 0;
  CHECK(!epd::renderPagesWithRecovery(display, [&] { ++draws; }));
  CHECK(fake::resets == 3 && display.epd2.attempts() == 3);
  CHECK(draws == 8 && display.starts == 2 && fake::power_offs == 0);
  const size_t count = fake::commands.size();
  display.epd2.hibernate();
  CHECK(fake::commands.size() == count); // application can cut VCC without more commands
}

void zero_additional_attempts()
{
  fake::reset(); fake::power_on_failures = 10;
  RobustGxEPD2_730c driver(13, 22, 21, 14);
  auto once = config; once.additional_attempts = 0;
  driver.beginUpdate(once);
  CHECK(!driver.prepareUpdate());
  CHECK(fake::resets == 1 && driver.attempts() == 1);
}

void power_off_failure_replays_update()
{
  fake::reset(); fake::power_off_failures = 1;
  PagedDisplay display;
  display.epd2.beginUpdate(config);
  unsigned draws = 0;
  CHECK(epd::renderPagesWithRecovery(display, [&] { ++draws; }));
  CHECK(draws == 8 && fake::resets == 2 && fake::refreshes == 2 && fake::power_offs == 2);
}

void measured_successful_timings_fit_calibrated_windows()
{
  fake::reset(); fake::measured_timings = true;
  PagedDisplay display;
  display.epd2.beginUpdate(config);
  CHECK(epd::renderPagesWithRecovery(display, [] {}));
  CHECK(fake::resets == 1 && display.epd2.attempts() == 1);
  CHECK(Serial.output.find("_InitDisplay reset : 16187 us; ok; glitches LOW=0 HIGH=0 (<100 us); early ends=1") != std::string::npos);
  CHECK(Serial.output.find("_PowerOn : 172140 us; ok;") != std::string::npos);
  CHECK(Serial.output.find("_refresh : 35058848 us; ok;") != std::string::npos);
  CHECK(Serial.output.find("_PowerOff : 145098 us; ok;") != std::string::npos);
}

int main()
{
  reset_failures_are_bounded_and_stop_spi();
  reset_glitches_are_counted_then_retried();
  early_power_on_resets_and_high_glitches_do_not_end_phase();
  refresh_failure_replays_all_four_pages();
  initialization_and_refresh_share_budget();
  zero_additional_attempts();
  power_off_failure_replays_update();
  measured_successful_timings_fit_calibrated_windows();
  std::cout << "Panel driver: 8 recovery scenarios passed\n";
}
