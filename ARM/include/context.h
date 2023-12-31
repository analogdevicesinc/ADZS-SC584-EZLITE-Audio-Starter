/**
 * Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
 * This software is proprietary and confidential to Analog Devices, Inc.
 * and its licensors.
 *
 * This software is subject to the terms and conditions of the license set
 * forth in the project LICENSE file. Downloading, reproducing, distributing or
 * otherwise using the software constitutes acceptance of the license. The
 * software may not be used except as expressly authorized under the license.
 */

#ifndef _context_h
#define _context_h

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"

#include "spi_simple.h"
#include "twi_simple.h"
#include "uart_simple.h"
#include "uart_simple_cdc.h"
#include "sport_simple.h"
#include "flash.h"
#include "shell.h"
#include "pa_ringbuffer.h"
#include "uac2_soundcard.h"
#include "sae.h"
#include "ipc.h"
#include "wav_file.h"
#include "clock_domain_defs.h"
#include "spiffs.h"

/* Misc defines */
#define UNUSED(expr) do { (void)(expr); } while (0)

/* SAM HW Versions */
#define SAM_VERSION_1  100
#define SAM_VERSION_2  200

/* The priorities assigned to the tasks (higher number == higher prio). */
#define HOUSEKEEPING_PRIORITY       (tskIDLE_PRIORITY + 2)
#define STARTUP_TASK_LOW_PRIORITY   (tskIDLE_PRIORITY + 2)
#define UAC20_TASK_PRIORITY         (tskIDLE_PRIORITY + 3)
#define WAV_TASK_PRIORITY           (tskIDLE_PRIORITY + 3)
#define STARTUP_TASK_HIGH_PRIORITY  (tskIDLE_PRIORITY + 5)

/* The some shell commands require a little more stack (startup task). */
#define STARTUP_TASK_STACK_SIZE    (configMINIMAL_STACK_SIZE + 8192)
#define UAC20_TASK_STACK_SIZE      (configMINIMAL_STACK_SIZE + 128)
#define WAV_TASK_STACK_SIZE        (configMINIMAL_STACK_SIZE + 128)
#define GENERIC_TASK_STACK_SIZE    (configMINIMAL_STACK_SIZE)

/*
 * WARNING: Do not change SYSTEM_AUDIO_TYPE from int32_t
 *
 */
#define SYSTEM_MCLK_RATE               (24576000)
#define SYSTEM_SAMPLE_RATE             (48000)
#define SYSTEM_BLOCK_SIZE              (32)
#define SYSTEM_AUDIO_TYPE              int32_t
#define SYSTEM_MAX_CHANNELS            (32)

#define USB_DEFAULT_IN_AUDIO_CHANNELS  (32)       /* USB IN endpoint audio */
#define USB_DEFAULT_OUT_AUDIO_CHANNELS (32)       /* USB OUT endpoint audio */
#define USB_DEFAULT_WORD_SIZE_BITS     (32)
#define USB_TIMER                      (0)
#define USB_VENDOR_ID                  (0x064b)  /* Analog Devices Vendor ID */
#define USB_PRODUCT_ID                 (0x0007)  /* CLD UAC+CDC */
#define USB_MFG_STRING                 "Analog Devices, Inc."
#define USB_PRODUCT_STRING             "Audio v2.0 Device"
#define USB_SERIAL_NUMBER_STRING       NULL
#define USB_OUT_RING_BUFF_FRAMES       1024
#define USB_IN_RING_BUFF_FRAMES        1024
#define USB_OUT_RING_BUFF_FILL         (USB_OUT_RING_BUFF_FRAMES / 2)
#define USB_IN_RING_BUFF_FILL          (USB_IN_RING_BUFF_FRAMES / 2)

#define WAV_RING_BUF_SAMPLES           (128 * 1024)

#define ADC_AUDIO_CHANNELS             (4)
#define ADC_DMA_CHANNELS               (8)
#define DAC_AUDIO_CHANNELS             (12)
#define DAC_DMA_CHANNELS               (16)

#define MIC_AUDIO_CHANNELS             (4)
#define MIC_DMA_CHANNELS               (8)

#define SPDIF_AUDIO_CHANNELS           (2)
#define SPDIF_DMA_CHANNELS             (2)
#define SPDIF_BLOCK_SIZE               (SYSTEM_BLOCK_SIZE)

#define A2B_AUDIO_CHANNELS             (32)
#define A2B_DMA_CHANNELS               (32)
#define A2B_BLOCK_SIZE                 (SYSTEM_BLOCK_SIZE)

#define AD2425W_SAM_I2C_ADDR           (0x68)
#define ADAU1977_I2C_ADDR              (0x31)

#define SPIFFS_VOL_NAME                "sf:"

typedef enum A2B_BUS_MODE {
    A2B_BUS_MODE_UNKNOWN = 0,
    A2B_BUS_MODE_MASTER,
    A2B_BUS_MODE_SLAVE
} A2B_BUS_MODE;

/* 8 slot packed I2S, both RX and TX serializers enabled */
#define SYSTEM_I2SGCFG                 (0xE4)
#define SYSTEM_I2SCFG                  (0x7F)

/* Audio routing */
#define MAX_AUDIO_ROUTES               (16)

/* Task notification values */
enum {
    UAC2_TASK_NO_ACTION,
    UAC2_TASK_AUDIO_DATA_READY,
};

/* USB Audio OUT (Rx) endpoint stats */
struct _USB_AUDIO_RX_STATS {
    uint32_t usbRxOverRun;
    uint32_t usbRxUnderRun;
    UAC2_ENDPOINT_STATS ep;
};
typedef struct _USB_AUDIO_RX_STATS USB_AUDIO_RX_STATS;

/* USB Audio IN (Tx) endpoint stats */
struct _USB_AUDIO_TX_STATS {
    uint32_t usbTxOverRun;
    uint32_t usbTxUnderRun;
    UAC2_ENDPOINT_STATS ep;
};
typedef struct _USB_AUDIO_TX_STATS USB_AUDIO_TX_STATS;

struct _USB_AUDIO_STATS {
    USB_AUDIO_RX_STATS rx;
    USB_AUDIO_TX_STATS tx;
};
typedef struct _USB_AUDIO_STATS USB_AUDIO_STATS;

typedef struct APP_CFG {
    int usbOutChannels;
    int usbInChannels;
    int usbWordSizeBits;
    bool usbRateFeedbackHack;
} APP_CFG;

/*
 * The main application context.  Used as a container to carry a
 * variety of useful pointers, handles, etc., between various
 * modules and subsystems.
 */
struct _APP_CONTEXT {

    /* Device handles */
    sUART *stdioHandle;
    sSPI *spi2Handle;
    sSPIPeriph *spiFlashHandle;
    FLASH_INFO *flashHandle;
    sTWI *twi0Handle;
    sTWI *twi2Handle;
    sTWI *ad2425TwiHandle;
    sTWI *adau1962TwiHandle;
    sTWI *softSwitchHandle;
    sTWI *adau1977TwiHandle;
    sSPORT *dacSportOutHandle;
    sSPORT *adcSportInHandle;
    sSPORT *spdifSportOutHandle;
    sSPORT *spdifSportInHandle;
    sSPORT *a2bSportOutHandle;
    sSPORT *a2bSportInHandle;
    sSPORT *micSportInHandle;
    spiffs *spiffsHandle;

    /* SHARC status */
    volatile bool sharc0Ready;

    /* Shell context */
    SHELL_CONTEXT shell;

    /* UAC2 related variables and settings including the
     * task handle, audio ring buffers, and IN/OUT
     * enable status.
     *
     * Rx/Tx are from the target's perspective.
     * Rx = UAC2 OUT, Tx = UAC2 IN
     */
    PaUtilRingBuffer *uac2OutRx;
    void *uac2OutRxData;
    PaUtilRingBuffer *uac2InTx;
    void *uac2InTxData;
    bool uac2RxEnabled;
    bool uac2TxEnabled;
    USB_AUDIO_STATS uac2stats;
    UAC2_APP_CONFIG uac2cfg;

    /* SHARC Audio Engine context */
    SAE_CONTEXT *saeContext;

    /* Task handles (used in 'stacks' command) */
    TaskHandle_t houseKeepingTaskHandle;
    TaskHandle_t pollStorageTaskHandle;
    TaskHandle_t uac2TaskHandle;
    TaskHandle_t startupTaskHandle;
    TaskHandle_t idleTaskHandle;
    TaskHandle_t wavSrcTaskHandle;
    TaskHandle_t wavSinkTaskHandle;
    TaskHandle_t a2bSlaveTaskHandle;

    /* A2B XML init items */
    void *a2bInitSequence;
    uint32_t a2bIinitLength;

    /* Audio ping/pong buffer pointers */
    void *codecAudioIn[2];
    void *codecAudioOut[2];
    void *spdifAudioIn[2];
    void *spdifAudioOut[2];
    void *a2bAudioIn[2];
    void *a2bAudioOut[2];
    void *micAudioIn[2];
    void *usbAudioRx[1];
    void *usbAudioTx[1];
    void *wavAudioSrc[1];
    void *wavAudioSink[1];

    /* Audio ping/pong buffer lengths */
    unsigned codecAudioInLen;
    unsigned codecAudioOutLen;
    unsigned spdifAudioInLen;
    unsigned spdifAudioOutLen;
    unsigned a2bAudioInLen;
    unsigned a2bAudioOutLen;
    unsigned micAudioInLen;
    unsigned usbAudioRxLen;
    unsigned usbAudioTxLen;
    unsigned wavAudioSrcLen;
    unsigned wavAudioSinkLen;

    /* SAE buffer pointers */
    SAE_MSG_BUFFER *codecMsgIn[2];
    SAE_MSG_BUFFER *codecMsgOut[2];
    SAE_MSG_BUFFER *spdifMsgIn[2];
    SAE_MSG_BUFFER *spdifMsgOut[2];
    SAE_MSG_BUFFER *a2bMsgIn[2];
    SAE_MSG_BUFFER *a2bMsgOut[2];
    SAE_MSG_BUFFER *micMsgIn[2];
    SAE_MSG_BUFFER *usbMsgRx[1];
    SAE_MSG_BUFFER *usbMsgTx[1];
    SAE_MSG_BUFFER *wavMsgSrc[1];
    SAE_MSG_BUFFER *wavMsgSink[1];

    /* Audio routing table */
    SAE_MSG_BUFFER *routingMsgBuffer;
    IPC_MSG *routingMsg;

    /* Not used */
    APP_CFG cfg;

    /* Current time in mS */
    uint64_t now;

    /* SHARC Cycles */
    uint32_t sharc0Cycles[CLOCK_DOMAIN_MAX];
    uint32_t sharc1Cycles[CLOCK_DOMAIN_MAX];

    /* WAV file related variables and settings */
    WAV_FILE wavSrc;
    WAV_FILE wavSink;
    PaUtilRingBuffer *wavSrcRB;
    void *wavSrcRBData;
    PaUtilRingBuffer *wavSinkRB;
    void *wavSinkRBData;

    /* A2B mode */
    A2B_BUS_MODE a2bmode;
    bool a2bSlaveActive;

    /* Clock domain management */
    uint32_t clockDomainMask[CLOCK_DOMAIN_MAX];
    uint32_t clockDomainActive[CLOCK_DOMAIN_MAX];

};
typedef struct _APP_CONTEXT APP_CONTEXT;

/*
 * Make the application context global for convenience
 */
extern APP_CONTEXT mainAppContext;

#endif
