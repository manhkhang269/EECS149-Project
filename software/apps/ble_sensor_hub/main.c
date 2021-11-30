// Controller Firmware

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "app_util.h"
#include "nrf_twi_mngr.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "display.h"

#include "simple_ble.h"
#include "nrfx_saadc.h"

#include "max30102.h"

APP_TIMER_DEF(upd_timer);

#define ADC_CHN_GSR  0
#define ADC_CHN_FLEX 1
#define ADC_CHN_EMG1 2
#define ADC_CHN_EMG2 3

#define REFRESH_INTERVAL_MS 200

NRF_TWI_MNGR_DEF(twi_mngr_instance, 5, 0);

// Intervals for advertising and connections
static simple_ble_config_t ble_config = {
        // c0:98:e5:49:xx:xx
        .platform_id       = 0x49,    // used as 4th octect in device BLE address
        .device_id         = 0x2041,
        .adv_name          = "DataHub", // used in advertisements if there is room
        .adv_interval      = MSEC_TO_UNITS(500, UNIT_0_625_MS),
        .min_conn_interval = MSEC_TO_UNITS(50, UNIT_1_25_MS),
        .max_conn_interval = MSEC_TO_UNITS(500, UNIT_1_25_MS),
};

// 32e61089-2b22-4db5-a914-43ce41986c70
static simple_ble_service_t sensing_service = {{
    .uuid128 = {0x70,0x6C,0x98,0x41,0xCE,0x43,0x14,0xA9,
                0xB5,0x4D,0x22,0x2B,0x89,0x10,0xE6,0x32}
}};
// xxxx<xxxx> -xxxx-xxxx-xxxx-xxxxxxxxxxxx
static simple_ble_char_t cmd_char = {.uuid16 = 0x108a};
static uint8_t cmd = 0x00;
// MAX_RED
static simple_ble_char_t max_red_char = {.uuid16 = 0x108b};
static uint32_t max_red_val = 0;
// MAX_IR
static simple_ble_char_t max_ir_char = {.uuid16 = 0x108c};
static uint32_t max_ir_val = 0;
// GSR
static simple_ble_char_t gsr_char = {.uuid16 = 0x108d};
static uint16_t gsr_val = 0x00;
// Flex
static simple_ble_char_t flex_char = {.uuid16 = 0x108e};
static uint16_t flex_val = 0x00;
// EMG1
static simple_ble_char_t emg1_char = {.uuid16 = 0x108f};
static uint16_t emg1_val = 0x00;
// EMG2
static simple_ble_char_t emg2_char = {.uuid16 = 0x1090};
static uint16_t emg2_val = 0x00;

/*******************************************************************************
 *   State for this application
 ******************************************************************************/
// Main application state
simple_ble_app_t* simple_ble_app;

void ble_evt_write(ble_evt_t const* p_ble_evt) {
    if (simple_ble_is_char_event(p_ble_evt, &cmd_char)) {
      // write event handler
    }
}

void ble_evt_connected(ble_evt_t const* p_ble_evt) {
  app_timer_start(upd_timer, APP_TIMER_TICKS(REFRESH_INTERVAL_MS), NULL);
}

void ble_evt_disconnected(ble_evt_t const* p_ble_evt) {
  app_timer_stop(upd_timer);
}

void ble_error(uint32_t error_code) {
  printf("(BLE Stack) BLE Error: %zu\n", error_code);
}

uint16_t sample_value(uint8_t channel) {
  uint16_t val;
  APP_ERROR_CHECK(nrfx_saadc_sample_convert(channel, (nrf_saadc_value_t *) &val));
  return val;
}

void upd_callback() {
  printf("Polling...\n");
  //printf("MAXPoll: got %u sample(s)\n", max30102_refresh());
  //printf("Red: %lu, IR: %lu\n", getFIFORed(), getFIFOIR()); // INOP
  //max30102_fifo_next();
  max_red_val = 0; // getred
  simple_ble_notify_char(&max_red_char);
  max_ir_val = 0; // getir
  simple_ble_notify_char(&max_ir_char);
  gsr_val = sample_value(ADC_CHN_GSR);
  simple_ble_notify_char(&gsr_char);
  flex_val = sample_value(ADC_CHN_FLEX);
  simple_ble_notify_char(&flex_char);
  emg1_val = sample_value(ADC_CHN_EMG1);
  simple_ble_notify_char(&emg1_char);
  emg2_val = sample_value(ADC_CHN_EMG2);
  simple_ble_notify_char(&emg2_char);
  printf("End\n");
}

void saadc_callback (nrfx_saadc_evt_t const * p_event) {
  // don't care about adc callbacks
}

int main(void) {

  // Initialize
  ret_code_t error_code = NRF_SUCCESS;

  // initialize RTT library
  error_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(error_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();

  // adc init
  nrfx_saadc_config_t saadc_config = NRFX_SAADC_DEFAULT_CONFIG;
  saadc_config.resolution = NRF_SAADC_RESOLUTION_10BIT;
  error_code = nrfx_saadc_init(&saadc_config, saadc_callback);
  APP_ERROR_CHECK(error_code);

  // initialize analog inputs
  // configure with 0 as input pin for now
  nrf_saadc_channel_config_t channel_config = NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(0);
  channel_config.gain = NRF_SAADC_GAIN1_6; // input gain of 1/6 Volts/Volt, multiply incoming signal by (1/6)
  channel_config.reference = NRF_SAADC_REFERENCE_INTERNAL; // 0.6 Volt reference, input after gain can be 0 to 0.6 Volts

  // GSR
  channel_config.pin_p = NRF_SAADC_INPUT_AIN1;
  // modify config if necessary
  error_code = nrfx_saadc_channel_init(ADC_CHN_GSR, &channel_config);
  APP_ERROR_CHECK(error_code);

  // Flex
  channel_config.pin_p = NRF_SAADC_INPUT_AIN2;
  // modify config if necessary
  error_code = nrfx_saadc_channel_init(ADC_CHN_FLEX, &channel_config);
  APP_ERROR_CHECK(error_code);

  // EMG1
  channel_config.pin_p = NRF_SAADC_INPUT_AIN4;
  // modify config if necessary
  error_code = nrfx_saadc_channel_init(ADC_CHN_EMG1, &channel_config);
  APP_ERROR_CHECK(error_code);

  // EMG2
  channel_config.pin_p = NRF_SAADC_INPUT_AIN5;
  // modify config if necessary
  error_code = nrfx_saadc_channel_init(ADC_CHN_EMG2, &channel_config);
  APP_ERROR_CHECK(error_code);

  // i2c init
  nrf_drv_twi_config_t i2c_config = NRF_DRV_TWI_DEFAULT_CONFIG;
  i2c_config.scl = NRF_GPIO_PIN_MAP(0, 26);
  i2c_config.sda = NRF_GPIO_PIN_MAP(0, 27);
  i2c_config.frequency = NRF_TWIM_FREQ_400K;
  error_code = nrf_twi_mngr_init(&twi_mngr_instance, &i2c_config);
  APP_ERROR_CHECK(error_code);

  // Setup PO & HR
  max30102_config_t hr_config = {
    .adcRange = MAX30102_ADCRANGE_16384,
    .ledMode = MAX30102_MODE_REDIRONLY,
    .powerLvl = 0x1F, // should be 6.4mA
    .pulseWidth = MAX30102_PULSEWIDTH_118,
    .sampleAvg = MAX30102_SAMPLEAVG_4,
    .sampleRate = MAX30102_SAMPLERATE_100
  };

  //max30102_init(&twi_mngr_instance);
  //max30102_config(hr_config);

  // Setup BLE
  simple_ble_app = simple_ble_init(&ble_config);

  simple_ble_add_service(&sensing_service);

  simple_ble_add_characteristic(1, 1, 1, 0, sizeof(cmd), (uint8_t*)&cmd, &sensing_service, &cmd_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(max_red_val), (uint8_t*)&max_red_val, &sensing_service, &max_red_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(max_ir_val), (uint8_t*)&max_ir_val, &sensing_service, &max_ir_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(gsr_val), (uint8_t*)&gsr_val, &sensing_service, &gsr_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(flex_val), (uint8_t*)&flex_val, &sensing_service, &flex_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(emg1_val), (uint8_t*)&emg1_val, &sensing_service, &emg1_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(emg2_val), (uint8_t*)&emg2_val, &sensing_service, &emg2_char);

  // Start Advertising
  simple_ble_adv_only_name();

  app_timer_init();
  app_timer_create(&upd_timer, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t) upd_callback);

  while(true) {
    power_manage();
  }
}

