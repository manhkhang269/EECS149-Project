#include <math.h>

#include "nrf_drv_gpiote.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_delay.h"

#include "max30102.h"

static const nrf_twi_mngr_t* twi_mngr_instance;
static uint8_t ledMode;

static sense_struct sense;

static max30102_read_callback* read_callback;
//static max30102_interrupt_callback* interrupt_callback;

//static void callback(ret_code_t result, void* p_context);

uint8_t readReg(uint8_t regAddr) {
    uint8_t scratch;
    nrf_twi_mngr_transfer_t read_tx[] = {
        NRF_TWI_MNGR_WRITE(MAX30102_ADDRESS, &regAddr, 1, NRF_TWI_MNGR_NO_STOP),
        NRF_TWI_MNGR_READ(MAX30102_ADDRESS, &scratch, 1, 0)
    };
    APP_ERROR_CHECK(nrf_twi_mngr_perform(twi_mngr_instance, NULL, read_tx, 1, NULL));
    return scratch;
}

void writeReg(uint8_t regAddr, uint8_t val) {
    nrf_twi_mngr_transfer_t write_tx[] = {
        NRF_TWI_MNGR_WRITE(MAX30102_ADDRESS, &regAddr, 1, NRF_TWI_MNGR_NO_STOP),
        NRF_TWI_MNGR_WRITE(MAX30102_ADDRESS, &val, 1, 0)
    };
    APP_ERROR_CHECK(nrf_twi_mngr_perform(twi_mngr_instance, NULL, write_tx, 1, NULL));
}

void bitMask(uint8_t regAddr, uint8_t mask, uint8_t val) {
    uint8_t scratch = readReg(regAddr);
    scratch = (scratch & mask) | val;
    writeReg(regAddr, scratch);
}

void max30102_init(const nrf_twi_mngr_t* instance) { // Need to set i2c speed?
  twi_mngr_instance = instance;
  bitMask(MAX30102_MODECONFIG, MAX30102_RESET_MASK, MAX30102_RESET);
  memset(&sense, 0, sizeof(sense_struct));
  nrf_delay_ms(100);
}

void max30102_config(max30102_config_t config) {
  bitMask(MAX30102_FIFOCONFIG, MAX30102_SAMPLEAVG_MASK, config.sampleAvg);
  bitMask(MAX30102_FIFOCONFIG, MAX30102_ROLLOVER_MASK, MAX30102_ROLLOVER_ENABLE);
  bitMask(MAX30102_MODECONFIG, MAX30102_MODE_MASK, config.ledMode);
  ledMode = config.ledMode;
  bitMask(MAX30102_PARTICLECONFIG, MAX30102_ADCRANGE_MASK, config.adcRange);
  bitMask(MAX30102_PARTICLECONFIG, MAX30102_SAMPLERATE_MASK, config.sampleRate);
  bitMask(MAX30102_PARTICLECONFIG, MAX30102_PULSEWIDTH_MASK, config.pulseWidth);
  writeReg(MAX30102_LED1_PULSEAMP, config.powerLvl);
  writeReg(MAX30102_LED2_PULSEAMP, config.powerLvl);
  writeReg(MAX30102_LED3_PULSEAMP, config.powerLvl);
  writeReg(MAX30102_LED_PROX_AMP, config.powerLvl);
  if (ledMode == MAX30102_MODE_REDONLY) {
      bitMask(MAX30102_MULTILEDCONFIG1, MAX30102_SLOT1_MASK, SLOT_RED_LED);
  } else if (ledMode == MAX30102_MODE_REDIRONLY) {
      bitMask(MAX30102_MULTILEDCONFIG1, MAX30102_SLOT1_MASK, SLOT_RED_LED);
      bitMask(MAX30102_MULTILEDCONFIG1, MAX30102_SLOT2_MASK, SLOT_IR_LED << 4);
  } else if (ledMode == MAX30102_MODE_MULTILED) {
      bitMask(MAX30102_MULTILEDCONFIG1, MAX30102_SLOT1_MASK, SLOT_RED_LED);
      bitMask(MAX30102_MULTILEDCONFIG1, MAX30102_SLOT2_MASK, SLOT_IR_LED << 4);
      bitMask(MAX30102_MULTILEDCONFIG2, MAX30102_SLOT3_MASK, SLOT_GREEN_LED);
  }
  writeReg(MAX30102_FIFOWRITEPTR, 0);
  writeReg(MAX30102_FIFOOVERFLOW, 0);
  writeReg(MAX30102_FIFOREADPTR, 0);
}

void  max30102_set_read_callback(max30102_read_callback* cb) {
  read_callback = cb;
}

uint8_t available() {
  int8_t numberOfSamples = sense.head - sense.tail;
  if (numberOfSamples < 0) numberOfSamples += STORAGE_SIZE;
  return numberOfSamples;
}

void max30102_fifo_next() {
    if (available()) {
        sense.tail++;
        sense.tail %= STORAGE_SIZE; //Wrap condition
    }
}

uint32_t getRed() {
    max30102_refresh();
    return sense.red[sense.head];
}

uint32_t getIR() {
    max30102_refresh();
    return sense.IR[sense.head];
}

uint32_t getGreen() {
    max30102_refresh();
    return sense.green[sense.head];
}

uint32_t getFIFORed() {
    return sense.red[sense.tail];
}

uint32_t getFIFOIR() {
    return sense.IR[sense.tail];
}

uint32_t getFIFOGreen() {
    return sense.green[sense.tail];
}

uint16_t max30102_refresh() {
    uint8_t readPointer = readReg(MAX30102_FIFOREADPTR);
    uint8_t writePointer = readReg(MAX30102_FIFOWRITEPTR);

    int numberOfSamples = 0;

    if (readPointer != writePointer)
    {
        //Calculate the number of readings we need to get from sensor
        numberOfSamples = writePointer - readPointer;
        if (numberOfSamples < 0)
            numberOfSamples += 32; //Wrap condition

        //We now have the number of readings, now calc bytes to read
        //For this example we are just doing Red and IR (3 bytes each)
        int activeLEDs = 0;
        if (ledMode == MAX30102_MODE_REDONLY) {
            activeLEDs = 1;
        } else if (ledMode == MAX30102_MODE_REDIRONLY) {
            activeLEDs = 2;
        } else if (ledMode == MAX30102_MODE_MULTILED) {
            activeLEDs = 3;
        }
        int bytesLeftToRead = numberOfSamples * activeLEDs * 3;

        //Get ready to read a burst of data from the FIFO register
        uint8_t cmd_tmp = MAX30102_FIFODATA;
        nrf_twi_mngr_transfer_t FIFOTxInit[] = {NRF_TWI_MNGR_WRITE(MAX30102_ADDRESS, &cmd_tmp, 1, 0)};
        int error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, FIFOTxInit, 1, NULL);
        APP_ERROR_CHECK(error);

        //We may need to read as many as 288 bytes so we read in blocks no larger than I2C_BUFFER_LENGTH
        //I2C_BUFFER_LENGTH changes based on the platform. 64 bytes for SAMD21, 32 bytes for Uno.
        //Wire.requestFrom() is limited to BUFFER_LENGTH which is 32 on the Uno
        while (bytesLeftToRead > 0)
        {
            int toGet = bytesLeftToRead;
            if (toGet > I2C_BUFFER_LENGTH)
            {
                //If toGet is 32 this is bad because we read 6 bytes (Red+IR * 3 = 6) at a time
                //32 % 6 = 2 left over. We don't want to request 32 bytes, we want to request 30.
                //32 % 9 (Red+IR+GREEN) = 5 left over. We want to request 27.

                toGet = I2C_BUFFER_LENGTH - (I2C_BUFFER_LENGTH % (activeLEDs * 3)); //Trim toGet to be a multiple of the samples we need to read
            }

            bytesLeftToRead -= toGet;

            //Request toGet number of bytes from sensor
            //_i2cPort->requestFrom(MAX30102_ADDRESS, toGet);

            while (toGet > 0)
            {
                sense.head++;               //Advance the head of the storage struct
                sense.head %= STORAGE_SIZE; //Wrap condition

                uint8_t temp[sizeof(uint32_t)]; //Array of 4 bytes that we will convert into long
                uint32_t tempLong;
                nrf_twi_mngr_transfer_t read_burst[] = {
                    NRF_TWI_MNGR_READ(MAX30102_ADDRESS, temp + 2, 1, NRF_TWI_MNGR_NO_STOP),
                    NRF_TWI_MNGR_READ(MAX30102_ADDRESS, temp + 1, 1, NRF_TWI_MNGR_NO_STOP),
                    NRF_TWI_MNGR_READ(MAX30102_ADDRESS, temp, 1, NRF_TWI_MNGR_NO_STOP)
                };

                //Burst read three bytes - RED
                temp[3] = 0;
                
                error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, read_burst, 1, NULL);
                APP_ERROR_CHECK(error);

                //Convert array to long
                memcpy(&tempLong, temp, sizeof(tempLong));

                tempLong &= 0x3FFFF; //Zero out all but 18 bits

                sense.red[sense.head] = tempLong; //Store this reading into the sense array

                if (activeLEDs > 1)
                {
                    //Burst read three more bytes - IR
                    temp[3] = 0;
                    error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, read_burst, 1, NULL);
                    APP_ERROR_CHECK(error);

                    //Convert array to long
                    memcpy(&tempLong, temp, sizeof(tempLong));

                    tempLong &= 0x3FFFF; //Zero out all but 18 bits

                    sense.IR[sense.head] = tempLong;
                }

                if (activeLEDs > 2)
                {
                    //Burst read three more bytes - Green
                    temp[3] = 0;
                    error = nrf_twi_mngr_perform(twi_mngr_instance, NULL, read_burst, 1, NULL);
                    APP_ERROR_CHECK(error);

                    //Convert array to long
                    memcpy(&tempLong, temp, sizeof(tempLong));

                    tempLong &= 0x3FFFF; //Zero out all but 18 bits

                    sense.green[sense.head] = tempLong;
                }

                toGet -= activeLEDs * 3;
            }

        } //End while (bytesLeftToRead > 0)

    } //End readPtr != writePtr

    return numberOfSamples;
}
