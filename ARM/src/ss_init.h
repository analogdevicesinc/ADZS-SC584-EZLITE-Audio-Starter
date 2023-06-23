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

#ifndef _init_ss_h
#define _init_ss_h

#include "context.h"

typedef bool (*SS_GET)(APP_CONTEXT *context, int pinId, bool *value);
typedef bool (*SS_SET)(APP_CONTEXT *context, int pinId, bool value);

enum SS_PIN_ID {
    SS_PIN_ID_UNKNOWN = -1,
    /* Device 0 */
    SS_PIN_ID_EEPROM_EN,
    SS_PIN_ID_UART0_FLOW_EN,
    SS_PIN_ID_UART0_EN,
    SS_PIN_ID_MLB3_EN,
    SS_PIN_ID_CAN0_EN,
    SS_PIN_ID_CAN1_EN,
    SS_PIN_ID_ADAU1962_EN,
    SS_PIN_ID_ADAU1979_EN,
    SS_PIN_ID_AUDIO_JACK_SEL,
    SS_PIN_ID_SPI2FLASH_CS_EN,
    SS_PIN_ID_SPID2_D3_EN,
    SS_PIN_ID_SPDIF_OPTICAL_EN,
    SS_PIN_ID_SPDIF_DIGITAL_EN,
    
    /* Device 1 */
    SS_PIN_ID_PUSHBUTTON3_EN,
    SS_PIN_ID_PUSHBUTTON2_EN,
    SS_PIN_ID_PUSHBUTTON1_EN,
    SS_PIN_ID_LEDS_EN,
    SS_PIN_ID_FLG0_LOOP,
    SS_PIN_ID_FLG1_LOOP,
    SS_PIN_ID_FLG2_LOOP,
    SS_PIN_ID_FLG3_LOOP,
    SS_PIN_ID_ADAU1977_EN,
    SS_PIN_ID_ADAU1977_FAULT_RST_EN,
    SS_PIN_ID_THUMBWHEEL_OE,
    SS_PIN_ID_ENGINE_RPM_OE,
    SS_PIN_ID_AD2410_MASTER_SLAVE,
    SS_PIN_ID_MAX
};

void ss_init(APP_CONTEXT *context);
bool ss_get(APP_CONTEXT *context, int pinId, bool *value);
bool ss_set(APP_CONTEXT *context, int pinId, bool value);

#endif
