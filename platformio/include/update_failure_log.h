#pragma once

#include "UpdateFailureHistory.h"

// Called immediately after Serial.begin(), before any early exit from setup().
void initializeUpdateFailureLog();
void printUpdateFailureLog();
const update_failure::History& getUpdateFailureHistory();
void recordUpdateFailure(update_failure::ErrorCode code, int16_t detail = 0);
void recordBatteryFailure(update_failure::ErrorCode code);
void clearBatteryFailureState();
void finishUpdateAttempt();
