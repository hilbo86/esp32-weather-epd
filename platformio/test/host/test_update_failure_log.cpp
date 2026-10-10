#include "update_failure_log.h"
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <time.h>

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __func__ << ':' << __LINE__ << ": " #condition "\n"; std::exit(1); \
} } while (false)

using namespace update_failure;
constexpr uint32_t firstTimestamp = 1791600000;
TestSerial Serial;
time_t fakeNow = firstTimestamp;

extern "C" time_t time(time_t* result) noexcept
{
  if (result) *result = fakeNow;
  return fakeNow;
}

uint16_t bit(ErrorCode code) { return static_cast<uint16_t>(code); }
bool unchanged(const History& a, const History& b)
{
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

void cold_start_and_invalid_headers()
{
  History h;
  std::memset(&h, 0xA5, sizeof(h));
  AttemptLog log(h);
  log.initialize();
  CHECK(h.magic == formatMagic && h.count == 0 && h.next == 0);
  CHECK(h.batteryErrorActive == 0);
  log.record(ErrorCode::SensorInit, firstTimestamp);
  log.finishAttempt();
  const auto before = h;
  log.initialize();
  CHECK(unchanged(h, before));
  h.next = capacity;
  log.initialize();
  CHECK(h.count == 0 && h.next == 0);
  h.count = capacity + 1;
  log.initialize();
  CHECK(h.count == 0);
  h.magic = formatMagic + 1;
  log.initialize();
  CHECK(h.magic == formatMagic);
}

void only_last_ten_failures_survive_wakes_and_successes()
{
  History h = {};
  for (int i = 0; i < 23; ++i)
  {
    AttemptLog wake(h);
    wake.initialize();
    const auto beforeFailure = h;
    wake.record(ErrorCode::OneCallApi, firstTimestamp + i, 400 + i);
    CHECK(unchanged(h, beforeFailure)); // No RTC write until sleep.
    wake.finishAttempt();
    const auto afterFailure = h;
    wake.finishAttempt(); // No duplicate commit.
    CHECK(unchanged(h, afterFailure));

    AttemptLog successfulWake(h);
    successfulWake.initialize();
    successfulWake.finishAttempt();
    CHECK(unchanged(h, afterFailure));
  }
  CHECK(h.count == 10);
  AttemptLog reader(h);
  reader.initialize();
  for (int i = 0; i < 10; ++i)
  {
    const auto& entry = reader.oldestFirst(i);
    CHECK(entry.timestamp == firstTimestamp + 13 + i);
    CHECK(entry.codes == bit(ErrorCode::OneCallApi));
    CHECK(entry.detail == 413 + i);
  }
}

void multiple_causes_take_one_slot()
{
  History h = {};
  AttemptLog log(h);
  log.initialize();
  log.record(ErrorCode::AirPollutionApi, firstTimestamp, -258);
  log.record(ErrorCode::DisplayUpdate, firstTimestamp + 40);
  log.record(ErrorCode::DisplayUpdate, firstTimestamp + 41);
  log.record(ErrorCode::AlertAllocation, firstTimestamp + 41);
  log.finishAttempt();
  CHECK(h.count == 1);
  const auto& entry = log.oldestFirst(0);
  CHECK(entry.timestamp == firstTimestamp);
  CHECK(entry.codes == (bit(ErrorCode::AirPollutionApi) |
                        bit(ErrorCode::DisplayUpdate) |
                        bit(ErrorCode::AlertAllocation)));
  CHECK(entry.detail == -258); // Display/allocation must preserve API detail.
}

void battery_logged_once_until_recovery_even_if_display_fails()
{
  History h = {};
  AttemptLog firstWake(h);
  firstWake.initialize();
  const auto beforeFirstBattery = h;
  firstWake.recordBattery(ErrorCode::BatteryLow, firstTimestamp);
  firstWake.recordBattery(ErrorCode::BatteryVeryLow, firstTimestamp + 1);
  CHECK(unchanged(h, beforeFirstBattery)); // Even the latch waits until commit.
  firstWake.record(ErrorCode::DisplayUpdate, firstTimestamp + 40);
  firstWake.finishAttempt();
  CHECK(h.count == 1 && h.batteryErrorActive == 1);
  CHECK(firstWake.oldestFirst(0).codes == (bit(ErrorCode::BatteryLow) |
                                         bit(ErrorCode::DisplayUpdate)));

  const auto beforeRepeat = h;
  AttemptLog nextWake(h);
  nextWake.initialize();
  nextWake.recordBattery(ErrorCode::BatteryVeryLow, firstTimestamp + 1800);
  nextWake.finishAttempt();
  nextWake.recordBattery(ErrorCode::BatteryCritical, firstTimestamp + 9000);
  nextWake.finishAttempt();
  CHECK(unchanged(h, beforeRepeat)); // Includes unchanged timestamp and header.

  nextWake.recordBattery(ErrorCode::BatteryLow, firstTimestamp + 10000);
  nextWake.record(ErrorCode::DisplayUpdate, firstTimestamp + 10040);
  nextWake.finishAttempt();
  CHECK(h.count == 2);
  CHECK(nextWake.oldestFirst(1).codes == bit(ErrorCode::DisplayUpdate));

  nextWake.batteryRecovered();
  const auto recovered = h;
  nextWake.batteryRecovered();
  CHECK(unchanged(h, recovered));
  nextWake.recordBattery(ErrorCode::BatteryCritical, firstTimestamp + 20000);
  nextWake.finishAttempt();
  CHECK(h.count == 3);
  CHECK(nextWake.oldestFirst(2).codes == bit(ErrorCode::BatteryCritical));
}

void battery_dedup_survives_eviction()
{
  History h = {};
  AttemptLog log(h);
  log.initialize();
  log.recordBattery(ErrorCode::BatteryLow, firstTimestamp);
  log.finishAttempt();
  for (int i = 0; i < 11; ++i)
  {
    log.record(ErrorCode::DisplayUpdate, firstTimestamp + i + 1);
    log.finishAttempt();
  }
  const auto before = h;
  AttemptLog nextWake(h);
  nextWake.initialize();
  nextWake.recordBattery(ErrorCode::BatteryLow, firstTimestamp + 1800);
  nextWake.finishAttempt();
  CHECK(unchanged(h, before));
}

void wifi_statuses_are_excluded_but_http_and_json_are_not()
{
  CHECK(wifiRetrySeconds == 300);
  // wl_status_t is 0..6, with WL_NO_SHIELD = 255.
  for (int status = 0; status <= 6; ++status)
    CHECK(isWiFiApiStatus(-512 - status));
  CHECK(isWiFiApiStatus(-512 - 255));
  for (const int status : {0, 200, 401, 429, 500, -1, -11, -257, -263})
    CHECK(!isWiFiApiStatus(status));
}

void timestamps_never_masquerade_as_uptime()
{
  CHECK(unixTimestamp(0) == 0);
  CHECK(unixTimestamp(-1) == 0);
  CHECK(unixTimestamp(1483228799) == 0);
  CHECK(unixTimestamp(1483228800) == 1483228800);
  CHECK(unixTimestamp(firstTimestamp) == firstTimestamp);
  CHECK(unixTimestamp(UINT32_MAX) == UINT32_MAX);
  CHECK(unixTimestamp(int64_t(UINT32_MAX) + 1) == 0);
}

void boot_output_and_public_api_use_the_retained_history()
{
  initializeUpdateFailureLog();
  CHECK(Serial.output == "RTC error history v1: 0/10 (oldest first)\n");
  fakeNow = 0;
  recordUpdateFailure(ErrorCode::TimeSync);
  finishUpdateAttempt();
  fakeNow = firstTimestamp;
  recordUpdateFailure(ErrorCode::OneCallApi, 401);
  fakeNow += 40;
  recordUpdateFailure(ErrorCode::DisplayUpdate);
  finishUpdateAttempt();
  const auto beforeBoot = getUpdateFailureHistory();
  Serial.output.clear();
  initializeUpdateFailureLog();
  CHECK(unchanged(getUpdateFailureHistory(), beforeBoot));
  CHECK(Serial.output ==
    "RTC error history v1: 2/10 (oldest first)\n"
    "RTC_ERROR timestamp=0 codes=0x0008 detail=0\n"
    "RTC_ERROR timestamp=1791600000 codes=0x0110 detail=401\n");

  recordBatteryFailure(ErrorCode::BatteryLow);
  finishUpdateAttempt();
  const auto beforeBatteryRepeat = getUpdateFailureHistory();
  initializeUpdateFailureLog();
  recordBatteryFailure(ErrorCode::BatteryVeryLow);
  finishUpdateAttempt();
  CHECK(unchanged(getUpdateFailureHistory(), beforeBatteryRepeat));
  clearBatteryFailureState();
  recordBatteryFailure(ErrorCode::BatteryVeryLow);
  finishUpdateAttempt();
  CHECK(getUpdateFailureHistory().count == beforeBatteryRepeat.count + 1);
}

int main()
{
  cold_start_and_invalid_headers();
  only_last_ten_failures_survive_wakes_and_successes();
  multiple_causes_take_one_slot();
  battery_logged_once_until_recovery_even_if_display_fails();
  battery_dedup_survives_eviction();
  wifi_statuses_are_excluded_but_http_and_json_are_not();
  timestamps_never_masquerade_as_uptime();
  boot_output_and_public_api_use_the_retained_history();
  std::cout << "RTC error history: 8 scenarios passed\n";
}
