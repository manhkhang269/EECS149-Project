#include "max30102.h"

#define MAX30102_ADDR 0x57

/* TWI instance ID. */
#if TWI0_ENABLED
#define TWI_INSTANCE_ID 0
#elif TWI1_ENABLED
#define TWI_INSTANCE_ID 1
#endif

/* Interrupt */
uint8_t REG_INT_STATUS_1 = 0x00;
uint8_t REG_INT_STATUS_2 = 0x01;
uint8_t REG_INT_ENABLE_1 = 0x02;
uint8_t REG_INT_ENABLE_2 = 0x03;

/* FIFO */
uint8_t REG_FIFO_WR_PTR = 0x04;
uint8_t REG_OVERFLOW_CTR = 0x05;
uint8_t REG_FIFO_RD_PTR = 0x06;
uint8_t REG_FIFO_DATA = 0x07;

/* Configuration */
uint8_t REG_FIFO_CONF = 0x08;
uint8_t REG_MODE_CONF = 0x09;
uint8_t REG_SPO2_CONF = 0X0A;
uint8_t REG_LED1_PA = 0x0C;
uint8_t REG_LED2_PA = 0x0D;
uint8_t REG_PILOT_PA = 0x10;
uint8_t REG_MULTILED_1 = 0x11;
uint8_t REG_MULTILED_2 = 0x12;

/* Die Temperature */
uint8_t REG_TEMP_INTR = 0x1F;
uint8_t REG_TEMP_FRAC = 0x20;
uint8_t REG_TEMP_CONF = 0x21;

/* Proximity function */
uint8_t REG_PROX_INT_TH = 0x30;

/* Identificators */
uint8_t REG_REF_ID = 0xFE;
uint8_t REG_PART_ID = 0xFF;

/* TWI instance. */
static const nrf_drv_twi_t twi_instance = NRF_DRV_TWI_INSTANCE(0);

/* Indicates if operation on TWI has ended. */
static volatile bool m_xfer_done = false;

/**
 * @brief TWI events handler.
 */
void twi_handler(nrf_drv_twi_evt_t const *p_event, void *p_context)
{

    switch (p_event->type)
    {
    case NRF_DRV_TWI_EVT_DONE:
        if (p_event->xfer_desc.type == NRF_DRV_TWI_XFER_RX)
        {
            //data_handler(sample);
        }

        m_xfer_done = true;
        break;

    default:
        break;
    }
}

/**
 * @brief TWI initialization.
 */
void MAX30102_twi_init(nrf_drv_twi_config_t *conf)
{
    ret_code_t err_code;

    err_code = nrf_drv_twi_init(&twi_instance, conf, twi_handler, NULL);
    APP_ERROR_CHECK(err_code);

    nrf_drv_twi_enable(&twi_instance);
}

void MAX30102_reset(void)
{

    MAX30102_write_register(REG_MODE_CONF, 0x40);
}

void MAX30102_init(void)
{
    MAX30102_write_register(REG_INT_ENABLE_1, 0xC0);
    MAX30102_write_register(REG_INT_ENABLE_2, 0x00);
    MAX30102_write_register(REG_FIFO_WR_PTR, 0x00);
    MAX30102_write_register(REG_OVERFLOW_CTR, 0x00);
    MAX30102_write_register(REG_FIFO_RD_PTR, 0x00);
    MAX30102_write_register(REG_FIFO_CONF, 0x4F);
    MAX30102_write_register(REG_MODE_CONF, 0x03);
    MAX30102_write_register(REG_SPO2_CONF, 0x27);
    MAX30102_write_register(REG_LED1_PA, 0x3F);
    MAX30102_write_register(REG_LED2_PA, 0x3F);
    MAX30102_write_register(REG_PILOT_PA, 0x7F);
}

void MAX30102_read_ID(void)
{

    uint8_t identification;
    uint8_t reference;

    MAX30102_read_register(REG_PART_ID, &identification);

    NRF_LOG_INFO("La ID general del sensor es: 0x%02x \r\n", identification);
    NRF_LOG_FLUSH();

    MAX30102_read_register(REG_REF_ID, &reference);

    NRF_LOG_INFO("La ID particular de este sensor es: 0x%02x \r\n", reference);
    NRF_LOG_FLUSH();
}

void MAX30102_read_fifo(uint32_t *pun_red_led, uint32_t *pun_ir_led)
{

    uint8_t clean;
    uint32_t temp_32 = 0;
    uint8_t temp_array[6];
    ret_code_t err_code;

    *pun_ir_led = 0;
    *pun_red_led = 0;
    MAX30102_read_register(REG_INT_STATUS_1, &clean);
    err_code = nrf_drv_twi_tx(&twi_instance, MAX30102_ADDR, &REG_FIFO_DATA, sizeof(REG_FIFO_DATA), true);
    APP_ERROR_CHECK(err_code);
    while (m_xfer_done == false)
        ;
    m_xfer_done = false;

    err_code = nrf_drv_twi_rx(&twi_instance, MAX30102_ADDR, temp_array, sizeof(temp_array));
    APP_ERROR_CHECK(err_code);
    while (m_xfer_done == false)
        ;
    m_xfer_done = false;

    temp_32 |= temp_array[0];
    temp_32 <<= 8;
    temp_32 |= temp_array[1];
    temp_32 <<= 8;
    temp_32 |= temp_array[2];
    *pun_red_led = temp_32;

    *pun_red_led &= 0x0003FFFF;

    temp_32 = 0;

    temp_32 |= temp_array[3];
    temp_32 <<= 8;
    temp_32 |= temp_array[4];
    temp_32 <<= 8;
    temp_32 |= temp_array[5];
    *pun_ir_led = temp_32;

    *pun_ir_led &= 0x0003FFFF;
}

void MAX30102_write_register(uint8_t reg_address, uint8_t data)
{
    ret_code_t err_code;
    uint8_t buffer_send[2] = {reg_address, data};

    err_code = nrf_drv_twi_tx(&twi_instance, MAX30102_ADDR, buffer_send, sizeof(buffer_send), true);
    APP_ERROR_CHECK(err_code);
    while (m_xfer_done == false)
        ;
    m_xfer_done = false;
}

void MAX30102_read_register(uint8_t reg_address, uint8_t *data)
{
    ret_code_t err_code;

    err_code = nrf_drv_twi_tx(&twi_instance, MAX30102_ADDR, &reg_address, sizeof(reg_address), true);
    APP_ERROR_CHECK(err_code);
    while (m_xfer_done == false)
        ;
    m_xfer_done = false;

    err_code = nrf_drv_twi_rx(&twi_instance, MAX30102_ADDR, data, sizeof(data));
    APP_ERROR_CHECK(err_code);
    while (m_xfer_done == false)
        ;
    m_xfer_done = false;
}
