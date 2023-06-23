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
#ifndef _adau1977_h
#define _adau1977_h

#include <stdint.h>

#include "twi_simple.h"

// adau1977 service return values
typedef enum
{
    ADAU1977_SUCCESS,  // Successful API call
    ADAU1977_ERROR     // General failure
} ADAU1977_RESULT;

ADAU1977_RESULT init_adau1977(sTWI *twi, uint8_t adau_address);
ADAU1977_RESULT write_adau1977(sTWI *twi, uint8_t adau_address, uint8_t * pu8Buffer, uint8_t u8Length);
ADAU1977_RESULT read_adau1977(sTWI *twi, uint8_t adau_address, uint8_t adau_register, uint8_t * pu8Buffer, uint8_t u8Length);

#endif
