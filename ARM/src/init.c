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

/* ADI service includes */
#include <services/gpio/adi_gpio.h>
#include <services/int/adi_gic.h>
#include <services/tmr/adi_tmr.h>
#include <services/pwr/adi_pwr.h>

/* ADI processor includes */
#include <sruSC589.h>

/* Standard library includes */
#include <string.h>
#include <assert.h>

#ifdef FREE_RTOS
#include "FreeRTOS.h"
#endif

/* umm_malloc includes  */
#include "umm_malloc.h"
#include "umm_malloc_heaps.h"
#include "umm_malloc_cfg.h"

/* Simple driver includes */
#include "sport_simple.h"
#include "flash.h"
#include "w25q128fv.h"
#include "pcg_simple.h"

/* Simple service includes */
#include "adau1962.h"
#include "adau1979.h"
#include "adau1977.h"
#include "syslog.h"

/* Application includes */
#include "context.h"
#include "clocks.h"
#include "init.h"
#include "codec_audio.h"
#include "spdif_audio.h"
#include "a2b_audio.h"
#include "mic_audio.h"
#include "util.h"
#include "sae_irq.h"
#include "flash_map.h"
#include "clock_domain.h"

/***********************************************************************
 * Audio Clock Initialization
 **********************************************************************/
/* DAI IE Bit definitions (not in any ADI header files) */
#define BITP_PADS0_DAI0_IE_PB06   (6)
#define BITP_PADS0_DAI0_IE_PB07   (7)
#define BITP_PADS0_DAI0_IE_PB08   (8)
#define BITP_PADS0_DAI0_IE_PB13   (13)
#define BITP_PADS0_DAI1_IE_PB03   (3)

#define BITP_PADS0_DAI1_IE_MCLK BITP_PADS0_DAI1_IE_PB03
#define DAI1_MCLK_PIN 3
#define DAI0_MCLK_CRS_PIN 13

#pragma pack(1)
typedef struct SI5356A_REG_DATA {
   uint8_t addr;
   uint8_t value;
} SI5356A_REG_DATA;
#pragma pack()

SI5356A_REG_DATA SI5356_CLK4_24567Mhz[] = {
    { 230,0x04},
    {  74,0x10},
    {  75,0xC2},
    {  76,0x2A},
    {  77,0x00},
    {  78,0x02},
    {  79,0x00},
    {  80,0x00},
    {  81,0x80},
    {  82,0x01},
    {  83,0x00},
    {  84,0x00},
    { 230,0x00}
};

/***********************************************************************
 * System Clock Initialization
 **********************************************************************/
void system_clk_init(void)
{
    ADI_PWR_RESULT ePwrResult;
    uint32_t cclk,sclk,sclk0,sclk1,dclk,oclk;

    /* Initialize ADI power service */
    ePwrResult = adi_pwr_Init(0, OSC_CLK);

    /* Set up core and system clocks to the values in clocks.h */
    ePwrResult = adi_pwr_SetFreq(0, CCLK, SYSCLK);
    ePwrResult = adi_pwr_SetClkDivideRegister(0, ADI_PWR_CLK_DIV_OSEL, OCLK_DIV);

    /* Query primary clocks from CGU0 for confirmation */
    ePwrResult = adi_pwr_GetCoreFreq(0, &cclk);
    ePwrResult = adi_pwr_GetSystemFreq(0, &sclk, &sclk0, &sclk1);
    ePwrResult = adi_pwr_GetDDRClkFreq(0, &dclk);
    ePwrResult = adi_pwr_GetOutClkFreq(0, &oclk);

    /* SPDIF clock is derived from CDU0_CLKO5
     * Choose OCLK_0 (see oclk above)
     */
    ePwrResult = adi_pwr_ConfigCduInputClock(ADI_PWR_CDU_CLKIN_0, ADI_PWR_CDU_CLKOUT_5);
    ePwrResult = adi_pwr_EnableCduClockOutput(ADI_PWR_CDU_CLKOUT_5, true);

}

#define AUDIO_CLK_DAI1_IE (          \
    (1 << BITP_PADS0_DAI1_IE_MCLK)   \
)

void disable_sport_mclk(APP_CONTEXT *context)
{
    *pREG_PADS0_DAI1_IE &= ~(AUDIO_CLK_DAI1_IE);
}

void enable_sport_mclk(APP_CONTEXT *context)
{
    *pREG_PADS0_DAI1_IE |= (AUDIO_CLK_DAI1_IE);
}

static void sru_config_mclk(APP_CONTEXT *context)
{
    /* 24.576Mhz MCLK in from clock generator */
    SRU2(LOW, DAI1_PBEN03_I);
    
    /* SPDIF pins are on DAI0 domain so enable and cross route
     * DAI1_PIN03 to unused pin on DAI0 domain.
     */
     SRU(HIGH, DAI0_PBEN13_I);
     SRU(DAI0_CRS_PB03_O, DAI0_PB13_I);
}

static void pcg_init_dai1_tdm8_bclk(void)
{
 /* Configure static PCG D parameters */
    PCG_SIMPLE_CONFIG pcg = {
        .pcg = PCG_D,                    // PCG D
        .clk_src = PCG_SRC_DAI_PIN,      // Sourced from DAI
        .clk_in_dai_pin = DAI1_MCLK_PIN, // Sourced from DAI pin 3
        .lrclk_clocks_per_frame = 256,   // Not used
        .sync_to_fs = false
    };

    /* Configure a 12.288 MHz BCLK from 24.576 BCLK */
    pcg.bitclk_div = 2;
    pcg_open(&pcg);
    pcg_enable(pcg.pcg, true);
}

void mclk_init(APP_CONTEXT *context)
{
    sru_config_mclk(context);
    pcg_init_dai1_tdm8_bclk();
}


/***********************************************************************
 * GPIO / Pin MUX / SRU Initialization
 **********************************************************************/
/*
 * The port FER and MUX settings are detailed in:
 *    ADSP-SC582_583_584_587_589_ADSP-21583_584_587.pdf
 *
 */

/* SPI2 GPIO FER bit positions (one bit per FER entry) */
#define SPI2_CLK_PORTC_FER   (1 << BITP_PORT_DATA_PX1)
#define SPI2_MISO_PORTC_FER  (1 << BITP_PORT_DATA_PX2)
#define SPI2_MOSO_PORTC_FER  (1 << BITP_PORT_DATA_PX3)
#define SPI2_D2_PORTC_FER    (1 << BITP_PORT_DATA_PX4)
#define SPI2_D3_PORTC_FER    (1 << BITP_PORT_DATA_PX5)
#define SPI2_SEL_PORTC_FER   (1 << BITP_PORT_DATA_PX6)

/* SPI2 GPIO MUX bit positions (two bits per MUX entry) */
#define SPI2_CLK_PORTC_MUX   (0 << (BITP_PORT_DATA_PX1 << 1))
#define SPI2_MISO_PORTC_MUX  (0 << (BITP_PORT_DATA_PX2 << 1))
#define SPI2_MOSO_PORTC_MUX  (0 << (BITP_PORT_DATA_PX3 << 1))
#define SPI2_D2_PORTC_MUX    (0 << (BITP_PORT_DATA_PX4 << 1))
#define SPI2_D3_PORTC_MUX    (0 << (BITP_PORT_DATA_PX5 << 1))
#define SPI2_SEL_PORTC_MUX   (0 << (BITP_PORT_DATA_PX6 << 1))

/* UART0 GPIO FER bit positions */
#define UART0_TX_PORTC_FER   (1 << BITP_PORT_DATA_PX13)
#define UART0_RX_PORTC_FER   (1 << BITP_PORT_DATA_PX14)
#define UART0_RTS_PORTC_FER  (1 << BITP_PORT_DATA_PX15)
#define UART0_CTS_PORTD_FER  (1 << BITP_PORT_DATA_PX0)

/* UART0 GPIO MUX bit positions (two bits per MUX entry) */
#define UART0_TX_PORTC_MUX   (0 << (BITP_PORT_DATA_PX13 << 1))
#define UART0_RX_PORTC_MUX   (0 << (BITP_PORT_DATA_PX14 << 1))
#define UART0_RTS_PORTC_MUX  (0 << (BITP_PORT_DATA_PX15 << 1))
#define UART0_CTS_PORTD_MUX  (0 << (BITP_PORT_DATA_PX0  << 1))

void gpio_init(void)
{
    static uint8_t gpioMemory[ADI_GPIO_CALLBACK_MEM_SIZE];
    uint32_t numCallbacks;

    ADI_GPIO_RESULT  result;
    
    /* Configure SPI2 Alternate Function GPIO */
    *pREG_PORTC_FER |= (
        SPI2_CLK_PORTC_FER |
        SPI2_MISO_PORTC_FER |
        SPI2_MOSO_PORTC_FER |
        SPI2_D2_PORTC_FER |
        SPI2_D3_PORTC_FER |
        SPI2_SEL_PORTC_FER
    );
    *pREG_PORTC_MUX |= (
        SPI2_CLK_PORTC_MUX |
        SPI2_MISO_PORTC_MUX |
        SPI2_MOSO_PORTC_MUX |
        SPI2_D2_PORTC_MUX |
        SPI2_D3_PORTC_MUX |
        SPI2_SEL_PORTC_MUX
    );
    
    /* Configure UART0 Alternate Function GPIO */
    *pREG_PORTC_FER |= (
        UART0_TX_PORTC_FER |
        UART0_RX_PORTC_FER |
        UART0_RTS_PORTC_FER
    );
    *pREG_PORTC_MUX |= (
        UART0_TX_PORTC_MUX |
        UART0_RX_PORTC_MUX |
        UART0_RTS_PORTC_MUX
    );
    *pREG_PORTD_FER |= (
        UART0_CTS_PORTD_FER
    );
    *pREG_PORTD_MUX |= (
        UART0_CTS_PORTD_MUX
    );

    result = adi_gpio_Init(gpioMemory, sizeof(gpioMemory), &numCallbacks);
    
    /* ADC ADAU1979 and DAC ADAU1962A Resets Lines (Reset line is shared) - enable it and make sure it is low to start */
    result = adi_gpio_SetDirection(ADI_GPIO_PORT_A, ADI_GPIO_PIN_14, ADI_GPIO_DIRECTION_OUTPUT);
    result = adi_gpio_Clear(ADI_GPIO_PORT_A, ADI_GPIO_PIN_14);
    
    /* ADC ADAU1977 Reset line */
    result = adi_gpio_SetDirection(ADI_GPIO_PORT_A, ADI_GPIO_PIN_15, ADI_GPIO_DIRECTION_OUTPUT);
    result = adi_gpio_Clear(ADI_GPIO_PORT_A, ADI_GPIO_PIN_15);
    
    /* LEDs PE0-PE8 */
    result = adi_gpio_SetDirection(
             ADI_GPIO_PORT_E, 
             ADI_GPIO_PIN_1 | ADI_GPIO_PIN_2 | ADI_GPIO_PIN_3 | ADI_GPIO_PIN_4 | ADI_GPIO_PIN_5 | ADI_GPIO_PIN_6 | ADI_GPIO_PIN_7 | ADI_GPIO_PIN_8, 
             ADI_GPIO_DIRECTION_OUTPUT);
    
    result = adi_gpio_Clear(
             ADI_GPIO_PORT_E, 
             ADI_GPIO_PIN_1 | ADI_GPIO_PIN_2 | ADI_GPIO_PIN_3 | ADI_GPIO_PIN_4 | ADI_GPIO_PIN_5 | ADI_GPIO_PIN_6 | ADI_GPIO_PIN_7 | ADI_GPIO_PIN_8);
    
    /* Push buttons - PB0, PC15 - Do not use PA15 if using MIC inputs */ 
    result = adi_gpio_SetDirection(ADI_GPIO_PORT_B, ADI_GPIO_PIN_0, ADI_GPIO_DIRECTION_INPUT);
    result = adi_gpio_SetDirection(ADI_GPIO_PORT_C, ADI_GPIO_PIN_15, ADI_GPIO_DIRECTION_INPUT);

    /* PADS0 DAI0/1 Port Input Enable Control Register */
    *pREG_PADS0_DAI0_IE = (unsigned int)0x001FFFFE;
    *pREG_PADS0_DAI1_IE = (unsigned int)0x001FFFFE;
}

/*
 * This macro is used to set the interrupt priority.  Interrupts of a
 * higher priority (lower number) will nest with interrupts of a lower
 * priority (higher number).
 *
 * Priority can range from 0 (highest) to 15 (lowest)
 *
 * Currently only USB interrupts are elevated, all others are lower.
 *
 */
#define INTERRUPT_PRIO(x) \
    ((configMAX_API_CALL_INTERRUPT_PRIORITY + x) << portPRIORITY_SHIFT)

/***********************************************************************
 * GIC Initialization
 **********************************************************************/
void gic_init(void)
{
    ADI_GIC_RESULT  result;

    result = adi_gic_Init();

#ifdef FREE_RTOS
    uint32_t saeIrq = sae_getInterruptID();
    /*
     * Setup peripheral interrupt priorities:
     *   Details: FreeRTOSv9.0.0/portable/GCC/ARM_CA9/port.c (line 574)
     *
     * All registered system interrupts can be identified by setting a breakpoint in
     * adi_rtl_register_dispatched_handler().
     *
     * Interrupts that need to call FreeRTOS functions, or that must be suspended during
     * critical section processing, must be registered with the required interrupt priority.
     *
     * If you end up in vAssertCalled() from vPortValidateInterruptPriority() then
     * the offending interrupt must be added here.  The interrupt ID (iid) can be found
     * by looking backwards in the call stack in vApplicationIRQHandler().
     *
     */
    adi_gic_SetBinaryPoint(ADI_GIC_CORE_0, 0);
    adi_gic_SetIntPriority(INTR_SPI0_STAT, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPI1_STAT, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPI2_STAT, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_TWI0_DATA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_TWI1_DATA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_TWI2_DATA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_UART0_STAT, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_UART1_STAT, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_UART2_STAT, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT0_A_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT0_B_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT1_A_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT1_B_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT2_A_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT2_B_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT6_A_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT6_B_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT4_A_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_SPORT4_B_DMA, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_MSI0_STAT, INTERRUPT_PRIO(1));

    /* TMR0, INTR_USB0_DATA is used for UAC2.0 */
    adi_gic_SetIntPriority(INTR_TIMER0_TMR0, INTERRUPT_PRIO(1));
    adi_gic_SetIntPriority(INTR_USB0_DATA, INTERRUPT_PRIO(0));
    adi_gic_SetIntPriority(INTR_USB0_STAT, INTERRUPT_PRIO(0));

    /* SHARC Audio Engine IRQ */
    adi_gic_SetIntPriority(saeIrq, INTERRUPT_PRIO(1));

    /* HADC0 interrupt */
    adi_gic_SetIntPriority(INTR_HADC0_EVT, INTERRUPT_PRIO(1));

    /* MSI0 interrupt */
    adi_gic_SetIntPriority(INTR_MSI0_STAT, INTERRUPT_PRIO(1));

    /* WARNING: The ADI FreeRTOS port uses TMR7 as the tick timer which must be configured as the lowest
     *          priority interrupt.  If you're using the stock ADI v9.0.0 or v10.0.1 port, be sure to
     *          enable the line below.  This countermeasure has been applied to the reusable module
     *          FreeRTOS v10.2.1.
     *
     *    The SysTick handler needs to run at the lowest priority.  This is because the critical section
     *    within the handler itself assumes it is running at the lowest priority, so saves time by not
     *    saving the old priority mask and then restoring the previous priority mask.
     */
    //adi_gic_SetIntPriority(INTR_TIMER0_TMR7, 30 << portPRIORITY_SHIFT);
#endif
}

/***********************************************************************
 * libc heap initialization
 **********************************************************************/
#ifndef STD_C_HEAP_SIZE
#define STD_C_HEAP_SIZE (1024 * 1024)
#endif
uint8_t __adi_heap_object[STD_C_HEAP_SIZE] __attribute__ ((section (".heap")));

/***********************************************************************
 * UMM_MALLOC heap initialization
 **********************************************************************/
__attribute__ ((section(".heap")))
    static uint8_t umm_sdram_heap[UMM_SDRAM_HEAP_SIZE];

__attribute__ ((section(".l3_uncached_data")))
    static uint8_t umm_sdram_uncached_heap[UMM_SDRAM_UNCACHED_HEAP_SIZE];

__attribute__ ((section(".l2_uncached_data")))
    static uint8_t umm_l2_uncached_heap[UMM_L2_UNCACHED_HEAP_SIZE];

__attribute__ ((section(".l2_cached_data")))
    static uint8_t umm_l2_cached_heap[UMM_L2_CACHED_HEAP_SIZE];

void heap_init(void)
{
    /* Initialize the cached L3 SDRAM heap (default heap). */
    umm_init(UMM_SDRAM_HEAP, umm_sdram_heap, UMM_SDRAM_HEAP_SIZE);

    /* Initialize the un-cached L3 SDRAM heap. */
    umm_init(UMM_SDRAM_UNCACHED_HEAP, umm_sdram_uncached_heap,
        UMM_SDRAM_UNCACHED_HEAP_SIZE);

    /* Initialize the L2 uncached heap. */
    umm_init(UMM_L2_UNCACHED_HEAP, umm_l2_uncached_heap,
        UMM_L2_UNCACHED_HEAP_SIZE);

    /* Initialize the L2 cached heap. */
    umm_init(UMM_L2_CACHED_HEAP, umm_l2_cached_heap, UMM_L2_CACHED_HEAP_SIZE);
}

/***********************************************************************
 * SPI Flash initialization
 **********************************************************************/
void flash_init(APP_CONTEXT *context)
{
    SPI_SIMPLE_RESULT spiResult;

    /* Open a SPI handle to SPI2 */
    spiResult = spi_open(SPI2, &context->spi2Handle);

    /* Open a SPI2 device handle for the flash */
    spiResult = spi_openDevice(context->spi2Handle, &context->spiFlashHandle);

    /* Configure the flash device handle */
    spiResult = spi_setClock(context->spiFlashHandle, 1);
    spiResult = spi_setMode(context->spiFlashHandle, SPI_MODE_3);
    spiResult = spi_setFastMode(context->spiFlashHandle, true);
    spiResult = spi_setLsbFirst(context->spiFlashHandle, false);
    spiResult = spi_setSlaveSelect(context->spiFlashHandle, SPI_SSEL_1);

    /* 
     * SC584 EZLIT uses the W25Q128 Flash Chip
     */
    context->flashHandle = w25q128fv_open(context->spiFlashHandle);
}

/***********************************************************************
 * CGU Timestamp init
 **********************************************************************/
void cgu_ts_init(void)
{
    /* Configure the CGU timestamp counter.  See clocks.h for more detail. */
    *pREG_CGU0_TSCTL =
        ( 1 << BITP_CGU_TSCTL_EN ) |
        ( CGU_TS_DIV << BITP_CGU_TSCTL_TSDIV );
}

/***********************************************************************
 * This function allocates audio buffers in L3 cached memory and
 * initializes a single SPORT using the simple SPORT driver.
 **********************************************************************/
static sSPORT *single_sport_init(SPORT_SIMPLE_PORT sport,
    SPORT_SIMPLE_CONFIG *cfg, SPORT_SIMPLE_AUDIO_CALLBACK cb,
    void **pingPongPtrs, unsigned *pingPongLen, void *usrPtr,
    bool cached, SPORT_SIMPLE_RESULT *result)
{
    sSPORT *sportHandle;
    SPORT_SIMPLE_RESULT sportResult;
    uint32_t dataBufferSize;

    /* Open a handle to the SPORT */
    sportResult = sport_open(sport, &sportHandle);
    if (sportResult != SPORT_SIMPLE_SUCCESS) {
        if (result) {
            *result = sportResult;
        }
        return(NULL);
    }

    /* Copy application callback info */
    cfg->callBack = cb;
    cfg->usrPtr = usrPtr;

    /* Allocate audio buffers if not already allocated */
    dataBufferSize = sport_buffer_size(cfg);
    if (!cfg->dataBuffers[0]) {
        cfg->dataBuffers[0] = umm_malloc_heap_aligned(
            UMM_SDRAM_HEAP, dataBufferSize, sizeof(uint32_t));
        memset(cfg->dataBuffers[0], 0, dataBufferSize);
    }
    if (!cfg->dataBuffers[1]) {
        cfg->dataBuffers[1] = umm_malloc_heap_aligned(
            UMM_SDRAM_HEAP, dataBufferSize, sizeof(uint32_t));
        memset(cfg->dataBuffers[1], 0, dataBufferSize);
    }
    cfg->dataBuffersCached = cached;

    /* Configure the SPORT */
    sportResult = sport_configure(sportHandle, cfg);

    /* Save ping pong data pointers */
    if (pingPongPtrs) {
        pingPongPtrs[0] = cfg->dataBuffers[0];
        pingPongPtrs[1] = cfg->dataBuffers[1];
    }
    if (pingPongLen) {
        *pingPongLen = dataBufferSize;
    }
    if (result) {
        *result = sportResult;
    }

    return(sportHandle);
}

/***********************************************************************
 * Simple SPORT driver 8/16-ch packed I2S settings
 * Compatible A2B I2S Register settings:
 * 8 ch
 *    I2SGCFG: 0xE2
 *     I2SCFG: 0x7F
 * 16 ch
 *    I2SGCFG: 0xE4
 *     I2SCFG: 0x7F
 **********************************************************************/
SPORT_SIMPLE_CONFIG cfg8chPackedI2S = {
    .clkDir = SPORT_SIMPLE_CLK_DIR_SLAVE,
    .fsDir = SPORT_SIMPLE_FS_DIR_SLAVE,
    .bitClkOptions = SPORT_SIMPLE_CLK_FALLING,
    .fsOptions = SPORT_SIMPLE_FS_OPTION_INV | SPORT_SIMPLE_FS_OPTION_EARLY |
                 SPORT_SIMPLE_FS_OPTION_50,
    .tdmSlots = SPORT_SIMPLE_TDM_8,
    .wordSize = SPORT_SIMPLE_WORD_SIZE_32BIT,
    .dataEnable = SPORT_SIMPLE_ENABLE_BOTH,
    .frames = SYSTEM_BLOCK_SIZE,
    .syncDMA = true
};

SPORT_SIMPLE_CONFIG cfg16chPackedI2S = {
    .clkDir = SPORT_SIMPLE_CLK_DIR_SLAVE,
    .fsDir = SPORT_SIMPLE_FS_DIR_SLAVE,
    .bitClkOptions = SPORT_SIMPLE_CLK_FALLING,
    .fsOptions = SPORT_SIMPLE_FS_OPTION_INV | SPORT_SIMPLE_FS_OPTION_EARLY |
                 SPORT_SIMPLE_FS_OPTION_50,
    .tdmSlots = SPORT_SIMPLE_TDM_16,
    .wordSize = SPORT_SIMPLE_WORD_SIZE_32BIT,
    .dataEnable = SPORT_SIMPLE_ENABLE_BOTH,
    .frames = SYSTEM_BLOCK_SIZE,
    .syncDMA = true
};

/***********************************************************************
 * ADAU1962 DAC / SPORT4 / SRU initialization (TDM16 clock slave)
 **********************************************************************/
#define ADAU1962_I2C_ADDR  (0x04)

static void sru_config_adau1962_slave(void)
{
    /* The following is the TDM pin mapping:
     * MCLK  = DAI1_PIN03
     * BCLK  = DAI1_PIN02
     * LRCLK = DAI1_PIN04
     * DATA1 = DAI1_PIN01
     * DATA2 = DAI1_PIN05
     */
     
    /* Setup DAI Pin I/O */
    SRU2(HIGH, DAI1_PBEN01_I);      // ADAU1962 DAC data1 is an output
    SRU2(HIGH, DAI1_PBEN05_I);      // ADAU1962 DAC data2 is an output
    SRU2(HIGH, DAI1_PBEN04_I);      // ADAU1962 FS is an output
    SRU2(HIGH, DAI1_PBEN02_I);      // ADAU1962 CLK is an output

    /* Route clocks */
    SRU2(SPT4_AFS_O, DAI1_PB04_I);   // route SPORT4A frame sync to ADAU1962
    SRU2(DAI1_PB03_O, DAI1_PB02_I);  // Route TDM16 BCLK to ADAU1962 CLK

    /* Route to SPORT4A */
    SRU2(SPT4_AD0_O, DAI1_PB01_I);    // SPORT4A-D0 output to ADAU1962 data1 pin
    SRU2(SPT4_AD1_O, DAI1_PB05_I);    // SPORT4A-D1 output to ADAU1962 data2 pin
}

static void adau1962_sport_init(APP_CONTEXT *context)
{
    SPORT_SIMPLE_CONFIG sportCfg;
    SPORT_SIMPLE_RESULT sportResult;
    unsigned len;

    /* SPORT4A: DAC 16-ch packed I2S data out */
    sportCfg = cfg16chPackedI2S;
    sportCfg.dataDir = SPORT_SIMPLE_DATA_DIR_TX;
    sportCfg.dataEnable = SPORT_SIMPLE_ENABLE_PRIMARY;
    sportCfg.fsDir = SPORT_SIMPLE_FS_DIR_MASTER;
    memcpy(sportCfg.dataBuffers, context->codecAudioOut, sizeof(sportCfg.dataBuffers));
    context->dacSportOutHandle = single_sport_init(
        SPORT4A, &sportCfg, dacAudioOut,
        NULL, &len, context, false, NULL
    );
    assert(context->codecAudioOutLen == len);

    if (context->dacSportOutHandle) {
        sportResult = sport_start(context->dacSportOutHandle, true);
        assert(sportResult == SPORT_SIMPLE_SUCCESS);
    }
}

static void adau1962_sport_deinit(APP_CONTEXT *context)
{
    if (context->dacSportOutHandle) {
        sport_close(&context->dacSportOutHandle);
    }
}

void adau1962_init(APP_CONTEXT *context)
{
    /* Take the ADC and DAC out of reset */
    adi_gpio_Set(ADI_GPIO_PORT_A, ADI_GPIO_PIN_14);
    
    /* Configure the DAI routing */
    sru_config_adau1962_slave();

    /* Configure the SPORT */
    adau1962_sport_init(context);
    
    /* Wait for reset stability */
    delay(300);

    /* Initialize the DAC */
    init_adau1962(context->adau1962TwiHandle, ADAU1962_I2C_ADDR);
}

/***********************************************************************
 * ADAU1979 ADC / SPORT6A / SRU initialization (TDM8 clock slave)
 *
 * WARNING: The ADAU1979 does not have the drive strength to reliably
 *          drive data out with a TDM16 bit clock.
 **********************************************************************/
#define ADAU1979_I2C_ADDR  (0x11)

static void sru_config_adau1979_slave(void)
{
    /* Setup DAI Pin I/O */
    SRU2(LOW, DAI1_PBEN06_I);      // ADAU1979 ADC data1 is an input
    SRU2(LOW, DAI1_PBEN07_I);      // ADAU1979 ADC data2 is an input
    SRU2(HIGH, DAI1_PBEN12_I);     // ADAU1979 CLK is an output
    SRU2(HIGH, DAI1_PBEN20_I);     // ADAU1979 FS is an output

    /* Route audio clocks to ADAU1979 */
    SRU2(SPT6_AFS_O, DAI1_PB20_I);   // route SPORT6A FS to ADAU1979 FS
    SRU2(PCG0_CLKD_O, DAI1_PB12_I);  // route TDM8 BCLK to ADAU1979 BCLK

    /* Route to SPORT6A */
    SRU2(DAI1_PB20_O, SPT6_AFS_I);   // route ADAU1979 FS to SPORT6A frame sync
    SRU2(DAI1_PB12_O, SPT6_ACLK_I);  // route ADAU1979 BCLK to SPORT6A clock input
    SRU2(DAI1_PB06_O, SPT6_AD0_I);   // ADAU1979 SDATAOUT1 pin to SPORT6A data0
    SRU2(DAI1_PB07_O, SPT6_AD1_I);   // ADAU1979 SDATAOUT2 pin to SPORT6A data1
}

static void adau1979_sport_init(APP_CONTEXT *context)
{
    SPORT_SIMPLE_CONFIG sportCfg;
    SPORT_SIMPLE_RESULT sportResult;
    unsigned len;

    /* SPORT6A: ADC 8-ch packed I2S data in */
    sportCfg = cfg8chPackedI2S;
    sportCfg.dataDir = SPORT_SIMPLE_DATA_DIR_RX;
    sportCfg.dataEnable = SPORT_SIMPLE_ENABLE_PRIMARY;
    sportCfg.fsDir = SPORT_SIMPLE_FS_DIR_MASTER;
    memcpy(sportCfg.dataBuffers, context->codecAudioIn, sizeof(sportCfg.dataBuffers));
    context->adcSportInHandle = single_sport_init(
        SPORT6A, &sportCfg, adcAudioIn,
        NULL, &len, context, true, NULL
    );
    assert(context->codecAudioInLen == len);

    if (context->adcSportInHandle) {
        sportResult = sport_start(context->adcSportInHandle, true);
        assert(sportResult == SPORT_SIMPLE_SUCCESS);
    }
}

static void adau1979_sport_deinit(APP_CONTEXT *context)
{
    if (context->adcSportInHandle) {
        sport_close(&context->adcSportInHandle);
    }
}

void adau1979_init(APP_CONTEXT *context)
{
    /* Take the ADC and DAC out of reset */
    adi_gpio_Set(ADI_GPIO_PORT_A, ADI_GPIO_PIN_14);
    
    /* Configure the DAI routing */
    sru_config_adau1979_slave();

    /* Configure the SPORT */
    adau1979_sport_init(context);
    
    /* Wait for reset stability */
    delay(40);

    /* Initialize the ADC */
    init_adau1979(context->adau1962TwiHandle, ADAU1979_I2C_ADDR);
}

/***********************************************************************
 * ADAU1977 ADC / SPORT6B / SRU initialization (TDM8 clock slave)
 *
 * WARNING: The ADAU1977 does not have the drive strength to reliably
 *          drive data out with a TDM16 bit clock.
 **********************************************************************/
static void sru_config_adau1977_slave(void)
{
    /* Setup DAI Pin I/O */
    SRU2(LOW, DAI1_PBEN10_I);      // ADAU1977 ADC data1 is an input
    SRU2(LOW, DAI1_PBEN11_I);      // ADAU1977 ADC data2 is an input
    SRU2(HIGH, DAI1_PBEN09_I);     // ADAU1977 CLK is an output
    SRU2(HIGH, DAI1_PBEN08_I);     // ADAU1977 FS is an output

    /* Route audio clocks to ADAU1977 */
    SRU2(SPT6_BFS_O, DAI1_PB08_I);   // route SPORT6B FS to ADAU1977 FS
    SRU2(PCG0_CLKD_O, DAI1_PB09_I);  // route TDM8 BCLK to ADAU1977 BCLK

    /* Route to SPORT6B */
    SRU2(DAI1_PB08_O, SPT6_BFS_I);   // route ADAU1977 FS to SPORT6B frame sync
    SRU2(DAI1_PB09_O, SPT6_BCLK_I);  // route ADAU1977 BCLK to SPORT6B clock input
    SRU2(DAI1_PB10_O, SPT6_BD0_I);   // ADAU1977 SDATAOUT1 pin to SPORT6B data0
    SRU2(DAI1_PB11_O, SPT6_BD1_I);   // ADAU1977 SDATAOUT2 pin to SPORT6B data1
}

static void adau1977_sport_init(APP_CONTEXT *context)
{
    SPORT_SIMPLE_CONFIG sportCfg;
    SPORT_SIMPLE_RESULT sportResult;
    unsigned len;

    /* SPORT6B: ADC 8-ch packed I2S data in */
    sportCfg = cfg8chPackedI2S;
    sportCfg.dataDir = SPORT_SIMPLE_DATA_DIR_RX;
    sportCfg.dataEnable = SPORT_SIMPLE_ENABLE_PRIMARY;
    sportCfg.fsDir = SPORT_SIMPLE_FS_DIR_MASTER;
    memcpy(sportCfg.dataBuffers, context->micAudioIn, sizeof(sportCfg.dataBuffers));
    context->micSportInHandle = single_sport_init(
        SPORT6B, &sportCfg, micAudioIn,
        NULL, &len, context, true, NULL
    );
    assert(context->micAudioInLen == len);

    if (context->micSportInHandle) {
        sportResult = sport_start(context->micSportInHandle, true);
        assert(sportResult == SPORT_SIMPLE_SUCCESS);
    }
}

static void adau1977_sport_deinit(APP_CONTEXT *context)
{
    if (context->micSportInHandle) {
        sport_close(&context->micSportInHandle);
    }
}

void adau1977_init(APP_CONTEXT *context)
{
    /* Take the ADC and DAC out of reset */
    adi_gpio_Set(ADI_GPIO_PORT_A, ADI_GPIO_PIN_15);
    
    /* Configure the DAI routing */
    sru_config_adau1977_slave();

    /* Configure the SPORT */
    adau1977_sport_init(context);
    
    /* Wait for reset stability */
    delay(40);

    /* Initialize the ADC */
    init_adau1977(context->adau1977TwiHandle, ADAU1977_I2C_ADDR);
}

/**************************************************************************
 * SPDIF Init
 *************************************************************************/
SPORT_SIMPLE_CONFIG cfgI2Sx1 = {
    .clkDir = SPORT_SIMPLE_CLK_DIR_SLAVE,
    .fsDir = SPORT_SIMPLE_FS_DIR_MASTER,
    .dataDir = SPORT_SIMPLE_DATA_DIR_UNKNOWN,
    .bitClkOptions = SPORT_SIMPLE_CLK_FALLING,
    .fsOptions = SPORT_SIMPLE_FS_OPTION_INV | SPORT_SIMPLE_FS_OPTION_EARLY |
                 SPORT_SIMPLE_FS_OPTION_50,
    .tdmSlots = SPORT_SIMPLE_TDM_2,
    .wordSize = SPORT_SIMPLE_WORD_SIZE_32BIT,
    .dataEnable = SPORT_SIMPLE_ENABLE_PRIMARY,
    .frames = SYSTEM_BLOCK_SIZE,
};

/* PCGB generates 3.072 MHz I2S BCLK from 24.576 MCLK/BCLK 
 * and PCGA generates a 12.288MHz from CRS PIN03
 */
void spdif_cfg_pcg(void)
{
    /* Configure static PCG B parameters */
    PCG_SIMPLE_CONFIG pcg_b = {
        .pcg = PCG_B,                        // PCG B
        .clk_src = PCG_SRC_DAI_PIN,          // Sourced from DAI
        .clk_in_dai_pin = DAI0_MCLK_CRS_PIN, // Sourced from DAI0 pin 13
        .lrclk_clocks_per_frame = 256,       // Not used
        .sync_to_fs = false
    };

    /* Configure the PCG BCLK depending on the cfgI2Sx1 SPORT config */
    pcg_b.bitclk_div =
        SYSTEM_MCLK_RATE / (cfgI2Sx1.wordSize * cfgI2Sx1.tdmSlots * SYSTEM_SAMPLE_RATE);
    assert(pcg_b.bitclk_div > 0);

    /* This sets everything up */
    pcg_open(&pcg_b);
    pcg_enable(PCG_B, true);
    
    /* Configure static PCG C parameters */
    PCG_SIMPLE_CONFIG pcg_a = {
        .pcg = PCG_A,                        // PCG A
        .clk_src = PCG_SRC_DAI_PIN,          // Sourced from DAI
        .clk_in_dai_pin = DAI0_MCLK_CRS_PIN, // Sourced from DAI0 pin 13
        .lrclk_clocks_per_frame = 256,       // Not used
        .sync_to_fs = false
    };

    /* Configure the PCG BCLK depending on the cfgI2Sx1 SPORT config */
    pcg_a.bitclk_div = 2;

    /* This sets everything up */
    pcg_open(&pcg_a);
    pcg_enable(PCG_A, true);
}

/*
 * WARNING: The SPDIF HFCLK is derived from the TDM8 clock
 *          PCG0_CLKA_O (12.288MHz)
 *
 * WARNING: The SPDIF BCLK is derived from the PCG0_CLKB_O (3.072MHz)
 *
 */
static void spdif_sru_config(void)
{
    // Assign SPDIF I/O pins
    SRU(HIGH, DAI0_PBEN20_I);       // SPDIF TX is an output
    SRU(LOW,  DAI0_PBEN19_I);       // SPDIF RX is an input

    // Connect I/O pins to SPDIF module
    SRU(DAI0_PB19_O, SPDIF0_RX_I);  // route DAI0_PB19 to SPDIF RX
    SRU(SPDIF0_TX_O, DAI0_PB20_I);  // route SPDIF TX to DAI0_PB20

    // Connect 64Fs BCLK to SPORT2A/B
    SRU(PCG0_CLKB_O, SPT2_ACLK_I);     // route PCG 64fs BCLK signal to SPORT2A BCLK
    SRU(PCG0_CLKB_O, SPT2_BCLK_I);     // route PCG 64fs BCLK signal to SPORT2B BCLK

    // Connect SPDIF RX to SRC 0 "IP" side
    SRU(SPDIF0_RX_CLK_O, SRC0_CLK_IP_I);     // route SPDIF RX BCLK to SRC IP BCLK
    SRU(SPDIF0_RX_FS_O,  SRC0_FS_IP_I);      // route SPDIF RX FS to SRC IP FS
    SRU(SPDIF0_RX_DAT_O, SRC0_DAT_IP_I);     // route SPDIF RX Data to SRC IP Data

    // Connect SPORT2B to SRC 0 "OP" side
    SRU(PCG0_CLKB_O,   SRC0_CLK_OP_I);     // route PCG 64fs BCLK signal to SRC OP BCLK
    SRU(SPT2_BFS_O,    SRC0_FS_OP_I);      // route PCG FS signal to SRC OP FS
    SRU(SRC0_DAT_OP_O, SPT2_BD0_I);        // route SRC0 OP Data output to SPORT 2B data

    // Connect 256Fs MCLK to SPDIF TX
    SRU(PCG0_CLKA_O, SPDIF0_TX_HFCLK_I);   // route PCGA_CLK to SPDIF TX HFCLK

    // Connect SPORT2A to SPDIF TX
    SRU(PCG0_CLKB_O, SPDIF0_TX_CLK_I);    // route 64fs BCLK signal to SPDIF TX BCLK
    SRU(SPT2_AFS_O,  SPDIF0_TX_FS_I);     // route SPORT2A FS signal to SPDIF TX FS
    SRU(SPT2_AD0_O,  SPDIF0_TX_DAT_I);    // SPT2A AD0 output to SPDIF TX data pin
}

void spdif_sport_deinit(APP_CONTEXT *context)
{
    if (context->spdifSportOutHandle) {
        sport_close(&context->spdifSportOutHandle);
    }
    if (context->spdifSportInHandle) {
        sport_close(&context->spdifSportInHandle);
    }
}

void spdif_sport_init(APP_CONTEXT *context)
{
    SPORT_SIMPLE_CONFIG sportCfg;
    SPORT_SIMPLE_RESULT sportResult;
    unsigned len;

    /* SPORT2A: SPDIF data out */
    sportCfg = cfgI2Sx1;
    sportCfg.dataDir = SPORT_SIMPLE_DATA_DIR_TX;
    sportCfg.dataBuffersCached = false;
    memcpy(sportCfg.dataBuffers, context->spdifAudioOut, sizeof(sportCfg.dataBuffers));
    context->spdifSportOutHandle = single_sport_init(
        SPORT2A, &sportCfg, spdifAudioOut,
        NULL, &len, context, false, NULL
    );
    assert(context->spdifAudioOutLen == len);


    /* SPORT2B: SPDIF data in */
    sportCfg = cfgI2Sx1;
    sportCfg.dataDir = SPORT_SIMPLE_DATA_DIR_RX;
    sportCfg.dataBuffersCached = false;
    memcpy(sportCfg.dataBuffers, context->spdifAudioIn, sizeof(sportCfg.dataBuffers));
    context->spdifSportInHandle = single_sport_init(
        SPORT2B, &sportCfg, spdifAudioIn,
        NULL, &len, context, false, NULL
    );
    assert(context->spdifAudioInLen == len);

    /* Start SPORT0A/B */
    sportResult = sport_start(context->spdifSportOutHandle, true);
    sportResult = sport_start(context->spdifSportInHandle, true);
}

void spdif_asrc_init(void)
{
    // Configure and enable SRC 0/1
    *pREG_ASRC0_CTL01 =
        BITM_ASRC_CTL01_EN0 |                // Enable SRC0
        (0x1 << BITP_ASRC_CTL01_SMODEIN0) |  // Input mode = I2S
        (0x1 << BITP_ASRC_CTL01_SMODEOUT0) | // Output mode = I2S
        0;

    // Configure and enable SPDIF RX
    *pREG_SPDIF0_RX_CTL =
        BITM_SPDIF_RX_CTL_EN |          // Enable the SPDIF RX
        BITM_SPDIF_RX_CTL_FASTLOCK |    // Enable SPDIF Fastlock (see HRM 32-15)
        BITM_SPDIF_RX_CTL_RSTRTAUDIO |
        0;

    // Configure SPDIF Transmitter in auto mode
    *pREG_SPDIF0_TX_CTL =
        (0x1 << BITP_SPDIF_TX_CTL_SMODEIN) |  // I2S Mode
        BITM_SPDIF_TX_CTL_AUTO |             // Standalone mode
        0;

    // Enable SPDIF transmitter
    *pREG_SPDIF0_TX_CTL |=
        BITM_SPDIF_TX_CTL_EN |         // Enable SPDIF TX
        0;
}

void spdif_init(APP_CONTEXT *context)
{
    /* Configure the DAI routing */
    spdif_sru_config();

    /* Initialize the SPDIF HFCLK PCG */
    spdif_cfg_pcg();

    /* Initialize the SPDIF and ASRC modules */
    spdif_asrc_init();

    /* Initialize the SPORTs */
    spdif_sport_init(context);
}

/***********************************************************************
 * AD2425 / SPORT1 / SRU initialization
 **********************************************************************/
bool ad2425_to_sport_cfg(bool master, bool rxtx,
    uint8_t I2SGCFG, uint8_t I2SCFG, SPORT_SIMPLE_CONFIG *sportCfg,
    bool verbose)
{
    SPORT_SIMPLE_CONFIG backup;
    bool ok = false;
    uint8_t bits;

    if (!sportCfg) {
        goto abort;
    }

    if (verbose) { syslog_print("A2B SPORT CFG"); }

    /* Save a backup in case of failure */
    memcpy(&backup, sportCfg, sizeof(backup));

    /* Reset elements that are configured */
    sportCfg->clkDir = SPORT_SIMPLE_CLK_DIR_UNKNOWN;
    sportCfg->fsDir = SPORT_SIMPLE_FS_DIR_UNKNOWN;
    sportCfg->dataDir = SPORT_SIMPLE_DATA_DIR_UNKNOWN;
    sportCfg->tdmSlots = SPORT_SIMPLE_TDM_UNKNOWN;
    sportCfg->wordSize = SPORT_SIMPLE_WORD_SIZE_UNKNOWN;
    sportCfg->dataEnable = SPORT_SIMPLE_ENABLE_NONE;
    sportCfg->bitClkOptions = SPORT_SIMPLE_CLK_DEFAULT;
    sportCfg->fsOptions = SPORT_SIMPLE_FS_OPTION_DEFAULT;

    /*
     * Set .clkDir, .fsDir, .dataDir
     *
     * if master, set clk/fs to master, else slave
     * if rxtx, set to input, else output
     *
     */
    if (master) {
        sportCfg->clkDir = SPORT_SIMPLE_CLK_DIR_MASTER;
        sportCfg->fsDir = SPORT_SIMPLE_FS_DIR_MASTER;
    } else {
        sportCfg->clkDir = SPORT_SIMPLE_CLK_DIR_SLAVE;
        sportCfg->fsDir = SPORT_SIMPLE_FS_DIR_SLAVE;
    }
    if (rxtx) {
        sportCfg->dataDir = SPORT_SIMPLE_DATA_DIR_RX;
        if (verbose) { syslog_print(" Direction: RX (AD24xx DTX pins)"); }
    } else {
        sportCfg->dataDir = SPORT_SIMPLE_DATA_DIR_TX;
        if (verbose) { syslog_print(" Direction: TX (AD24xx DRX pins)"); }
    }

    /*
     * Set .wordSize
     *
     */
    if (I2SGCFG & 0x10) {
        sportCfg->wordSize = SPORT_SIMPLE_WORD_SIZE_16BIT;
        if (verbose) { syslog_print(" Size: 16-bit"); }
    } else {
        sportCfg->wordSize = SPORT_SIMPLE_WORD_SIZE_32BIT;
        if (verbose) { syslog_print(" Size: 32-bit"); }
    }

    /*
     * Set .tdmSlots
     */
    switch (I2SGCFG & 0x07) {
        case 0:
            sportCfg->tdmSlots = SPORT_SIMPLE_TDM_2;
            if (verbose) { syslog_print(" TDM: 2 (I2S)"); }
            break;
        case 1:
            sportCfg->tdmSlots = SPORT_SIMPLE_TDM_4;
            if (verbose) { syslog_print(" TDM: 4"); }
            break;
        case 2:
            sportCfg->tdmSlots = SPORT_SIMPLE_TDM_8;
            if (verbose) { syslog_print(" TDM: 8"); }
            break;
        case 4:
            sportCfg->tdmSlots = SPORT_SIMPLE_TDM_16;
            if (verbose) { syslog_print(" TDM: 16"); }
            break;
        case 7:
            /*
             * TDM32 with 32-bit word size is not supported with a
             * 24.576MCLK
             */
            if (sportCfg->wordSize == SPORT_SIMPLE_WORD_SIZE_32BIT) {
                goto abort;
            }
            sportCfg->tdmSlots = SPORT_SIMPLE_TDM_32;
            if (verbose) { syslog_print(" TDM: 32"); }
            break;
        default:
            goto abort;
    }

    /*
     * Set .dataEnable
     *
     */
    if (rxtx) {
        bits = I2SCFG >> 0;
    } else {
        bits = I2SCFG >> 4;
    }
    switch (bits & 0x03) {
        case 0x01:
            sportCfg->dataEnable = SPORT_SIMPLE_ENABLE_PRIMARY;
            if (verbose) { syslog_print(" Data Pins: Primary"); }
            break;
        case 0x02:
            sportCfg->dataEnable = SPORT_SIMPLE_ENABLE_SECONDARY;
            if (verbose) { syslog_print(" Data Pins: Secondary"); }
            break;
        case 0x03:
            sportCfg->dataEnable = SPORT_SIMPLE_ENABLE_BOTH;
            if (verbose) {
                syslog_print(" Data Pins: Both");
                syslog_printf(" Interleave: %s", (bits & 0x04) ? "Yes" : "No");
            }
            break;
        default:
            sportCfg->dataEnable = SPORT_SIMPLE_ENABLE_NONE;
            if (verbose) { syslog_print(" Data Pins: None"); }
            break;
    }

    /*
     * Set .bitClkOptions
     *
     * Default setting is assert on the rising edge, sample on falling (TDM)
     *
     */
    if (rxtx) {
        if ((I2SCFG & 0x80) == 0) {
            sportCfg->bitClkOptions |= SPORT_SIMPLE_CLK_FALLING;
            if (verbose) { syslog_print(" CLK: Assert falling, Sample rising (I2S)"); }
        } else {
            if (verbose) { syslog_print(" CLK: Assert rising, Sample falling"); }
        }
    } else {
        if (I2SCFG & 0x08) {
            sportCfg->bitClkOptions |= SPORT_SIMPLE_CLK_FALLING;
            if (verbose) { syslog_print(" CLK: Assert falling, Sample rising (I2S)"); }
        } else {
            if (verbose) { syslog_print(" CLK: Assert rising, Sample falling"); }
        }
    }

    /*
     * Set .fsOptions
     *
     * Default setting is pulse, rising edge frame sync where the
     * frame sync signal asserts in the same cycle as the MSB of the
     * first data slot (TDM)
     */
    if (I2SGCFG & 0x80) {
        sportCfg->fsOptions |= SPORT_SIMPLE_FS_OPTION_INV;
        if (verbose) { syslog_print(" FS: Falling edge (I2S)"); }
    } else {
        if (verbose) { syslog_print(" FS: Rising edge"); }
    }
    if (I2SGCFG & 0x40) {
        sportCfg->fsOptions |= SPORT_SIMPLE_FS_OPTION_EARLY;
        if (verbose) { syslog_print(" FS: Early (I2S)"); }
    } else {
        if (verbose) { syslog_print(" FS: Not Early"); }
    }
    if (I2SGCFG & 0x20) {
        sportCfg->fsOptions |= SPORT_SIMPLE_FS_OPTION_50;
        if (verbose) { syslog_print(" FS: 50% (I2S)"); }
    } else {
        if (verbose) { syslog_print(" FS: Pulse"); }
    }

    ok = true;

abort:
    if (!ok) {
        memcpy(sportCfg, &backup, sizeof(*sportCfg));
    }
    return(ok);
}

static void ad2425_disconnect_master_clocks(void)
{
    // Set A2B BCLK LOW
    SRU(LOW, DAI0_PB07_I);
    // Set A2B FS LOW
    SRU(LOW, DAI0_PB08_I);
}

static void ad2425_connect_master_clocks(void)
{
    // Route BCLK to A2B BCLK
    SRU(DAI0_CRS_PB03_O, DAI0_PB07_I);
    
    // Route FS to A2B SYNC
    SRU(SPT1_AFS_O, DAI0_PB08_I);
}

static void ad2425_disconnect_slave_clocks(void)
{
    *pREG_PADS0_DAI0_IE &= ~(
        BITP_PADS0_DAI0_IE_PB07 | BITP_PADS0_DAI0_IE_PB08
    );
}

static void ad2425_connect_slave_clocks(void)
{
    *pREG_PADS0_DAI0_IE |= (
        BITP_PADS0_DAI0_IE_PB07 | BITP_PADS0_DAI0_IE_PB08
    );
}

/**
 *
 * A2B Master Mode Configuration:
 *    - MCLK/BCLK to SPORT1B/A2B Transceiver
 *    - SPORT1A FS to SPORT1B/A2B Transceiver
 *
 * NOTE: This function does not connect the A2B transceiver FS and BCLK.
 *       That happens in ad2425_connect_clocks().
 *
 */
void sru_config_a2b_master(void)
{
    // Set up pins for AD2425W (A2B)
    SRU(HIGH,  DAI0_PBEN07_I);        // pin for A2B BCLK is an output (to A2B bus)
    SRU(HIGH,  DAI0_PBEN08_I);        // pin for A2B FS is an output (to A2B bus)
    SRU(LOW,   DAI0_PBEN09_I);        // DTX0 is always an input (from A2B bus)
    SRU(LOW,   DAI0_PBEN10_I);        // DTX1 is always an input (from A2B bus)
    SRU(HIGH,  DAI0_PBEN11_I);        // DRX0 is always an output (to A2B bus)
    SRU(HIGH,  DAI0_PBEN12_I);        // DRX1 is always an output (to A2B bus)

    // BCLK/MCLK to SPORTA/B CLK */
    SRU(DAI0_CRS_PB03_O, SPT1_ACLK_I);     // route MCLK/BCLK to SPORT1A
    SRU(DAI0_CRS_PB03_O, SPT1_BCLK_I);     // route MCLK/BCLK to SPORT1B

    // SPORT1A FS to SPORT1B FS */
    SRU(SPT1_AFS_O, SPT1_BFS_I);      // route SPORT1A FS to SPORT1B

    // Connect A2B data signals to SPORT1
    SRU(SPT1_AD0_O, DAI0_PB11_I);     // route SPORT1A data TX primary to A2B DRX0
    SRU(SPT1_AD1_O, DAI0_PB12_I);     // route SPORT1A data TX secondary to A2B DRX0
    SRU(DAI0_PB09_O, SPT1_BD0_I);     // route A2B DTX0 to SPORT1B data RX primary
    SRU(DAI0_PB10_O, SPT1_BD1_I);     // route A2B DTX1 to SPORT1B data RX secondary
}

/**
 *
 * A2B Slave Mode Configuration:
 *    - A2B BCLK to SPORT1B
 *    - A2B FS to SPORT1B
 *
 */
void sru_config_a2b_slave(void)
{
    // Set up pins for AD2425W (A2B)
    SRU(LOW,   DAI0_PBEN07_I);        // pin for A2B BCLK is an input (from A2B bus)
    SRU(LOW,   DAI0_PBEN08_I);        // pin for A2B FS is an input (from A2B bus)
    SRU(LOW,   DAI0_PBEN09_I);        // DTX0 is always an input (from A2B bus)
    SRU(LOW,   DAI0_PBEN10_I);        // DTX1 is always an input (from A2B bus)
    SRU(HIGH,  DAI0_PBEN11_I);        // DRX0 is always an output (to A2B bus)
    SRU(HIGH,  DAI0_PBEN12_I);        // DRX1 is always an output (to A2B bus)

    // A2B BCLK SPORTA/B CLK */
    SRU(DAI0_PB07_O, SPT1_ACLK_I);     // route A2B BCLK to SPORT1A
    SRU(DAI0_PB07_O, SPT1_BCLK_I);     // route A2B BCLK to SPORT1B

    // A2B BCLK SPORTA/B CLK */
    SRU(DAI0_PB08_O, SPT1_AFS_I);     // route A2B FS to SPORT1A
    SRU(DAI0_PB08_O, SPT1_BFS_I);     // route A2B FS to SPORT1B

    // Connect A2B data signals to SPORT1
    SRU(SPT1_AD0_O, DAI0_PB11_I);     // route SPORT1A data TX primary to A2B DRX0
    SRU(SPT1_AD1_O, DAI0_PB12_I);     // route SPORT1A data TX secondary to A2B DRX0
    SRU(DAI0_PB09_O, SPT1_BD0_I);     // route A2B DTX0 to SPORT1B data RX primary
    SRU(DAI0_PB10_O, SPT1_BD1_I);     // route A2B DTX1 to SPORT1B data RX secondary
}

#define AD242X_CONTROL             0x12u
#define AD242X_CONTROL_SOFTRST     0x04u
#define AD242X_CONTROL_MSTR        0x80u

/* Soft reset a single transceiver */
bool ad2425_restart(APP_CONTEXT *context)
{
    TWI_SIMPLE_RESULT result;
    uint8_t wBuf[2];

    wBuf[0] = AD242X_CONTROL;
    wBuf[1] = AD242X_CONTROL_SOFTRST;
    if (context->a2bmode == A2B_BUS_MODE_MASTER) {
        wBuf[1] |= AD242X_CONTROL_MSTR;
    }

    result = twi_write(context->ad2425TwiHandle, AD2425W_SAM_I2C_ADDR,
        wBuf, sizeof(wBuf));

    return(result == TWI_SIMPLE_SUCCESS);
}

void ad2425_reset(APP_CONTEXT *context)
{
#if 0
    // Idle A2B SYNC pin for at least 1mS to reset transceiver */
    ad2425_disconnect_master_clocks();
    delay(2);
    ad2425_connect_master_clocks();
    delay(2);
    UNUSED(context);
#else
    ad2425_restart(context);
#endif
}

void sportCfg2ipcMsg(SPORT_SIMPLE_CONFIG *sportCfg, unsigned dataLen, IPC_MSG *msg)
{
    msg->audio.wordSize = sportCfg->wordSize / 8;
    msg->audio.numChannels = dataLen / (sportCfg->frames * msg->audio.wordSize);
}

bool ad2425_sport_init(APP_CONTEXT *context,
    bool master, CLOCK_DOMAIN clockDomain, uint8_t I2SGCFG, uint8_t I2SCFG,
    bool verbose)
{
    SPORT_SIMPLE_CONFIG sportCfg;
    SPORT_SIMPLE_RESULT sportResult;
    bool sportCfgOk;
    IPC_MSG *msg;
    bool rxtx;
    int i;

    /* Calculate the SPORT0A TX configuration */
    memset(&sportCfg, 0, sizeof(sportCfg));
    rxtx = false;
    sportCfgOk = ad2425_to_sport_cfg(master, rxtx, I2SGCFG, I2SCFG, &sportCfg, verbose);
    if (!sportCfgOk) {
        goto abort;
    }
    sportCfg.clkDir = SPORT_SIMPLE_CLK_DIR_SLAVE;
    if (master) {
        sportCfg.fsDir = SPORT_SIMPLE_FS_DIR_MASTER;
    } else {
        sportCfg.fsDir = SPORT_SIMPLE_FS_DIR_SLAVE;
    }
    sportCfg.frames = SYSTEM_BLOCK_SIZE;
    sportCfg.fs = SYSTEM_SAMPLE_RATE;
    sportCfg.dataBuffersCached = false;
    memcpy(sportCfg.dataBuffers, context->a2bAudioOut, sizeof(sportCfg.dataBuffers));
    context->a2bSportOutHandle = single_sport_init(
        SPORT1A, &sportCfg, a2bAudioOut,
        NULL, &context->a2bAudioOutLen, context, false, &sportResult
    );
    if (sportResult == SPORT_SIMPLE_SUCCESS) {
        for (i = 0; i < 2; i++) {
            msg = (IPC_MSG *)sae_getMsgBufferPayload(context->a2bMsgOut[i]);
            sportCfg2ipcMsg(&sportCfg, context->a2bAudioOutLen, msg);
        }
        clock_domain_set(context, clockDomain, CLOCK_DOMAIN_BITM_A2B_OUT);
        sportResult = sport_start(context->a2bSportOutHandle, true);
    } else {
        if (context->a2bSportOutHandle) {
            sport_close(&context->a2bSportOutHandle);
        }
        clock_domain_set(context, CLOCK_DOMAIN_MAX, CLOCK_DOMAIN_BITM_A2B_OUT);
    }

    /* Calculate the SPORT0B RX configuration */
    memset(&sportCfg, 0, sizeof(sportCfg));
    rxtx = true;
    sportCfgOk = ad2425_to_sport_cfg(master, rxtx, I2SGCFG, I2SCFG, &sportCfg, verbose);
    if (!sportCfgOk) {
        goto abort;
    }
    sportCfg.clkDir = SPORT_SIMPLE_CLK_DIR_SLAVE;
    sportCfg.fsDir = SPORT_SIMPLE_FS_DIR_SLAVE;
    sportCfg.frames = SYSTEM_BLOCK_SIZE;
    sportCfg.fs = SYSTEM_SAMPLE_RATE;
    sportCfg.dataBuffersCached = false;
    memcpy(sportCfg.dataBuffers, context->a2bAudioIn, sizeof(sportCfg.dataBuffers));
    context->a2bSportInHandle = single_sport_init(
        SPORT1B, &sportCfg, a2bAudioIn,
        NULL, &context->a2bAudioInLen, context, false, &sportResult
    );
    if (sportResult == SPORT_SIMPLE_SUCCESS) {
        for (i = 0; i < 2; i++) {
            msg = (IPC_MSG *)sae_getMsgBufferPayload(context->a2bMsgIn[i]);
            sportCfg2ipcMsg(&sportCfg, context->a2bAudioInLen, msg);
        }
        clock_domain_set(context, clockDomain, CLOCK_DOMAIN_BITM_A2B_IN);
        sportResult = sport_start(context->a2bSportInHandle, true);
    } else {
        if (context->a2bSportInHandle) {
            sport_close(&context->a2bSportInHandle);
        }
        clock_domain_set(context, CLOCK_DOMAIN_MAX, CLOCK_DOMAIN_BITM_A2B_IN);
    }

abort:
    return(sportCfgOk);
}

bool ad2425_sport_deinit(APP_CONTEXT *context)
{
    if (context->a2bSportOutHandle) {
        sport_close(&context->a2bSportOutHandle);
    }
    if (context->a2bSportInHandle) {
        sport_close(&context->a2bSportInHandle);
    }
    return(true);
}

bool ad2425_init_master(APP_CONTEXT *context)
{
    bool ok;

    sru_config_a2b_master();

    ok = ad2425_sport_init(context, true, CLOCK_DOMAIN_SYSTEM,
        SYSTEM_I2SGCFG, SYSTEM_I2SCFG, false);
    if (ok) {
        context->a2bmode = A2B_BUS_MODE_MASTER;
        context->a2bSlaveActive = false;
        ad2425_connect_master_clocks();
    }

    return(ok);
}

bool ad2425_init_slave(APP_CONTEXT *context)
{
    sru_config_a2b_slave();

    context->a2bmode = A2B_BUS_MODE_SLAVE;

    /*
     * Disconnect A2B from all clock domains.  IN and OUT will be re-attached
     * to the A2B domain during discovery when/if the TX and RX serializers
     * are enabled.
     */
    clock_domain_set(context, CLOCK_DOMAIN_MAX, CLOCK_DOMAIN_BITM_A2B_IN);
    clock_domain_set(context, CLOCK_DOMAIN_MAX, CLOCK_DOMAIN_BITM_A2B_OUT);

    return(true);
}

bool ad2425_set_mode(APP_CONTEXT *context, A2B_BUS_MODE mode)
{
    if (mode == context->a2bmode) {
        return(true);
    }

    ad2425_sport_deinit(context);

    if (mode == A2B_BUS_MODE_SLAVE) {
        ad2425_init_slave(context);
    } else {
        adau1962_sport_deinit(context);
        spdif_sport_deinit(context);
        disable_sport_mclk(context);
        adau1962_sport_init(context);
        spdif_sport_init(context);
        ad2425_init_master(context);
        enable_sport_mclk(context);
    }

    ad2425_restart(context);

    return(true);
}

bool ad2425_sport_start(APP_CONTEXT *context, uint8_t I2SGCFG, uint8_t I2SCFG)
{
    bool ok;
    ok = ad2425_sport_init(context, false, CLOCK_DOMAIN_A2B,
        I2SGCFG, I2SCFG, true);
    ad2425_connect_slave_clocks();
    return(ok);
}

bool ad2425_sport_stop(APP_CONTEXT *context)
{
    bool ok;
    ok = ad2425_sport_deinit(context);
    ad2425_disconnect_slave_clocks();
    return(ok);
}

void system_reset(APP_CONTEXT *context)
{
    w25q128fv_close(context->flashHandle);
    taskENTER_CRITICAL();
    *pREG_RCU0_CTL = BITM_RCU_CTL_SYSRST | BITM_RCU_CTL_RSTOUTASRT;
    while(1);
}

/***********************************************************************
 * SHARC Audio Engine (SAE) Audio IPC buffer configuration
 **********************************************************************/
/*
 * allocateIpcAudioMsg()
 *
 * Allocates an IPC_MSG_AUDIO Audio message and saves the data payload
 * pointer.
 *
 */
static SAE_MSG_BUFFER *allocateIpcAudioMsg(APP_CONTEXT *context,
    uint16_t size, uint8_t streamID, uint8_t numChannels, uint8_t wordSize,
    void **audioPtr)
{
    SAE_CONTEXT *saeContext = context->saeContext;
    SAE_MSG_BUFFER *msgBuffer;
    IPC_MSG *msg;
    uint16_t msgSize;

    /* Create an IPC message large enough to hold an IPC_MSG_AUDIO struct
     * with the data payload.
     */
    msgSize = sizeof(*msg) + size;

    /* Allocate a message buffer and initialize both the USB_IPC_SRC_MSG's
     * 'msgBuffer' and 'msg' members.
     */
    msgBuffer = sae_createMsgBuffer(saeContext, msgSize, (void **)&msg);
    assert(msgBuffer);

    /* Set fixed 'IPC_MSG_AUDIO' parameters */
    msg->type = IPC_TYPE_AUDIO;
    msg->audio.streamID = streamID;
    msg->audio.numChannels = numChannels;
    msg->audio.wordSize = wordSize;
    msg->audio.numFrames = size / (numChannels * wordSize);
    if (audioPtr) {
        *audioPtr = msg->audio.data;
    }

    return(msgBuffer);
}

/*
 * sae_buffer_init()
 *
 * Allocates and configures all of the SAE message/audio ping/pong
 * buffers between the ARM and SHARC0 and SHARC1.  Audio DMA buffers
 * are sent by reference from the ARM to the SHARCs
 *
 * These buffers can be referenced and used locally through the
 * context->xxxAudioIn/Out[] ping/pong buffers and sent/received via
 * the IPC message buffers context->xxxMsgIn/Out[].
 *
 */
void sae_buffer_init(APP_CONTEXT *context)
{
    int i;

    /* Allocate and initialize audio IPC ping/pong message buffers */
    for (i = 0; i < 2; i++) {

        /* ADC Audio In */
        context->codecAudioInLen =
            ADC_DMA_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
        context->codecMsgIn[i] = allocateIpcAudioMsg(
            context, context->codecAudioInLen,
            IPC_STREAMID_CODEC_IN, ADC_DMA_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
            &context->codecAudioIn[i]
        );
        assert(context->codecMsgIn[i]);
        memset(context->codecAudioIn[i], 0, context->codecAudioInLen);

        /* DAC Audio Out */
        context->codecAudioOutLen =
            DAC_DMA_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
        context->codecMsgOut[i] = allocateIpcAudioMsg(
            context, context->codecAudioOutLen,
            IPC_STREAMID_CODEC_OUT, DAC_DMA_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
            &context->codecAudioOut[i]
        );
        assert(context->codecMsgOut[i]);
        memset(context->codecAudioOut[i], 0, context->codecAudioOutLen);

        /* SPDIF Audio In */
        context->spdifAudioInLen =
            SPDIF_DMA_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
        context->spdifMsgIn[i] = allocateIpcAudioMsg(
            context, context->spdifAudioInLen,
            IPC_STREAMID_SPDIF_IN, SPDIF_DMA_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
            &context->spdifAudioIn[i]
        );
        memset(context->spdifAudioIn[i], 0, context->spdifAudioInLen);

        /* SPDIF Audio Out */
        context->spdifAudioOutLen =
            SPDIF_DMA_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
        context->spdifMsgOut[i] = allocateIpcAudioMsg(
            context, context->spdifAudioOutLen,
            IPC_STREAMID_SPDIF_OUT, SPDIF_DMA_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
            &context->spdifAudioOut[i]
        );
        memset(context->spdifAudioOut[i], 0, context->spdifAudioOutLen);

        /* A2B Audio In */
        context->a2bAudioInLen =
            A2B_DMA_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
        context->a2bMsgIn[i] = allocateIpcAudioMsg(
            context, context->a2bAudioInLen,
            IPC_STREAMID_A2B_IN, A2B_DMA_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
            &context->a2bAudioIn[i]
        );
        memset(context->a2bAudioIn[i], 0, context->a2bAudioInLen);

        /* A2B Audio Out */
        context->a2bAudioOutLen =
            A2B_DMA_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
        context->a2bMsgOut[i] = allocateIpcAudioMsg(
            context, context->a2bAudioOutLen,
            IPC_STREAMID_A2B_OUT, A2B_DMA_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
            &context->a2bAudioOut[i]
        );
        memset(context->a2bAudioOut[i], 0, context->a2bAudioOutLen);
        
        /* MIC Audio In */
        context->micAudioInLen =
            MIC_DMA_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
        context->micMsgIn[i] = allocateIpcAudioMsg(
            context, context->micAudioInLen,
            IPC_STREAMID_MIC_IN, MIC_DMA_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
            &context->micAudioIn[i]
        );
        assert(context->micMsgIn[i]);
        memset(context->micAudioIn[i], 0, context->micAudioInLen);

        /* Only need one buffer for the rest of these (no ping/pong) */
        if (i == 0) {
            /* USB Audio Rx */
            context->usbAudioRxLen =
                USB_DEFAULT_OUT_AUDIO_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
            context->usbMsgRx[i] = allocateIpcAudioMsg(
                context, context->usbAudioRxLen,
                IPC_STREAMID_USB_RX, USB_DEFAULT_OUT_AUDIO_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
                &context->usbAudioRx[i]
            );
            memset(context->usbAudioRx[i], 0, context->usbAudioRxLen);

            /* USB Audio Tx */
            context->usbAudioTxLen =
                USB_DEFAULT_IN_AUDIO_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
            context->usbMsgTx[i] = allocateIpcAudioMsg(
                context, context->usbAudioTxLen,
                IPC_STREAMID_USB_TX, USB_DEFAULT_IN_AUDIO_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
                &context->usbAudioTx[i]
            );
            memset(context->usbAudioTx[i], 0, context->usbAudioTxLen);

            /* WAVE Audio Src */
            context->wavAudioSrcLen =
                SYSTEM_MAX_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
            context->wavMsgSrc[i] = allocateIpcAudioMsg(
                context, context->wavAudioSrcLen,
                IPC_STREAM_ID_WAVE_SRC, SYSTEM_MAX_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
                &context->wavAudioSrc[i]
            );
            memset(context->wavAudioSrc[i], 0, context->wavAudioSrcLen);

            /* WAVE Audio Sink */
            context->wavAudioSinkLen =
                SYSTEM_MAX_CHANNELS * sizeof(SYSTEM_AUDIO_TYPE) * SYSTEM_BLOCK_SIZE;
            context->wavMsgSink[i] = allocateIpcAudioMsg(
                context, context->wavAudioSinkLen,
                IPC_STREAM_ID_WAVE_SINK, SYSTEM_MAX_CHANNELS, sizeof(SYSTEM_AUDIO_TYPE),
                &context->wavAudioSink[i]
            );
            memset(context->wavAudioSink[i], 0, context->wavAudioSinkLen);

        }

    }
}

/*
 * audio_routing_init()
 *
 * Allocates and configures the audio routing array message for use
 * with the SAE.
 *
 */
void audio_routing_init(APP_CONTEXT *context)
{
    SAE_CONTEXT *saeContext = context->saeContext;
    IPC_MSG *msg;
    unsigned msgSize;

    /* Create an IPC message large enough to hold the routing table */
    msgSize = sizeof(*msg) +
        (MAX_AUDIO_ROUTES - 1) * sizeof(ROUTE_INFO);

    /* Allocate a message buffer */
    context->routingMsgBuffer = sae_createMsgBuffer(
        saeContext, msgSize, (void **)&context->routingMsg
    );
    assert(context->routingMsgBuffer);

    /* Initialize the message and routing table */
    memset(context->routingMsg, 0 , msgSize);
    context->routingMsg->type = IPC_TYPE_AUDIO_ROUTING;
    context->routingMsg->routes.numRoutes = MAX_AUDIO_ROUTES;
}
