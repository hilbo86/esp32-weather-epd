#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#pragma GCC diagnostic ignored "-Wunused-parameter" // matches the real GxEPD2_EPD.h

#define IRAM_ATTR
constexpr int LOW = 0, HIGH = 1, OUTPUT = 1, CHANGE = 3;
uint32_t micros();
void delay(unsigned long ms);
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
inline int digitalPinToInterrupt(int pin) { return pin; }
void attachInterruptArg(int pin, void (*callback)(void*), void* arg, int mode);
void detachInterrupt(int pin);

namespace fake
{
  void command(uint8_t value);
  void data(uint8_t value);
  void transfer(uint8_t value);
}

struct FakeSerial
{
  std::string output;
  template<typename... Args> void printf(const char* format, Args... args)
  {
    char line[512];
    std::snprintf(line, sizeof(line), format, args...);
    output += line;
  }
  void println(const char* text) { output += std::string(text) + '\n'; }
};
extern FakeSerial Serial;

namespace GxEPD2 { enum Panel { GDEY073D46 }; }
// Hardware boundary only: the REAL local panel driver is compiled into these tests.
class GxEPD2_EPD
{
public:
  GxEPD2_EPD(int16_t cs, int16_t dc, int16_t rst, int16_t busy, int16_t level,
             uint32_t, uint16_t, uint16_t, GxEPD2::Panel, bool, bool, bool)
    : _cs(cs), _dc(dc), _rst(rst), _busy(busy), _busy_level(level) {}
protected:
  static uint16_t gx_uint16_min(uint16_t a, uint16_t b) { return a < b ? a : b; }
  void _writeCommand(uint8_t value) { fake::command(value); }
  void _writeData(uint8_t value) { fake::data(value); }
  void _startTransfer() {}
  void _transfer(uint8_t value) { fake::transfer(value); }
  void _endTransfer() {}
  int16_t _cs, _dc, _rst, _busy, _busy_level;
  bool _initial_write = true, _hibernating = false, _power_is_on = false, _init_display_done = false;
  void (*_busy_callback)(const void*) = nullptr;
  const void* _busy_callback_parameter = nullptr;
};
