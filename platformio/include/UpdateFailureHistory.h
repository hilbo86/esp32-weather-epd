#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace update_failure
{
// Stable bit values for the app. Multiple causes belong to ONE update attempt.
enum class ErrorCode : uint16_t
{
  BatteryLow       = 0x0001,
  BatteryVeryLow   = 0x0002,
  BatteryCritical  = 0x0004,
  TimeSync         = 0x0008,
  OneCallApi       = 0x0010,
  AirPollutionApi  = 0x0020,
  SensorInit       = 0x0040,
  SensorRead       = 0x0080,
  DisplayUpdate    = 0x0100,
  AlertAllocation  = 0x0200
};

constexpr uint8_t capacity = 10;
constexpr uint32_t formatMagic = 0x55460001; // "UF", format version 1
constexpr uint64_t wifiRetrySeconds = 5 * 60;

struct Record
{
  uint32_t timestamp; // Unix seconds, UTC; 0 = time not yet known
  uint16_t codes;     // OR of ErrorCode values
  int16_t detail;     // Original HTTP/HTTPClient/JSON status for an API failure
};

// Plain data only: safe for RTC_NOINIT_ATTR, with no constructor on wake-up.
struct History
{
  uint32_t magic;
  uint8_t next;
  uint8_t count;
  uint8_t batteryErrorActive;
  uint8_t reserved;
  Record records[capacity];
};

static_assert(sizeof(Record) == 8, "App error record layout changed");
static_assert(sizeof(History) == 88, "RTC history layout changed");
static_assert(std::is_trivial<History>::value, "RTC history must be plain data");

// Match Arduino's getLocalTime validity threshold; never store an uptime as UTC.
inline uint32_t unixTimestamp(int64_t now)
{
  return now >= 1483228800LL && now <= UINT32_MAX
           ? static_cast<uint32_t>(now) : 0;
}

// client_utils.cpp encodes a lost Wi-Fi connection as -512 - wl_status_t.
inline bool isWiFiApiStatus(int status)
{
  return status <= -512;
}

class AttemptLog
{
public:
  explicit AttemptLog(History& history) : history_(history) {}

  void initialize()
  {
    if (history_.magic != formatMagic || history_.next >= capacity ||
        history_.count > capacity || history_.batteryErrorActive > 1)
    {
      history_ = {};
      history_.magic = formatMagic;
    }
    pending_ = {};
  }

  void record(ErrorCode code, uint32_t timestamp, int16_t detail = 0)
  {
    if (!pending_.codes) pending_.timestamp = timestamp;
    pending_.codes |= static_cast<uint16_t>(code);
    if (code == ErrorCode::OneCallApi || code == ErrorCode::AirPollutionApi)
      pending_.detail = detail;
  }

  void recordBattery(ErrorCode code, uint32_t timestamp)
  {
    // Independent of the NVS flag that tracks whether the battery screen was
    // shown. Failed display retries must not duplicate the battery error.
    if (history_.batteryErrorActive || (pending_.codes & batteryMask)) return;
    record(code, timestamp);
  }

  void batteryRecovered()
  {
    if (history_.batteryErrorActive) history_.batteryErrorActive = 0;
  }

  void finishAttempt()
  {
    if (!pending_.codes) return; // Successful/Wi-Fi-only attempts do not write RTC.
    history_.records[history_.next] = pending_;
    history_.next = (history_.next + 1) % capacity;
    if (history_.count < capacity) ++history_.count;
    if (pending_.codes & batteryMask) history_.batteryErrorActive = 1;
    pending_ = {};
  }

  const Record& oldestFirst(size_t index) const
  {
    return history_.records[(history_.next + capacity - history_.count + index)
                            % capacity];
  }

private:
  static constexpr uint16_t batteryMask = 0x0007;
  History& history_;
  Record pending_ = {}; // Normal SRAM; committed once immediately before sleep.
};
} // namespace update_failure
