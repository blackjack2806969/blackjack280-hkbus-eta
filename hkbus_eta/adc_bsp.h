#pragma once

#include <Arduino.h>

void Adc_PortInit();
float Adc_GetBatteryVoltage(int *raw = nullptr);
uint8_t Adc_GetBatteryLevel();
