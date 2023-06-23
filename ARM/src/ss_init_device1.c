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

#define SOFT_SWITCH1_U6_I2C_ADDR    0x22
#define PORTA 0x12u
#define PORTB 0x13u

/* switch 2 register settings */
static SWITCH_CONFIG ss1U16Config[] =
{

/*
       U6 Port A                                   U6 Port B
    7--------------- ~FLG3_LOOP         |       7--------------- NOT USED
    | 6------------- ~FLG2_LOOP         |       | 6------------- NOT USED
    | | 5----------- ~FLG1_LOOP         |       | | 5----------- NOT USED
    | | | 4--------- ~FLG0_LOOP         |       | | | 4--------- ADA2410_MASTER_SLAVE
    | | | | 3------- ~LEDS_EN           |       | | | | 3------- ~ENGINE_RPM_OE
    | | | | | 2----- ~PUSHBUTTON1_EN    |       | | | | | 2----- ~THUMBWHEEL_OE
    | | | | | | 1--- ~PUSHBUTTON2_EN    |       | | | | | | 1--- ~ADAU1977_FAULT_RST_EN
    | | | | | | | 0- ~PUSHBUTTON3_EN    |       | | | | | | | 0- ~ADAU1977_EN
    | | | | | | | |                     |       | | | | | | | |
    X X X X X X X X                     |       X X X X X X X X  ( Active Y or N )
    1 1 1 1 0 1 0 1                     |       0 0 0 1 1 1 0 0  ( value being set )
*/
  { PORTA, 0xF5u }, /* 0xF5 */                  { PORTB, 0x1Cu }, /* 0x1C */

  { 0x0u, 0x00u },    /* Set IODIRA direction (all output) */
  { 0x1u, 0x00u },    /* Set IODIRB direction (all output) */
};

void ss_init_device1(APP_CONTEXT *context)
{
    TWI_SIMPLE_RESULT twiResult;
    uint8_t configLen;
    uint8_t i;

    /* Program carrier U6 soft switches */
    configLen = sizeof(ss1U16Config) / sizeof(ss1U16Config[0]);
    for (i = 0; i < configLen; i++) {
        twiResult = twi_write(context->softSwitchHandle, SOFT_SWITCH1_U6_I2C_ADDR,
            (uint8_t *)&ss1U16Config[i], sizeof(ss1U16Config[i]));
    }

}

typedef struct _SS1_PIN {
    int pinId;
    uint8_t port;
    uint8_t bitp;
} SS1_PIN;

static SS1_PIN ss1Pins[] = {
    { .pinId = SS_PIN_ID_FLG3_LOOP,             .port = PORTA, .bitp = 7 },
    { .pinId = SS_PIN_ID_FLG2_LOOP,             .port = PORTA, .bitp = 6 },
    { .pinId = SS_PIN_ID_FLG1_LOOP,             .port = PORTA, .bitp = 5 },
    { .pinId = SS_PIN_ID_FLG0_LOOP,             .port = PORTA, .bitp = 4 },
    { .pinId = SS_PIN_ID_LEDS_EN,               .port = PORTA, .bitp = 3 },
    { .pinId = SS_PIN_ID_PUSHBUTTON1_EN,        .port = PORTA, .bitp = 2 },
    { .pinId = SS_PIN_ID_PUSHBUTTON2_EN,        .port = PORTA, .bitp = 1 },
    { .pinId = SS_PIN_ID_PUSHBUTTON3_EN,        .port = PORTA, .bitp = 0 },
    { .pinId = SS_PIN_ID_AD2410_MASTER_SLAVE,   .port = PORTB, .bitp = 4 },
    { .pinId = SS_PIN_ID_ENGINE_RPM_OE,         .port = PORTB, .bitp = 3 },
    { .pinId = SS_PIN_ID_THUMBWHEEL_OE,         .port = PORTB, .bitp = 2 },
    { .pinId = SS_PIN_ID_ADAU1977_FAULT_RST_EN, .port = PORTB, .bitp = 1 },
    { .pinId = SS_PIN_ID_ADAU1977_EN,           .port = PORTB, .bitp = 0 },
    { .pinId = SS_PIN_ID_MAX                                             }
};

static SS1_PIN *findPin(int pinId)
{
    SS1_PIN *somCrrPin;

    somCrrPin = ss1Pins;
    while (somCrrPin->pinId != SS_PIN_ID_MAX) {
        if (somCrrPin->pinId == pinId) {
            break;
        }
        somCrrPin++;
    }
    if (somCrrPin->pinId == SS_PIN_ID_MAX) {
        somCrrPin = NULL;
    }
    return(somCrrPin);
}

bool ss_get_device1(APP_CONTEXT *context, int pinId, bool *value)
{
    TWI_SIMPLE_RESULT twiResult;
    SS1_PIN *somCrrPin;
    uint8_t reg;
    uint8_t val;

    somCrrPin = findPin(pinId);
    if (somCrrPin == NULL) {
        return(false);
    }

    reg = somCrrPin->port;
    twiResult = twi_writeRead(context->softSwitchHandle,
        SOFT_SWITCH1_U6_I2C_ADDR, &reg, 1, &val, 1);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(false);
    }

    if (value) {
        *value = (val & (1 << somCrrPin->bitp));
    }

    return(true);
}

bool ss_set_device1(APP_CONTEXT *context, int pinId, bool value)
{
    TWI_SIMPLE_RESULT twiResult;
    SS1_PIN *somCrrPin;
    SWITCH_CONFIG sw;

    somCrrPin = findPin(pinId);
    if (somCrrPin == NULL) {
        return(false);
    }

    sw.reg = somCrrPin->port;
    twiResult = twi_writeRead(context->softSwitchHandle,
        SOFT_SWITCH1_U6_I2C_ADDR, &sw.reg, 1, &sw.value, 1);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(false);
    }

    if (value) {
        sw.value |= (1 << somCrrPin->bitp);
    } else {
        sw.value &= ~(1 << somCrrPin->bitp);
    }

    twiResult = twi_write(context->softSwitchHandle,
        SOFT_SWITCH1_U6_I2C_ADDR, (uint8_t *)&sw, sizeof(sw));
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        return(false);
    }

    return(true);
}
