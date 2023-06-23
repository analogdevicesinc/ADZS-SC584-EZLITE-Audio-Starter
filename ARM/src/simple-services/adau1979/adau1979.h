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
#ifndef _adau1979_h
#define _adau1979_h

#include <stdint.h>

#include "twi_simple.h"

// adau1979 service return values
typedef enum
{
    ADAU1979_SUCCESS,  // Successful API call
    ADAU1979_ERROR     // General failure
} ADAU1979_RESULT;

ADAU1979_RESULT init_adau1979(sTWI *twi, uint8_t adau_address);

#endif
