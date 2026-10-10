// Display Library for SPI e-paper panels from Dalian Good Display and boards from Waveshare.
// Requires HW SPI and Adafruit_GFX. Caution: the e-paper panels require 3.3V supply AND data lines!
//
// based on Demo Example from Good Display: https://www.good-display.com/product/442.html
// Panel: GDEY073D46 : https://www.good-display.com/product/442.html
// Controller: unknown
//
// Author: Jean-Marc Zingg
//
// Version: see library.properties
//
// Library: https://github.com/ZinggJM/GxEPD2

#include "epd/RobustGxEPD2_730c.h"

RobustGxEPD2_730c::RobustGxEPD2_730c(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  GxEPD2_EPD(cs, dc, rst, busy, LOW, 60000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

void RobustGxEPD2_730c::beginUpdate(const EpdBusyConfig& config)
{
  _busy_config = config;
  _attempts = 0;
  _failed = false;
}

bool RobustGxEPD2_730c::prepareUpdate()
{
  if (_rst < 0 || _busy < 0)
  {
    Serial.println("EPD recovery requires RST and BUSY pins");
    _failed = true;
    return false;
  }
  const uint16_t total = uint16_t(_busy_config.additional_attempts) + 1;
  while (_attempts < total)
  {
    ++_attempts;
    Serial.printf("EPD attempt %u/%u: hardware reset and full initialization\n", _attempts, total);
    _failed = false;
    _init_display_done = false;
    _initial_write = true;
    _hibernating = false;
    _power_is_on = false;
    digitalWrite(_cs, HIGH);
    if (_InitDisplay()) return true;
  }
  _failed = true;
  Serial.printf("EPD attempts exhausted (%u); aborting update\n", total);
  return false;
}

void IRAM_ATTR RobustGxEPD2_730c::_busyEdge(void* context)
{
  auto* self = static_cast<RobustGxEPD2_730c*>(context);
  portENTER_CRITICAL_ISR(&self->_busy_mux);
  if (self->_capture_enabled)
    self->_busy_monitor.edge(micros(), digitalRead(self->_busy) == self->_busy_level);
  portEXIT_CRITICAL_ISR(&self->_busy_mux);
}

void RobustGxEPD2_730c::_beginBusyPhase(epd::BusyWindow window)
{
  // Capture from BEFORE reset release / command transmission, including short pulses
  // during SPI transfers and while the waiting task yields to the scheduler.
  if (_busy < 0) return;
  attachInterruptArg(digitalPinToInterrupt(_busy), _busyEdge, this, CHANGE);
  portENTER_CRITICAL(&_busy_mux);
  _busy_monitor.begin(micros(), digitalRead(_busy) == _busy_level, window, _busy_config.glitch_us);
  _capture_enabled = true;
  portEXIT_CRITICAL(&_busy_mux);
}

bool RobustGxEPD2_730c::_finishBusyPhase(const char* label)
{
  if (_busy < 0) { _failed = true; return false; }
  epd::BusyStats stats;
  for (;;)
  {
    portENTER_CRITICAL(&_busy_mux);
    _busy_monitor.poll(micros());
    stats = _busy_monitor.stats();
    if (stats.result != epd::BusyResult::Running) _capture_enabled = false;
    portEXIT_CRITICAL(&_busy_mux);
    if (stats.result != epd::BusyResult::Running) break;
    // Edges are timestamped by the ISR, even during this delay. No busy-loop for 35 s.
    if (_busy_callback) _busy_callback(_busy_callback_parameter);
    delay(1);
  }
  detachInterrupt(digitalPinToInterrupt(_busy));
  const char* result = "ok";
  if (stats.result == epd::BusyResult::MissingBusy) result = "no valid BUSY assertion";
  else if (stats.result == epd::BusyResult::Timeout) result = "no valid completion in window";
  else if (stats.result == epd::BusyResult::InvalidConfig) result = "invalid BUSY configuration";
  Serial.printf("%s : %lu us; %s; glitches LOW=%lu HIGH=%lu (<%lu us); early ends=%lu\n",
                label, (unsigned long)stats.elapsed_us, result,
                (unsigned long)stats.low_glitches, (unsigned long)stats.high_glitches,
                (unsigned long)_busy_config.glitch_us, (unsigned long)stats.early_ends);
  if (stats.result != epd::BusyResult::Ok) _failed = true;
  return !_failed;
}

void RobustGxEPD2_730c::clearScreen(uint8_t value)
{
  clearScreen(value, 0xFF);
}

void RobustGxEPD2_730c::clearScreen(uint8_t black_value, uint8_t color_value)
{
  writeScreenBuffer(black_value, color_value);
  refresh();
}

void RobustGxEPD2_730c::writeScreenBuffer(uint8_t value)
{
  writeScreenBuffer(value, 0xFF);
}

void RobustGxEPD2_730c::writeScreenBuffer(uint8_t black_value, uint8_t color_value)
{
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  _writeCommand(0x10);
  _startTransfer();
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 2; i++)
  {
    //_transfer(0x11);
    _transfer(0xFF == black_value ? 0x11 : black_value);
  }
  _endTransfer();
}

void RobustGxEPD2_730c::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  //Serial.print("writeImage("); Serial.print(x); Serial.print(", "); Serial.print(y); Serial.print(", ");
  //Serial.print(w); Serial.print(", "); Serial.print(h); Serial.println(")");
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  if (_initial_write) writeScreenBuffer();
  int16_t wb = (w + 7) / 8; // width bytes, bitmaps are padded
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(0x10);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      // use wb, h of bitmap for index!
      int16_t idx = mirror_y ? j + dx / 8 + ((h - 1 - (i + dy))) * wb : j + dx / 8 + (i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      for (int16_t k = 0; k < 4; k++)
      {
        uint8_t data2 = (data & 0x80 ? 0x10 : 0x00) | (data & 0x40 ? 0x01 : 0x00);
        data <<= 2;
        _transfer(data2);
      }
    }
  }
  _endTransfer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void RobustGxEPD2_730c::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!black && !color) return;
  if (!color) return writeImage(black, x, y, w, h, invert, mirror_y, pgm);
  //Serial.print("writeImage_3c("); Serial.print(x); Serial.print(", "); Serial.print(y); Serial.print(", ");
  //Serial.print(w); Serial.print(", "); Serial.print(h); Serial.println(")");
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  if (_initial_write) writeScreenBuffer();
  int16_t wb = (w + 7) / 8; // width bytes, bitmaps are padded
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(0x10);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t black_data = 0xFF, color_data = 0xFF;
      // use wb, h of bitmap for index!
      uint32_t idx = mirror_y ? j + dx / 8 + uint32_t((h - 1 - (i + dy))) * wb : j + dx / 8 + uint32_t(i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        black_data = pgm_read_byte(&black[idx]);
        color_data = pgm_read_byte(&color[idx]);
#else
        black_data = black[idx];
        color_data = color[idx];
#endif
      }
      else
      {
        black_data = black[idx];
        color_data = color[idx];
      }
      if (invert)
      {
        black_data = ~black_data;
        color_data = ~color_data;
      }
      for (int16_t k = 0; k < 4; k++)
      {
        uint8_t out_data = 0x00;
        for (int16_t l = 0; l < 2; l++)
        {
          out_data <<= 4;
          if (!(color_data & 0x80)) out_data |= 0x04;
          else out_data |= black_data & 0x80 ? 0x01 : 0x00;
          black_data <<= 1;
          color_data <<= 1;
        }
        _transfer(out_data);
      }
    }
  }
  _endTransfer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void RobustGxEPD2_730c::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  //Serial.print("writeImagePart("); Serial.print(x_part); Serial.print(", "); Serial.print(y_part); Serial.print(", ");
  //Serial.print(w_bitmap); Serial.print(", "); Serial.print(h_bitmap); Serial.print(", ");
  //Serial.print(x); Serial.print(", "); Serial.print(y); Serial.print(", ");
  //Serial.print(w); Serial.print(", "); Serial.print(h); Serial.println(")");
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  if (_initial_write) writeScreenBuffer();
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  int16_t wb_bitmap = (w_bitmap + 7) / 8; // width bytes, bitmaps are padded
  x_part -= x_part % 8; // byte boundary
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w; // limit
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h; // limit
  // make x, w multiple of 8
  w += x % 8; // adjust for byte boundary of x
  x -= x % 8; // byte boundary ox
  w = 8 * ((w + 7) / 8); // byte boundary, bitmaps are padded
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(0x10);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      // use wb_bitmap, h_bitmap of bitmap for index!
      uint32_t idx = mirror_y ? x_part / 8 + j + dx / 8 + uint32_t((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 8 + j + dx / 8 + uint32_t(y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      for (int16_t k = 0; k < 4; k++)
      {
        uint8_t data2 = (data & 0x80 ? 0x10 : 0x00) | (data & 0x40 ? 0x01 : 0x00);
        data <<= 2;
        _transfer(data2);
      }
    }
  }
  _endTransfer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void RobustGxEPD2_730c::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  //Serial.print("writeImagePart_3c("); Serial.print(x_part); Serial.print(", "); Serial.print(y_part); Serial.print(", ");
  //Serial.print(w_bitmap); Serial.print(", "); Serial.print(h_bitmap); Serial.print(", ");
  //Serial.print(x); Serial.print(", "); Serial.print(y); Serial.print(", ");
  //Serial.print(w); Serial.print(", "); Serial.print(h); Serial.println(")");
  if (!black && !color) return;
  if (!color) return writeImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  if (_initial_write) writeScreenBuffer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  int16_t wb_bitmap = (w_bitmap + 7) / 8; // width bytes, bitmaps are padded
  x_part -= x_part % 8; // byte boundary
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w; // limit
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h; // limit
  // make x, w multiple of 8
  w += x % 8; // adjust for byte boundary of x
  x -= x % 8; // byte boundary ox
  w = 8 * ((w + 7) / 8); // byte boundary, bitmaps are padded
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(0x10);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t black_data = 0xFF, color_data = 0xFF;
      // use wb_bitmap, h_bitmap of bitmap for index!
      uint32_t idx = mirror_y ? x_part / 8 + j + dx / 8 + uint32_t((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 8 + j + dx / 8 + uint32_t(y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        black_data = pgm_read_byte(&black[idx]);
        color_data = pgm_read_byte(&color[idx]);
#else
        black_data = black[idx];
        color_data = color[idx];
#endif
      }
      else
      {
        black_data = black[idx];
        color_data = color[idx];
      }
      if (invert)
      {
        black_data = ~black_data;
        color_data = ~color_data;
      }
      for (int16_t k = 0; k < 4; k++)
      {
        uint8_t out_data = 0x00;
        for (int16_t l = 0; l < 2; l++)
        {
          out_data <<= 4;
          if (!(color_data & 0x80)) out_data |= 0x04;
          else out_data |= black_data & 0x80 ? 0x01 : 0x00;
          black_data <<= 1;
          color_data <<= 1;
        }
        _transfer(out_data);
      }
    }
  }
  _endTransfer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void RobustGxEPD2_730c::writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!data1) return;
  //Serial.print("writeNative("); Serial.print(x); Serial.print(", "); Serial.print(y); Serial.print(", ");
  //Serial.print(w); Serial.print(", "); Serial.print(h); Serial.println(")");
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  if (_initial_write) writeScreenBuffer();
  uint32_t wb = (w + 1) / 2; // width bytes, bitmaps are padded
  x -= x % 2; // byte boundary
  w = wb * 2; // byte boundary
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(0x10);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 2; j++)
    {
      uint8_t data;
      // use wb, h of bitmap for index!
      uint32_t idx = mirror_y ? j + dx / 2 + uint32_t((h - 1 - (i + dy))) * wb : j + dx / 2 + uint32_t(i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&data1[idx]);
#else
        data = data1[idx];
#endif
      }
      else
      {
        data = data1[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void RobustGxEPD2_730c::writeNativePart(const uint8_t* data1, const uint8_t* data2, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (!data1) return;
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  if (_initial_write) writeScreenBuffer();
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  int16_t wb_bitmap = (w_bitmap + 1) / 2; // width bytes, bitmaps are padded
  x_part -= x_part % 2; // byte boundary
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w; // limit
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h; // limit
  w += x % 2; // adjust for byte boundary of x
  x -= x % 2; // byte boundary
  w = 2 * ((w + 1) / 2); // byte boundary, bitmaps are padded
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(0x10);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 2; j++)
    {
      uint8_t data;
      // use wb_bitmap, h_bitmap of bitmap for index!
      uint32_t idx = mirror_y ? x_part / 2 + j + dx / 2 + uint32_t((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 2 + j + dx / 2 + uint32_t(y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&data1[idx]);
#else
        data = data1[idx];
#endif
      }
      else
      {
        data = data1[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void RobustGxEPD2_730c::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void RobustGxEPD2_730c::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void RobustGxEPD2_730c::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(black, color, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void RobustGxEPD2_730c::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(black, color, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void RobustGxEPD2_730c::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeNative(data1, data2, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
}

void RobustGxEPD2_730c::refresh(bool partial_update_mode)
{
  if (_failed) return;
  _PowerOn();
  if (_failed) return;
  _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(0x50);
  _writeData(0x3F);
  _beginBusyPhase(_busy_config.refresh);
  _writeCommand(0x12); // Display Refresh
  _writeData(0x00);
  _finishBusyPhase("_refresh");
}

void RobustGxEPD2_730c::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (_failed) return;
  _PowerOn();
  if (_failed) return;
  if (usePartialUpdate) _setPartialRamArea(x, y, w, h);
  else _setPartialRamArea(0, 0, WIDTH, HEIGHT);
  _writeCommand(0x50);
  _writeData(0x3F); // border white, no floating?
  _beginBusyPhase(_busy_config.refresh);
  _writeCommand(0x12); // Display Refresh
  _writeData(0x00);
  _finishBusyPhase("_refresh");
}

void RobustGxEPD2_730c::powerOff()
{
  _PowerOff();
}

void RobustGxEPD2_730c::hibernate()
{
  if (_failed) return;
  _PowerOff();
  if (_failed) return;
  if (_rst >= 0)
  {
    _writeCommand(0x07); // deep sleep
    _writeData(0xA5);    // control code
    _hibernating = true;
    _init_display_done = false;
  }
}

void RobustGxEPD2_730c::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  //Serial.print("_setPartialRamArea("); Serial.print(x); Serial.print(", "); Serial.print(y); Serial.print(", ");
  //Serial.print(w); Serial.print(", "); Serial.print(h); Serial.println(")");
  uint16_t xe = x + w - 1;
  uint16_t ye = y + h; // controller issue (one more)
  _writeCommand(0x83); // partial window
  _writeData(x / 256);
  _writeData(x % 256);
  _writeData(xe / 256);
  _writeData(xe % 256);
  _writeData(y / 256);
  _writeData(y % 256);
  _writeData(ye / 256);
  _writeData(ye % 256);
  _writeData(0x01);
}

void RobustGxEPD2_730c::_PowerOn()
{
  if (_failed) return;
  if (!_power_is_on)
  {
    _beginBusyPhase(_busy_config.power_on);
    _writeCommand(0x04);
    if (!_finishBusyPhase("_PowerOn")) return;
  }
  _power_is_on = true;
}

void RobustGxEPD2_730c::_PowerOff()
{
  if (_failed) return;
  if (_power_is_on)
  {
    _beginBusyPhase(_busy_config.power_off);
    _writeCommand(0x02);
    _writeData(0x00);
    if (!_finishBusyPhase("_PowerOff")) return;
  }
  _power_is_on = false;
}

bool RobustGxEPD2_730c::_InitDisplay()
{
  if (_failed) return false;
  if ((_rst >= 0) && (_hibernating || _initial_write))
  {
    pinMode(_rst, OUTPUT); // just in case
    digitalWrite(_rst, HIGH);
    delay(50); // needs a little longer
    digitalWrite(_rst, LOW);
    delay(20);
    _beginBusyPhase(_busy_config.reset);
    digitalWrite(_rst, HIGH);
    if (!_finishBusyPhase("_InitDisplay reset")) return false;
    _initial_write = false; // used for initial reset done
    _hibernating = false;
    _power_is_on = false;
  }
  _writeCommand(0xAA); // CMDH
  _writeData(0x49);
  _writeData(0x55);
  _writeData(0x20);
  _writeData(0x08);
  _writeData(0x09);
  _writeData(0x18);
  _writeCommand(0x01); // PWRR
  _writeData(0x3F);
  _writeData(0x00);
  _writeData(0x32);
  _writeData(0x2A);
  _writeData(0x0E);
  _writeData(0x2A);
  _writeCommand(0x00); // PSR
  _writeData(0x5F);
  _writeData(0x69);
  _writeCommand(0x03); // POFS
  _writeData(0x00);
  _writeData(0x54);
  _writeData(0x00);
  _writeData(0x44);
  _writeCommand(0x05); // BTST1
  _writeData(0x40);
  _writeData(0x1F);
  _writeData(0x1F);
  _writeData(0x2C);
  _writeCommand(0x06); // BTST2
  _writeData(0x6F);
  _writeData(0x1F);
  _writeData(0x16);
  _writeData(0x25);
  _writeCommand(0x08); // BTST3
  _writeData(0x6F);
  _writeData(0x1F);
  _writeData(0x1F);
  _writeData(0x22);
  _writeCommand(0x13); // IPC
  _writeData(0x00);
  _writeData(0x04);
  _writeCommand(0x30); // PLL
  _writeData(0x02);
  _writeCommand(0x41); // TSE
  _writeData(0x00);
  _writeCommand(0x50); // CDI
  _writeData(0x3F);
  _writeCommand(0x60); // TCON
  _writeData(0x02);
  _writeData(0x00);
  _writeCommand(0x61); // TRES
  _writeData(0x03);
  _writeData(0x20);
  _writeData(0x01);
  _writeData(0xE0);
  _writeCommand(0x82); // VDCS
  _writeData(0x1E);
  _writeCommand(0x84); // T_VDCS
  _writeData(0x00);
  _writeCommand(0x86); // AGID
  _writeData(0x00);
  _writeCommand(0xE3); // PWS
  _writeData(0x2F);
  _writeCommand(0xE0); // CCSET
  _writeData(0x00);
  _writeCommand(0xE6); // TSSET
  _writeData(0x00);
  _PowerOn();
  if (_failed) return false;
  _init_display_done = true;
  return true;
}

uint8_t RobustGxEPD2_730c::_colorOfDemoBitmap(uint8_t from, int16_t mode)
{
  switch (mode)
  {
    case 0:
      {
        switch (from)
        {
          case 0xFF: return (0x01); // white;
          case 0xFC: return (0x05); // yellow;
          case 0xF1: return (0x06); // orange;
          case 0xE5: return (0x04); // red;
          case 0x4B: return (0x03); // blue;
          case 0x39: return (0x02); // green;
          case 0x00: return (0x00); // black;
          default: return (0x01); // white;
        }
      }
    case 1:
      {
        switch (from)
        {
          case 0x00: return (0x00); // black;
          case 0xff: return (0x01); // white;
          case 0xfc: return (0x05); // yellow;
          case 0xE0: return (0x04); // red;
          case 0x03: return (0x03); // blue;
          case 0x1c: return (0x02); // green;
          default: return (0x01); // white;
        }
      }
    default: return (0x01); // white;
  }
}

void RobustGxEPD2_730c::writeDemoBitmap(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, int16_t mode, bool mirror_y, bool pgm)
{
  (void) mode;
  if (!data1) return;
  //Serial.print("writeDemoBitmap("); Serial.print(x); Serial.print(", "); Serial.print(y); Serial.print(", ");
  //Serial.print(w); Serial.print(", "); Serial.print(h); Serial.println(")");
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if (_failed || (!_init_display_done && !_InitDisplay())) return;
  if (_initial_write) writeScreenBuffer();
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(0x10);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1; j += 2)
    {
      uint8_t data = 0x11;
      uint32_t idx = mirror_y ? j + dx + uint32_t((h - 1 - (i + dy))) * w : j + dx + uint32_t(i + dy) * w;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = _colorOfDemoBitmap(pgm_read_byte(&data1[idx++]), mode) << 4;
        data |= _colorOfDemoBitmap(pgm_read_byte(&data1[idx]), mode);
#else
        data = _colorOfDemoBitmap(data1[idx++], mode) << 4;
        data |= _colorOfDemoBitmap(data1[idx], mode);
#endif
      }
      else
      {
        data = _colorOfDemoBitmap(data1[idx++], mode) << 4;
        data |= _colorOfDemoBitmap(data1[idx], mode);
      }
      _transfer(data);
    }
  }
  _endTransfer();
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void RobustGxEPD2_730c::drawDemoBitmap(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, int16_t mode, bool mirror_y, bool pgm)
{
  writeDemoBitmap(data1, data2, x, y, w, h, mode, mirror_y, pgm);
  refresh(x, y, w, h);
}
