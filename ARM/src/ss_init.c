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

#include "ss_init.h"

/* Init prototypes */
void ss_init_device0(APP_CONTEXT *context);
void ss_init_device1(APP_CONTEXT *context);

/* Set prototypes */
bool ss_set_device0(APP_CONTEXT *context, int pinId, bool value);
bool ss_set_device1(APP_CONTEXT *context, int pinId, bool value);

/* Get prototypes */
bool ss_get_device0(APP_CONTEXT *context, int pinId, bool *value);
bool ss_get_device1(APP_CONTEXT *context, int pinId, bool *value);

/* Convenience macros */
#define DEV0_GET ss_get_device0
#define DEV1_GET ss_get_device1
#define DEV0_SET ss_set_device0
#define DEV1_SET ss_set_device1

typedef struct _SS_PIN {
    int pinId;
    SS_GET get;
    SS_SET set;
} SS_PIN;

static SS_PIN SS_PINS[] = {
    /* Device 0 */
    { .pinId = SS_PIN_ID_EEPROM_EN,              .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_UART0_FLOW_EN,          .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_UART0_EN,               .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_MLB3_EN,                .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_CAN0_EN,                .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_CAN1_EN,                .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_ADAU1962_EN,            .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_ADAU1979_EN,            .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_AUDIO_JACK_SEL,         .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_SPI2FLASH_CS_EN,        .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_SPID2_D3_EN,            .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_SPDIF_OPTICAL_EN,       .get = DEV0_GET, .set = DEV0_SET},
    { .pinId = SS_PIN_ID_SPDIF_DIGITAL_EN,       .get = DEV0_GET, .set = DEV0_SET},
    /* Device 1 */
    { .pinId = SS_PIN_ID_PUSHBUTTON3_EN,         .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_PUSHBUTTON2_EN,         .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_PUSHBUTTON1_EN,         .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_LEDS_EN,                .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_FLG0_LOOP,              .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_FLG1_LOOP,              .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_FLG2_LOOP,              .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_FLG3_LOOP,              .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_ADAU1977_EN,            .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_ADAU1977_FAULT_RST_EN,  .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_THUMBWHEEL_OE,          .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_ENGINE_RPM_OE,          .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_AD2410_MASTER_SLAVE,    .get = DEV1_GET, .set = DEV1_SET},
    { .pinId = SS_PIN_ID_MAX,                    .get = NULL,     .set = NULL}
};

bool ss_get(APP_CONTEXT *context, int pinId, bool *value)
{
    SS_PIN *pin = SS_PINS;
    while (pin->pinId != SS_PIN_ID_MAX) {
        if (pin->pinId == pinId) {
            return(pin->get(context, pinId, value));
        }
        pin++;
    }
    return(false);
}

bool ss_set(APP_CONTEXT *context, int pinId, bool value)
{
    SS_PIN *pin = SS_PINS;
    while (pin->pinId != SS_PIN_ID_MAX) {
        if (pin->pinId == pinId) {
            return(pin->set(context, pinId, value));
        }
        pin++;
    }
    return(false);
}

void ss_init(APP_CONTEXT *context)
{
    ss_init_device0(context);
    ss_init_device1(context);
}
