#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "nrf_drv_gpiote.h"
#include "nrf_twi_mngr.h"

#define MAX30102_ADDRESS                    0x57 // Not sure about this

#define MAX30102_INTSTAT1 		              0x00
#define MAX30102_INTSTAT2 		              0x01
#define MAX30102_INTENABLE1 		            0x02
#define MAX30102_INTENABLE2 		            0x03

// FIFO Registers
#define MAX30102_FIFOWRITEPTR  	            0x04
#define MAX30102_FIFOOVERFLOW  	            0x05
#define MAX30102_FIFOREADPTR  	            0x06
#define MAX30102_FIFODATA 		              0x07

// Configuration Registers
#define MAX30102_FIFOCONFIG  		            0x08
#define MAX30102_MODECONFIG  		            0x09
#define MAX30102_PARTICLECONFIG  	          0x0A  // Note, sometimes listed as "SPO2" config in datasheet (pg. 11)
#define MAX30102_LED1_PULSEAMP  	          0x0C
#define MAX30102_LED2_PULSEAMP  	          0x0D
#define MAX30102_LED3_PULSEAMP  	          0x0E
#define MAX30102_LED_PROX_AMP  	            0x10
#define MAX30102_MULTILEDCONFIG1            0x11
#define MAX30102_MULTILEDCONFIG2            0x12

// Die Temperature Registers
#define MAX30102_DIETEMPINT  		            0x1F
#define MAX30102_DIETEMPFRAC  	            0x20
#define MAX30102_DIETEMPCONFIG  	          0x21

// Proximity Function Registers
#define MAX30102_PROXINTTHRESH  	          0x30

// MAX30102 Commands
// Interrupt configuration (pg 13, 14)
#define MAX30102_INT_A_FULL_MASK 		        (uint8_t) (~0b10000000)
#define MAX30102_INT_A_FULL_ENABLE  	      0x80
#define MAX30102_INT_A_FULL_DISABLE  	      0x00

#define MAX30102_INT_DATA_RDY_MASK          (uint8_t) (~0b01000000)
#define MAX30102_INT_DATA_RDY_ENABLE 	      0x40
#define MAX30102_INT_DATA_RDY_DISABLE       0x00

#define MAX30102_INT_ALC_OVF_MASK           (uint8_t) (~0b00100000)
#define MAX30102_INT_ALC_OVF_ENABLE  	      0x20
#define MAX30102_INT_ALC_OVF_DISABLE        0x00

#define MAX30102_INT_PROX_INT_MASK          (uint8_t) (~0b00010000)
#define MAX30102_INT_PROX_INT_ENABLE        0x10
#define MAX30102_INT_PROX_INT_DISABLE       0x00

#define MAX30102_INT_DIE_TEMP_RDY_MASK      (uint8_t) (~0b00000010)
#define MAX30102_INT_DIE_TEMP_RDY_ENABLE    0x02
#define MAX30102_INT_DIE_TEMP_RDY_DISABLE   0x00

#define MAX30102_SAMPLEAVG_MASK 	          (uint8_t) (~0b11100000)
#define MAX30102_SAMPLEAVG_1  	            0x00
#define MAX30102_SAMPLEAVG_2  	            0x20
#define MAX30102_SAMPLEAVG_4  	            0x40
#define MAX30102_SAMPLEAVG_8  	            0x60
#define MAX30102_SAMPLEAVG_16  	            0x80
#define MAX30102_SAMPLEAVG_32  	            0xA0

#define MAX30102_ROLLOVER_MASK  	          0xEF
#define MAX30102_ROLLOVER_ENABLE            0x10
#define MAX30102_ROLLOVER_DISABLE           0x00

#define MAX30102_A_FULL_MASK  	            0xF0

// Mode configuration commands (page 19)
#define MAX30102_SHUTDOWN_MASK  	          0x7F
#define MAX30102_SHUTDOWN  		              0x80
#define MAX30102_WAKEUP  			              0x00

#define MAX30102_RESET_MASK  		            0xBF
#define MAX30102_RESET  			              0x40

#define MAX30102_MODE_MASK  		            0xF8
#define MAX30102_MODE_REDONLY  	            0x02
#define MAX30102_MODE_REDIRONLY  	          0x03 // use this
#define MAX30102_MODE_MULTILED  	          0x07

// Particle sensing configuration commands (pgs 19-20)
#define MAX30102_ADCRANGE_MASK  	          0x9F
#define MAX30102_ADCRANGE_2048  	          0x00
#define MAX30102_ADCRANGE_4096  	          0x20
#define MAX30102_ADCRANGE_8192  	          0x40
#define MAX30102_ADCRANGE_16384  	          0x60

#define MAX30102_SAMPLERATE_MASK            0xE3
#define MAX30102_SAMPLERATE_50  	          0x00
#define MAX30102_SAMPLERATE_100  	          0x04
#define MAX30102_SAMPLERATE_200  	          0x08
#define MAX30102_SAMPLERATE_400  	          0x0C
#define MAX30102_SAMPLERATE_800  	          0x10
#define MAX30102_SAMPLERATE_1000            0x14
#define MAX30102_SAMPLERATE_1600            0x18
#define MAX30102_SAMPLERATE_3200            0x1C

#define MAX30102_PULSEWIDTH_MASK            0xFC
#define MAX30102_PULSEWIDTH_69  	          0x00
#define MAX30102_PULSEWIDTH_118  	          0x01
#define MAX30102_PULSEWIDTH_215  	          0x02
#define MAX30102_PULSEWIDTH_411  	          0x03

//Multi-LED Mode configuration (pg 22)
#define MAX30102_SLOT1_MASK  		            0xF8
#define MAX30102_SLOT2_MASK  		            0x8F
#define MAX30102_SLOT3_MASK  		            0xF8
#define MAX30102_SLOT4_MASK  		            0x8F

#define SLOT_NONE  				                  0x00
#define SLOT_RED_LED  			                0x01
#define SLOT_IR_LED  				                0x02
#define SLOT_GREEN_LED  			              0x03
#define SLOT_NONE_PILOT  			              0x04
#define SLOT_RED_PILOT 			                0x05
#define SLOT_IR_PILOT  			                0x06
#define SLOT_GREEN_PILOT  		              0x07

typedef void max30102_read_callback(float val);
typedef void max30102_interrupt_callback(void);

typedef struct {
  uint8_t powerLvl;
  uint8_t sampleAvg;
  uint8_t ledMode;
  uint8_t sampleRate;
  uint8_t pulseWidth;
  uint8_t adcRange;
} max30102_config_t;

#define STORAGE_SIZE 4 // should adjust this?

#define I2C_BUFFER_LENGTH 32 // default

typedef struct {
    uint32_t red[STORAGE_SIZE];
    uint32_t IR[STORAGE_SIZE];
    uint32_t green[STORAGE_SIZE]; // should be INOP
    uint8_t head;
    uint8_t tail;
} sense_struct; // Circular buffer of readings from the sensor

void max30102_init(const nrf_twi_mngr_t* instance);
void max30102_config(max30102_config_t config);
void max30102_set_read_callback(max30102_read_callback* callback); // INOP
void max30102_fifo_next();
uint32_t getRed();
uint32_t getIR();
uint32_t getGreen(); // INOP
uint32_t getFIFORed();
uint32_t getFIFOIR();
uint32_t getFIFOGreen(); // INOP
uint16_t max30102_refresh(); // Poll for data, returns collected num of new samples
