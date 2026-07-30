#include "adc_bsp.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>

static adc_cali_handle_t calibration;
static adc_oneshot_unit_handle_t adc1;

void Adc_PortInit() {
  adc_cali_curve_fitting_config_t cali = {};
  cali.unit_id = ADC_UNIT_1;
  cali.atten = ADC_ATTEN_DB_12;
  cali.bitwidth = ADC_BITWIDTH_12;
  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali, &calibration));
  adc_oneshot_unit_init_cfg_t unit = {};
  unit.unit_id = ADC_UNIT_1;
  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &adc1));
  adc_oneshot_chan_cfg_t channel = {};
  channel.bitwidth = ADC_BITWIDTH_12;
  channel.atten = ADC_ATTEN_DB_12;
  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1, ADC_CHANNEL_3, &channel));
}

float Adc_GetBatteryVoltage(int *raw) {
  int value = 0, millivolts = 0;
  if (adc_oneshot_read(adc1, ADC_CHANNEL_3, &value) != ESP_OK) return 0;
  adc_cali_raw_to_voltage(calibration, value, &millivolts);
  if (raw) *raw = value;
  return millivolts * 0.003f;
}

uint8_t Adc_GetBatteryLevel() {
  const float voltage = Adc_GetBatteryVoltage();
  if (voltage <= 3.0f) return 0;
  if (voltage >= 4.12f) return 100;
  return static_cast<uint8_t>((voltage - 3.0f) * 100.0f / 1.12f);
}
