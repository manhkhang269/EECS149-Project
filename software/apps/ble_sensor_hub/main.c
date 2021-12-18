// Controller Firmware

#include <stdbool.h>
#include <stdint.h>
#include "nrf.h"
#include "app_util.h"
#include "nrf_drv_spi.h"
#include "nrf_gpio.h"
#include "app_pwm.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "simple_ble.h"

APP_TIMER_DEF(data_upd_timer);
APP_TIMER_DEF(stat_upd_timer);

#define SPI_INSTANCE 1                                               /**< SPI instance index. */
static const nrf_drv_spi_t spi = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE); /**< SPI instance. */
static volatile bool spi_xfer_done;                                  /**< Flag used to indicate that SPI instance completed the transfer. */

#define STAT_REFRESH_INTERVAL_MS 60
#define DATA_REFRESH_INTERVAL_MS 100
#define AVG_SAMPLE_CNT 4

#define SPI_SS_PIN 31
#define SPI_MISO_PIN 30
#define SPI_MOSI_PIN 29
#define SPI_SCK_PIN 26

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

static bool alive = false;

static bool callback_is_data;

static volatile bool spi_in_progress = false;

typedef struct
{
  uint32_t max_red;
  uint32_t max_ir;
  uint16_t gsr;
  uint16_t flex;
  uint16_t emg1;
  uint16_t emg2;
} data_packet_t;

typedef struct
{
  uint8_t hr;
  uint8_t br;
  uint8_t st_emg;
  uint8_t st_acc;
} stat_packet_t;

static uint8_t m_tx_buf; /** Command: 0x01 for data, 0x02 for stat */

// 32e61089-2b22-4db5-a914-43ce41986c70
static simple_ble_service_t sensing_service = {{.uuid128 = {0x70, 0x6C, 0x98, 0x41, 0xCE, 0x43, 0x14, 0xA9,
                                                            0xB5, 0x4D, 0x22, 0x2B, 0x89, 0x10, 0xE6, 0x32}}};
// xxxx<xxxx> -xxxx-xxxx-xxxx-xxxxxxxxxxxx
static simple_ble_char_t cmd_char = {.uuid16 = 0x108a};
static uint8_t cmd = 0x00;
// Raw signal
static simple_ble_char_t sig_char = {.uuid16 = 0x108b};
static data_packet_t data;
// Telemetry
static simple_ble_char_t telemetry_char = {.uuid16 = 0x108c};
static stat_packet_t stat_buffer[AVG_SAMPLE_CNT];
static size_t stat_buf_ptr = 0;
static stat_packet_t stat_avg;

/*******************************************************************************
 *   State for this application
 ******************************************************************************/
// Main application state
simple_ble_app_t *simple_ble_app;

void ble_evt_write(ble_evt_t const *p_ble_evt)
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
      stat_buf_ptr = 0;
      memset((void *)stat_buffer, 0x00, sizeof(stat_buffer));
      memset((void *)&stat_avg, 0x00, sizeof(stat_avg));
      cmd = 0x00;
      break;

    default:
      printf("Unknown command: %x\n", cmd);
      cmd = 0x00;
      break;
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

void stat_upd()
{
  if (spi_in_progress)
    return;
  m_tx_buf = 0x02;
  APP_ERROR_CHECK(nrf_drv_spi_transfer(&spi, &m_tx_buf, sizeof(m_tx_buf), (uint8_t *)&stat_buffer[stat_buf_ptr], sizeof(stat_packet_t)));
  spi_in_progress = true;
  callback_is_data = false;
}

void data_upd()
{
  if (spi_in_progress)
    return;
  m_tx_buf = 0x01;
  APP_ERROR_CHECK(nrf_drv_spi_transfer(&spi, &m_tx_buf, sizeof(m_tx_buf), (uint8_t *)&data, sizeof(data_packet_t)));
  spi_in_progress = true;
  callback_is_data = true;
}

void stat_upd_callback()
{
  stat_buf_ptr++;
  stat_buf_ptr %= AVG_SAMPLE_CNT;
  if (stat_buf_ptr == 0)
  {
    uint16_t hr_accu = 0, br_accu = 0, st_emg_accu = 0, st_acc_accu = 0;
    for (size_t x = 0; x < AVG_SAMPLE_CNT; x++)
    {
      hr_accu += stat_buffer[x].hr;
      br_accu += stat_buffer[x].br;
      st_emg_accu += stat_buffer[x].st_emg;
      st_acc_accu += stat_buffer[x].st_acc;
    }
    stat_avg.hr = hr_accu / AVG_SAMPLE_CNT;
    stat_avg.br = br_accu / AVG_SAMPLE_CNT;
    stat_avg.st_emg = st_emg_accu / AVG_SAMPLE_CNT;
    stat_avg.st_acc = st_acc_accu / AVG_SAMPLE_CNT;
    simple_ble_notify_char(&telemetry_char);
  }
}

void data_upd_callback()
{
  simple_ble_notify_char(&sig_char);
}

void spi_event_handler(nrf_drv_spi_evt_t const *p_event, void *p_context)
{
  callback_is_data ? data_upd_callback() : stat_upd_callback();
  spi_in_progress = false;
}

int main(void)
{

  // Initialize
  ret_code_t error_code = NRF_SUCCESS;

  // initialize RTT library
  error_code = NRF_LOG_INIT(NULL);
  APP_ERROR_CHECK(error_code);
  NRF_LOG_DEFAULT_BACKENDS_INIT();

  // SPI Setup
  nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
  spi_config.ss_pin = SPI_SS_PIN;
  spi_config.miso_pin = SPI_MISO_PIN;
  spi_config.mosi_pin = SPI_MOSI_PIN;
  spi_config.sck_pin = SPI_SCK_PIN;
  APP_ERROR_CHECK(nrf_drv_spi_init(&spi, &spi_config, spi_event_handler, NULL));

  // Setup BLE
  simple_ble_app = simple_ble_init(&ble_config);
  simple_ble_add_service(&sensing_service);

  simple_ble_add_characteristic(1, 1, 1, 0, sizeof(cmd), (uint8_t *)&cmd, &sensing_service, &cmd_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(data_packet_t), (uint8_t *)&data, &sensing_service, &sig_char);
  simple_ble_add_characteristic(1, 0, 1, 0, sizeof(stat_packet_t), (uint8_t *)&stat_avg, &sensing_service, &telemetry_char);

  // Start Advertising
  simple_ble_adv_only_name();

  app_timer_init();
  app_timer_create(&stat_upd_timer, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t)stat_upd);
  app_timer_start(stat_upd_timer, APP_TIMER_TICKS(STAT_REFRESH_INTERVAL_MS), NULL);
  app_timer_create(&data_upd_timer, APP_TIMER_MODE_REPEATED, (app_timer_timeout_handler_t)data_upd);
  app_timer_start(data_upd_timer, APP_TIMER_TICKS(DATA_REFRESH_INTERVAL_MS), NULL);

  while (true)
  {
    power_manage();
  }
}
