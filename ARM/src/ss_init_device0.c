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

#include "context.h"
#include "twi_simple.h"
#include "ss_init.h"

#pragma pack(push)
#pragma pack(1)
typedef struct {
    uint8_t reg;
    uint8_t value;
} SWITCH_CONFIG;
#pragma pack(pop)

#define SOFT_SWITCH0_U16_I2C_ADDR   0x21
#define PORTA 0x12u
#define PORTB 0x13u

/* switch 1 register settings */
static SWITCH_CONFIG ss0U16Config[] =
{

 /*
       U16 Port A                                        U16 Port B
  7--------------- ~CAN1_EN                 |       7--------------- ~SPDIF_DIGITAL_EN
  | 6------------- ~CAN0_EN                 |       | 6------------- ~SPDIF_OPTICAL_EN
  | | 5----------- ~MLB3_EN                 |       | | 5----------- ~SPID2_D3_EN
  | | | 4--------- NOT USED                 |       | | | 4--------- ~SPI2FLASH_CS_EN
  | | | | 3------- NOT USED                 |       | | | | 3------- NOT USED
  | | | | | 2----- ~UART0_EN                |       | | | | | 2----- AUDIO_JACK_SEL
  | | | | | | 1--- ~UART0_FLOW_EN           |       | | | | | | 1--- ~ADAU1979_EN
  | | | | | | | 0- ~EEPROM_EN               |       | | | | | | | 0- ~ADAU1962_EN
  | | | | | | | |                           |       | | | | | | | |
  X X X X X X X X                           |       X X X X X X X X  ( Active Y or N )
  1 1 1 0 0 0 1 0                           |       1 0 0 0 0 1 0 0  ( value being set )
*/
  { PORTA, 0xE2u }, /* 0xE2 */                     { PORTB, 0x84u }, /* 0x84 */

  { 0x0u, 0x00u },  /* Set IODIRA direction (all output) */
  { 0x1u, 0x00u },  /* Set IODIRB direction (all output) */
};

void ss_init_device0(APP_CONTEXT *context)
{
    TWI_SIMPLE_RESULT twiResult;
    uint8_t configLen;
    uint8_t i;

    /* Program carrier U16 soft switches */
    configLen = sizeof(ss0U16Config) / sizeof(ss0U16Config[0]);
    for (i = 0; i < configLen; i++) {
        twiResult = twi_write(context->softSwitchHandle, SOFT_SWITCH0_U16_I2C_ADDR,
            (uint8_t *)&ss0U16Config[i], sizeof(ss0U16Config[i]));
    }

}

typedef struct _SS0_PIN {
    int pinId;
    uint8_t port;
    uint8_t bitp;
} SS0_PIN;

static SS0_PIN ss0Pins[] = {
    { .pinId = SS_PIN_ID_CAN1_EN,           .port = PORTA, .bitp = 7 },
    { .pinId = SS_PIN_ID_CAN0_EN,           .port = PORTA, .bitp = 6 },
    { .pinId = SS_PIN_ID_MLB3_EN,           .port = PORTA, .bitp = 5 },
    { .pinId = SS_PIN_ID_UART0_EN,          .port = PORTA, .bitp = 2 },
    { .pinId = SS_PIN_ID_UART0_FLOW_EN,     .port = PORTA, .bitp = 1 },
    { .pinId = SS_PIN_ID_EEPROM_EN,         .port = PORTA, .bitp = 0 },
    { .pinId = SS_PIN_ID_SPDIF_DIGITAL_EN,  .port = PORTB, .bitp = 7 },
    { .pinId = SS_PIN_ID_SPDIF_OPTICAL_EN,  .port = PORTB, .bitp = 6 },
    { .pinId = SS_PIN_ID_SPID2_D3_EN,       .port = PORTB, .bitp = 5 },
    { .pinId = SS_PIN_ID_SPI2FLASH_CS_EN,   .port = PORTB, .bitp = 4 },
    { .pinId = SS_PIN_ID_AUDIO_JACK_SEL,    .port = PORTB, .bitp = 2 },
    { .pinId = SS_PIN_ID_ADAU1979_EN,       .port = PORTB, .bitp = 1 },
    { .pinId = SS_PIN_ID_ADAU1962_EN,       .port = PORTB, .bitp = 0 },
    { .pinId = SS_PIN_ID_MAX                                         }
};

static SS0_PIN *findPin(int pinId)
{
    SS0_PIN *somPin;

    somPin = ss0Pins;
    while (somPin->pinId != SS_PIN_ID_MAX) {
        if (somPin->pinId == pinId) {
            break;
        }
        somPin++;
    }
    if (somPin->pinId == SS_PIN_ID_MAX) {
        somPin = NULL;
    }
    return(somPin);
}

bool ss_get_device0(APP_CONTEXT *context, int pinId, bool *value)
{
    TWI_SIMPLE_RESULT twiResult;
    SS0_PIN *somPin;
    uint8_t reg;
    uint8_t val;

    somPin = findPin(pinId);
    if (somPin == NULL) {
        return(false);
    }

    reg = somPin->port;
    twiResult = twi_writeRead(context->softSwitchHandle,
        SOFT_SWITCH0_U16_I2C_ADDR, &reg, 1, &val, 1);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(false);
    }

    if (value) {
        *value = (val & (1 << somPin->bitp));
    }

    return(true);
}

bool ss_set_device0(APP_CONTEXT *context, int pinId, bool value)
{
    TWI_SIMPLE_RESULT twiResult;
    SS0_PIN *somPin;
    SWITCH_CONFIG sw;

    somPin = findPin(pinId);
    if (somPin == NULL) {
        return(false);
    }

    sw.reg = somPin->port;
    twiResult = twi_writeRead(context->softSwitchHandle,
        SOFT_SWITCH0_U16_I2C_ADDR, &sw.reg, 1, &sw.value, 1);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(false);
    }

    if (value) {
        sw.value |= (1 << somPin->bitp);
    } else {
        sw.value &= ~(1 << somPin->bitp);
    }

    twiResult = twi_write(context->softSwitchHandle,
        SOFT_SWITCH0_U16_I2C_ADDR, (uint8_t *)&sw, sizeof(sw));
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(false);
    }

    return(true);
}
