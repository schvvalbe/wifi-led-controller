#pragma once
#include <Arduino.h>

namespace Board {
// GPIO numbers, NOT NodeMCU Dx labels. Matches the working 0.0.0.7 firmware.
constexpr uint8_t Red = 4;
constexpr uint8_t Green = 12;
constexpr uint8_t Blue = 14;
constexpr uint8_t Motion1 = 13;
constexpr uint8_t Motion2 = 5;
constexpr uint8_t Light = A0;
constexpr bool MotionActiveHigh = true;
constexpr bool PwmActiveHigh = true;
constexpr uint16_t PwmRange = 1023;
constexpr uint32_t SensorWarmupMs = 30000;
}
