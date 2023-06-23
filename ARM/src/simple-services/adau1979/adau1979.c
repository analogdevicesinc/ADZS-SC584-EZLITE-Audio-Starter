/**
 * Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
 * This software is proprietary and confidential to Analog Devices, Inc.
 * and its licensors.
 *
 * This software is subject to the terms and conditions of the license set
 * forth in the project LICENSE file. Downloading, reproducing, distributing or
 * otherwise using the software constitutes acceptance of the license. The
 * software may not be used except as expressly authorized under the license.
 */

#include <stdint.h>

#include "adau1979.h"
#include "twi_simple.h"
#include "util.h"

#define ADAU1979_REG_POWER                  0x00
#define ADAU1979_REG_PLL                    0x01
#define ADAU1979_REG_BOOST                  0x02
#define ADAU1979_REG_MICBIAS                0x03
#define ADAU1979_REG_BLOCK_POWER_SAI        0x04
#define ADAU1979_REG_SAI_CTRL0              0x05
#define ADAU1979_REG_SAI_CTRL1              0x06
#define ADAU1979_REG_CMAP12                 0x07
#define ADAU1979_REG_CMAP34                 0x08
#define ADAU1979_REG_SAI_OVERTEMP           0x09
#define ADAU1979_REG_POST_ADC_GAIN1         0x0a
#define ADAU1979_REG_POST_ADC_GAIN2         0x0b
#define ADAU1979_REG_POST_ADC_GAIN3         0x0c
#define ADAU1979_REG_POST_ADC_GAIN4         0x0d
#define ADAU1979_REG_MISC_CONTROL           0x0e
#define ADAU1979_REG_ADC_CLIP               0x19
#define ADAU1979_REG_DC_HPF_CAL             0x1a

typedef struct {
    uint8_t reg;
    uint8_t value;
} REG_CONFIG;

REG_CONFIG adau1979Config[] = {
    { ADAU1979_REG_SAI_CTRL0,       0x1A }, // 48KHz, TDM8, 1-Bit Delay
//    { ADAU1979_REG_SAI_CTRL0,       0x22 }, // 48KHz, TDM16, 1-Bit Delay
    { ADAU1979_REG_SAI_CTRL1,       0x00 }, // I2S framing, Slave
    { ADAU1979_REG_DC_HPF_CAL,      0x0F }, // Enable HPF CH1-4
    { ADAU1979_REG_POST_ADC_GAIN1,  0x8C }, // +7.5dB gain
    { ADAU1979_REG_POST_ADC_GAIN2,  0x8C }, // +7.5dB gain
    { ADAU1979_REG_POST_ADC_GAIN3,  0x8C }, // +7.5dB gain
    { ADAU1979_REG_POST_ADC_GAIN4,  0x8C }, // +7.5dB gain
};

static ADAU1979_RESULT init_adau1979_pll(sTWI *twi, uint8_t adau_address)
{
    ADAU1979_RESULT result;
    TWI_SIMPLE_RESULT twiResult;
    uint8_t rx[2];
    uint8_t tx[2];

    result = ADAU1979_SUCCESS;

    /* Set PWUP bit */
    tx[0] = ADAU1979_REG_POWER; tx[1] = 0x01;
    twiResult = twi_write(twi, adau_address, tx, 2);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(ADAU1979_ERROR);
    }
    delay(2);

    /* Set MCS = 512fs */
    tx[0] = ADAU1979_REG_PLL; tx[1] = 0x03;
    twiResult = twi_write(twi, adau_address, tx, 2);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(ADAU1979_ERROR);
    }
    delay(2);

    /* Poll for PLL_LOCK bit */
    tx[0] = ADAU1979_REG_PLL;
    do {
        twiResult = twi_writeRead(twi, adau_address, tx, 1, rx, 1);
        if (twiResult != TWI_SIMPLE_SUCCESS) {
            return(ADAU1979_ERROR);
        }
    } while ((rx[0] & 0x80) == 0);

    return(result);
}


ADAU1979_RESULT init_adau1979(sTWI *twi, uint8_t adau_address)
{
    ADAU1979_RESULT result;
    TWI_SIMPLE_RESULT twiResult;
    uint8_t configLen;
    uint8_t i;
    uint8_t buf[2];
    
    /* Perform a software reset to ensure all internal registers are reset to their POR */
    buf[0] = ADAU1979_REG_POWER;
    buf[1] = 0x80;
    twiResult = twi_write(twi, adau_address, buf, sizeof(buf));
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(ADAU1979_ERROR);
    }

    /* Initialize the ADAU1979 PLL */
    result = init_adau1979_pll(twi, adau_address);
    if (result != ADAU1979_SUCCESS) {
        return(result);
    }

    /* Program ADAU1979 registers */
    configLen = sizeof(adau1979Config) / sizeof(adau1979Config[0]);
    for (i = 0; i < configLen; i++) {
        buf[0] = adau1979Config[i].reg; buf[1] = adau1979Config[i].value;
        twiResult = twi_write(twi, adau_address, buf, sizeof(buf));
        if (twiResult != TWI_SIMPLE_SUCCESS) {
            return(ADAU1979_ERROR);
        }
    }

    return(result);
}
