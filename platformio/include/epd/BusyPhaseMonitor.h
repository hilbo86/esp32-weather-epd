#pragma once

#include <cstdint>
#ifdef ESP32
#include <esp_attr.h>
#define EPD_BUSY_IRAM IRAM_ATTR
#else
#define EPD_BUSY_IRAM
#endif

namespace epd
{
// Absolute assertion/phase deadlines never move. min_us is the minimum
// duration of a qualified BUSY interval, including ignored HIGH glitches.
struct BusyWindow
{
  uint32_t assert_timeout_us;
  uint32_t min_us;
  uint32_t max_us;
};

enum class BusyResult : uint8_t { Running, Ok, MissingBusy, Timeout, InvalidConfig };

struct BusyStats
{
  BusyResult result;
  uint32_t elapsed_us;
  uint32_t low_glitches;
  uint32_t high_glitches;
  uint32_t early_ends;
};

// Caller serializes edge() and poll(). No allocation or logging in interrupt context.
class BusyPhaseMonitor
{
public:
  void begin(uint32_t now, bool active, BusyWindow window, uint32_t glitch_us)
  {
    _start = now;
    _window = window;
    _glitch = glitch_us;
    _active = active;
    _level_since = 0;
    _have_edge = false;
    _valid_active = false;
    _ever_active = false;
    _busy_since = 0;
    _stats = {BusyResult::Running, 0, 0, 0, 0};
    if (!glitch_us || window.min_us < glitch_us ||
        window.min_us >= window.max_us || !window.assert_timeout_us ||
        window.assert_timeout_us >= window.max_us || window.max_us >= 0x80000000U)
      _stats.result = BusyResult::InvalidConfig;
  }

  void EPD_BUSY_IRAM edge(uint32_t now, bool active)
  {
    poll(now);
    if (_stats.result != BusyResult::Running || active == _active) return;
    const uint32_t elapsed = now - _start;
    const uint32_t width = elapsed - _level_since;
    if (_active)
    {
      if (width < _glitch) ++_stats.low_glitches;
    }
    else if (_have_edge && width < _glitch)
    {
      ++_stats.high_glitches;
      // Keep a qualified BUSY interval across a short HIGH interruption.
    }
    _active = active;
    _level_since = elapsed;
    _have_edge = true;
    poll(now);
  }

  void EPD_BUSY_IRAM poll(uint32_t now)
  {
    if (_stats.result != BusyResult::Running) return;
    const uint32_t elapsed = now - _start; // unsigned subtraction also handles micros() wrap
    const uint32_t width = elapsed - _level_since;
    _stats.elapsed_us = elapsed;
    if (_active && width >= _glitch)
    {
      if (!_valid_active) _busy_since = _level_since;
      _valid_active = true;
      _ever_active = true;
    }
    if (!_active && _valid_active && width >= _glitch)
    {
      const uint32_t busy_duration = _level_since - _busy_since;
      if (busy_duration >= _window.min_us &&
          _level_since <= _window.max_us - _glitch)
      {
        _stats.result = BusyResult::Ok;
        _stats.elapsed_us = _level_since + _glitch;
        return;
      }
      if (busy_duration < _window.min_us)
      {
        ++_stats.early_ends;
        _valid_active = false;
        // An early completion cannot turn into success just by waiting until min_us.
        // A subsequent valid BUSY interval can still complete within the original window.
      }
    }
    if (elapsed >= _window.max_us) _stats.result = BusyResult::Timeout;
    else if (!_ever_active && elapsed >= _window.assert_timeout_us &&
             !(_active && _level_since < _window.assert_timeout_us && width < _glitch))
      _stats.result = BusyResult::MissingBusy;
  }

  BusyStats stats() const { return _stats; }

private:
  BusyWindow _window = {};
  BusyStats _stats = {};
  uint32_t _start = 0, _level_since = 0, _busy_since = 0, _glitch = 100;
  bool _active = false, _have_edge = false, _valid_active = false, _ever_active = false;
};
} // namespace epd
