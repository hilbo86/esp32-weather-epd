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

#ifndef _RobustGxEPD2_730c_H_
#define _RobustGxEPD2_730c_H_

#include <GxEPD2_EPD.h>
#include "BusyConfig.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// Local panel driver based on GxEPD2 1.6.8 (Jean-Marc Zingg, GPL-3.0).
// SPI commands and pixel encoding follow upstream; BUSY validation/recovery is local.

class RobustGxEPD2_730c : public GxEPD2_EPD
{
  public:
    // attributes
    static const uint16_t WIDTH = 800;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 480;
    static const GxEPD2::Panel panel = GxEPD2::GDEY073D46;
    static const bool hasColor = true;
    static const bool hasPartialUpdate = true;
    static const bool usePartialUpdate = false; // force full screen update, else looks horrible
    static const bool hasFastPartialUpdate = false;
    static const uint16_t power_on_time = 200; // ms, e.g. 172000us
    static const uint16_t power_off_time = 150; // ms, e.g. 145000us
    static const uint16_t full_refresh_time = 40000; // ms, e.g. 38858000us
    static const uint16_t partial_refresh_time = 40000; // ms, e.g. 38858000us
    // constructor
    RobustGxEPD2_730c(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    void beginUpdate(const EpdBusyConfig& config);
    bool prepareUpdate(); // RST + initialization; shares one retry budget with later phases
    bool healthy() const { return !_failed; }
    uint16_t attempts() const { return _attempts; }
    // methods (virtual)
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    void clearScreen(uint8_t value = 0xFF); // init controller memory and screen (default white)
    void clearScreen(uint8_t black_value, uint8_t color_value); // init controller memory and screen
    void writeScreenBuffer(uint8_t value = 0xFF); // init controller memory (default white)
    void writeScreenBuffer(uint8_t black_value, uint8_t color_value); // init controller memory
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeNativePart(const uint8_t* data1, const uint8_t* data2, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeDemoBitmap(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, int16_t mode = 0, bool mirror_y = false, bool pgm = false);
    void drawDemoBitmap(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, int16_t mode = 0, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false); // screen refresh from controller memory to full screen
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h); // screen refresh from controller memory, partial screen
    void powerOff(); // turns off generation of panel driving voltages, avoids screen fading over time
    void hibernate(); // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)
  private:
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    uint8_t _colorOfDemoBitmap(uint8_t from, int16_t mode = 0);
    void _PowerOn();
    void _PowerOff();
    bool _InitDisplay();
    void _beginBusyPhase(epd::BusyWindow window);
    bool _finishBusyPhase(const char* label);
    static void IRAM_ATTR _busyEdge(void* context);
    EpdBusyConfig _busy_config = {};
    epd::BusyPhaseMonitor _busy_monitor;
    portMUX_TYPE _busy_mux = portMUX_INITIALIZER_UNLOCKED;
    bool _capture_enabled = false;
    bool _failed = false;
    uint16_t _attempts = 0;
};

#endif
