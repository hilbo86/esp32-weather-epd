#pragma once

#include "BusyPhaseMonitor.h"

struct EpdBusyConfig
{
  uint32_t glitch_us;
  epd::BusyWindow reset;
  epd::BusyWindow power_on;
  epd::BusyWindow refresh;
  epd::BusyWindow power_off;
  uint8_t additional_attempts;
};
