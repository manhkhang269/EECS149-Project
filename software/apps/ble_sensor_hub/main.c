// Controller Firmware

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "app_util.h"
#include "nrf_twi_mngr.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_delay.h"
#include "nrf_log_default_backends.h"
#include "display.h"

#include "simple_ble.h"
#include "nrfx_saadc.h"
#include "nrf_drv_systick.h"

#include "max30102.h"
#include "algorithm.h"
#include "pd.h"

APP_TIMER_DEF(upd_timer);

#define ADC_CHN_GSR 0
#define ADC_CHN_FLEX 1
#define ADC_CHN_EMG1 2
#define ADC_CHN_EMG2 3
APP_TIMER_DEF(sample_upd);
APP_TIMER_DEF(hr_upd);
APP_TIMER_DEF(br_upd);
APP_TIMER_DEF(sr_upd);

#define ADC_CHN_GSR 0
#define ADC_CHN_FLEX 1
#define ADC_CHN_EMG1 2
#define ADC_CHN_EMG2 3
#define ADC_CHN_ACC 4

#define SAMPLE_INTERVAL_MS 20
#define HR_INTERVAL_MS 100
#define BR_INTERVAL_MS 800 // CHANGE THIS
#define SR_INTERVAL_MS 500 // CHANGE THIS
#define BPM_BUF_SIZE 4
#define BPM_ADJ_SIZE 3
#define HR_VAR_THRESHOLD 16

// Intervals for advertising and connections
static simple_ble_config_t ble_config = {
    // c0:98:e5:49:xx:xx
    .platform_id = 0x49, // used as 4th octect in device BLE address
    .device_id = 0x2041,
    .adv_name = "DataHub", // used in advertisements if there is room
    .adv_interval = MSEC_TO_UNITS(100, UNIT_0_625_MS),
    .min_conn_interval = MSEC_TO_UNITS(10, UNIT_1_25_MS),
    .max_conn_interval = MSEC_TO_UNITS(200, UNIT_1_25_MS),
};

uint32_t millis(void)
{
  return (app_timer_cnt_get() / 32.768);
}

uint16_t sample_value(uint8_t channel)
{
  uint16_t val;
  APP_ERROR_CHECK(nrfx_saadc_sample_convert(channel, (nrf_saadc_value_t *)&val));
  return val;
}

static bool alive = false;

static uint32_t startms;
static uint32_t currms;
static int same_peak = 0;
static int peak_high_count = 0;
static uint16_t rates[4]; //Array of step rate
static uint16_t rateSpot = 0;
static uint32_t lastStep = 0; //Time at which the last step occurred
static uint16_t stepAvg;
static float stepPerMinute;

typedef struct
{
  uint32_t max_red;
  uint32_t max_ir;
  uint16_t gsr;
  uint16_t flex;
  uint16_t emg1;
  uint16_t emg2;
} packet_t;

typedef struct
{
  uint16_t hr_bpm;  // heart rate
  uint16_t br_bpm;  // breath rate
  uint16_t sr1_bpm; // step rate
  uint16_t sr2_bpm;
} stat_packet_t;

// 32e61089-2b22-4db5-a914-43ce41986c70
static simple_ble_service_t sensing_service = {{.uuid128 = {0x70, 0x6C, 0x98, 0x41, 0xCE, 0x43, 0x14, 0xA9,
                                                            0xB5, 0x4D, 0x22, 0x2B, 0x89, 0x10, 0xE6, 0x32}}};
// xxxx<xxxx> -xxxx-xxxx-xxxx-xxxxxxxxxxxx
static simple_ble_char_t cmd_char = {.uuid16 = 0x108a};
static uint8_t cmd = 0x00;
// Telemetry
static simple_ble_char_t telemetry_char = {.uuid16 = 0x108b};
static packet_t buffer;

static simple_ble_char_t stat_char = {.uuid16 = 0x108c};
static stat_packet_t stat;

/*******************************************************************************
 *   State for this application
 ******************************************************************************/
// Main application state
simple_ble_app_t *simple_ble_app;

static uint32_t hr_lastbeat_ms = 0;

static uint16_t hr_bpmAvg;

static uint16_t hr_bpm;

static uint16_t hr_bpmbuf[BPM_BUF_SIZE];

static uint16_t hr_adjbuf[BPM_ADJ_SIZE];

static uint16_t hr_ref_ctr = 0;

static size_t hr_adjbuf_cnt = 0;

static size_t hr_bpmbufcnt = 0;

void hr_update()
{
  if (!alive)
    return;
  bool check = checkForBeat((int32_t)buffer.max_ir);
  printf("Currms: %lu, Beat check: %s\n", millis(), check ? "detected" : "not detected");
  if (buffer.max_ir > 50000 && check)
  {
    uint32_t t = millis();
    uint32_t hr_delta = t - hr_lastbeat_ms;
    hr_lastbeat_ms = t;
    hr_bpm = 60 / (hr_delta / 1000.0);

    if (hr_bpm < 300 && hr_bpm > 40)
    {
      if (hr_ref_ctr)
      {
        if (hr_bpm > hr_ref_ctr + HR_VAR_THRESHOLD || hr_bpm < hr_ref_ctr - HR_VAR_THRESHOLD)
        {
          hr_adjbuf[hr_adjbuf_cnt++] = hr_bpm;
          if (hr_adjbuf_cnt >= BPM_ADJ_SIZE)
          {
            hr_adjbuf_cnt = 0;
            hr_ref_ctr = 0;
            for (size_t x = 0; x < BPM_ADJ_SIZE; x++)
            {
              hr_ref_ctr += hr_adjbuf[x];
            }
            hr_ref_ctr /= BPM_ADJ_SIZE;
          }
          return;
        }
        else
        {
          hr_adjbuf_cnt = 0;
        }
      }
      hr_bpmbuf[hr_bpmbufcnt++] = hr_bpm;
      hr_bpmbufcnt %= BPM_BUF_SIZE;

      if (hr_bpmbufcnt % BPM_BUF_SIZE == 0)
      {
        hr_bpmAvg = 0;
        for (size_t x = 0; x < BPM_BUF_SIZE; x++)
        {
          hr_bpmAvg += hr_bpmbuf[x];
        }
        hr_bpmAvg /= BPM_BUF_SIZE;
        stat.hr_bpm = hr_bpmAvg;
        if (!hr_ref_ctr)
          hr_ref_ctr = hr_bpmAvg;
      }
    }
  }
}

void br_update()
{
  currms = millis();
  if (peak_high_count == 2)
  {
    if (currms - startms < 120)
    {
      same_peak = 1;
      peak_high_count = 0;
    }
  }
  else if (currms - startms > 120)
  {
    same_peak = 0;
    peak_high_count = 0;
  }
  add(buffer.flex);
  if (getPeak() == 1)
  {
    startms = currms;
    peak_high_count++;
  }
}

void sr_update()
{
  currms = millis();
  if (peak_high_count == 2)
  {
    if (currms - startms < 50)
    {
      same_peak = 1;
      peak_high_count = 0;
    }
  }
  else if (currms - startms > 50)
  {
    same_peak = 0;
    peak_high_count = 0;
  }

  add(buffer.gsr);
  int peak = getPeak();
  if (peak == 1)
  {
    startms = currms;
    peak_high_count++;
  }
  //Step per minute counting
  if (same_peak)
  {
    long delta = millis() - lastStep;
    lastStep = millis();
    stepPerMinute = 60 / (delta / 1000.0);
    if (stepPerMinute < 250 && stepPerMinute > 20)
    {
      rates[rateSpot++] = (uint16_t)stepPerMinute;
      rateSpot %= 4; //Wrap variable
      //Take average of readings
      stepAvg = 0;
      for (size_t x = 0; x < 4; x++)
        stepAvg += rates[x];
      stepAvg /= 4;
    }
  }
  printf("Currms: %lu, SP: %s, avgbpm: %d\n", currms, peak ? "yes" : "no", stepAvg);
}

void val_update()
{
  if (!alive)
    return;
  MAX30102_read_fifo(&buffer.max_red, &buffer.max_ir);
  buffer.gsr = sample_value(ADC_CHN_GSR);
  buffer.flex = sample_value(ADC_CHN_FLEX);
  buffer.emg1 = sample_value(ADC_CHN_EMG1);
  buffer.emg2 = sample_value(ADC_CHN_EMG2);
}

void ble_evt_write(ble_evt_t const *p_ble_evt)
{
  if (simple_ble_is_char_event(p_ble_evt, &cmd_char))
  {
    if (simple_ble_is_char_event(p_ble_evt, &cmd_char))
    {
      switch (cmd)
      {
      case 0x00:
        break; // Ignore this

      case 0x01:
        printf("(BLE) HALT received, system going down...\n");
        cmd = 0x00;
        NVIC_SystemReset();
        break;

      case 0x02:
        printf("(BLE) Reset...\n");
        // not impl
        cmd = 0x00;
        break;

      default:
        printf("Unknown command: %x\n", cmd);
        cmd = 0x00;
        break;
      }
    }
  }
}

void ble_evt_connected(ble_evt_t const *p_ble_evt)
{
  alive = true;
}

void ble_evt_disconnected(ble_evt_t const *p_ble_evt)
{
  alive = false;
}

void ble_error(uint32_t error_code)
{
  printf("(BLE Stack) BLE Error: %lu\n", error_code);
}

void saadc_callback(nrfx_saadc_evt_t const *p_event)
{
  // don't care about adc callbacks
}

int main(void)
{

  // Initialize
  ret_code_t error_code = NRF_SUCCESS;

  // initialize RTT library
  error_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(error_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();

  // adc init
  nrfx_saadc_config_t saadc_config = NRFX_SAADC_DEFAULT_CONFIG;
  saadc_config.resolution = NRF_SAADC_RESOLUTION_12BIT;
  error_code = nrfx_saadc_init(&saadc_config, saadc_callback);
  APP_ERROR_CHECK(error_code);

  // initialize analog inputs
  // configure with 0 as input pin for now
  nrf_saadc_channel_config_t channel_config = NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(0);
  channel_config.gain = NRF_SAADC_GAIN1_6;                 // input gain of 1/6 Volts/Volt, multiply incoming signal by (1/6)
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

  // Step counter (whatever that is)
  channel_config.pin_p = NRF_SAADC_INPUT_AIN6;
  // modify config if necessary
  error_code = nrfx_saadc_channel_init(ADC_CHN_ACC, &channel_config);
  APP_ERROR_CHECK(error_code);

  // i2c init
  nrf_drv_twi_config_t i2c_config = NRF_DRV_TWI_DEFAULT_CONFIG;
  i2c_config.scl = NRF_GPIO_PIN_MAP(0, 26);
  i2c_config.sda = NRF_GPIO_PIN_MAP(0, 27);
  i2c_config.frequency = NRF_TWIM_FREQ_400K;
  i2c_config.interrupt_priority = APP_IRQ_PRIORITY_LOW;
  i2c_config.clear_bus_init = false;
  MAX30102_twi_init(&i2c_config);

  // Setup PO & HR
  MAX30102_init();

  // Setup BLE
  simple_ble_app = simple_ble_init(&ble_config);

  simple_ble_add_service(&sensing_service);

  simple_ble_add_characteristic(1, 1, 1, 0, sizeof(cmd), (uint8_t *)&cmd, &sensing_service, &cmd_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(packet_t), (uint8_t *)&buffer, &sensing_service, &telemetry_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(stat_packet_t), (uint8_t *)&stat, &sensing_service, &stat_char);

  // Start Advertising
  simple_ble_adv_only_name();

  nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(0, 10));

  app_timer_init();
  app_timer_create(&sample_upd, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t)val_update);
  app_timer_start(sample_upd, APP_TIMER_TICKS(SAMPLE_INTERVAL_MS), NULL);
  app_timer_create(&sr_upd, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t)hr_update);
  app_timer_start(sr_upd, APP_TIMER_TICKS(60), NULL);
  //app_timer_create(&br_upd, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t)br_update);
  //app_timer_start(br_upd, APP_TIMER_TICKS(BR_INTERVAL_MS), NULL);
  app_timer_create(&sr_upd, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t)sr_update);
  app_timer_start(sr_upd, APP_TIMER_TICKS(SR_INTERVAL_MS), NULL);

  begin(30, 1.2, 0.9);
  startms = millis();
  while (true)
  {
    power_manage();
  }
}
