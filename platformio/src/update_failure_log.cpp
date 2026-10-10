#include "update_failure_log.h"

#include <Arduino.h>
#include <esp_attr.h>
#include <time.h>

namespace
{
// Retain history on software resets too; initialize() validates uninitialized
// RTC memory using the format marker and ring bounds before accessing entries.
RTC_NOINIT_ATTR update_failure::History history;
update_failure::AttemptLog attemptLog(history);

uint32_t currentTimestamp()
{
  // The system RTC keeps time across deep sleep, including battery/Wi-Fi wakes.
  // Do not wait for NTP or start Wi-Fi just to produce a log timestamp.
  return update_failure::unixTimestamp(time(nullptr));
}
}

void initializeUpdateFailureLog()
{
  attemptLog.initialize();
  printUpdateFailureLog();
}

void printUpdateFailureLog()
{
  Serial.printf("RTC error history v1: %u/%u (oldest first)\n",
                history.count, update_failure::capacity);
  for (uint8_t i = 0; i < history.count; ++i)
  {
    const auto& entry = attemptLog.oldestFirst(i);
    Serial.printf("RTC_ERROR timestamp=%lu codes=0x%04X detail=%d\n",
                  static_cast<unsigned long>(entry.timestamp), entry.codes,
                  entry.detail);
  }
}

const update_failure::History& getUpdateFailureHistory()
{
  return history;
}

void recordUpdateFailure(update_failure::ErrorCode code, int16_t detail)
{
  attemptLog.record(code, currentTimestamp(), detail);
}

void recordBatteryFailure(update_failure::ErrorCode code)
{
  attemptLog.recordBattery(code, currentTimestamp());
}

void clearBatteryFailureState()
{
  attemptLog.batteryRecovered();
}

void finishUpdateAttempt()
{
  attemptLog.finishAttempt();
}
