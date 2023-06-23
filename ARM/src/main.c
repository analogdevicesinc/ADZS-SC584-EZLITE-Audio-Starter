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

/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

/* CCES includes */
#include <services/gpio/adi_gpio.h>
#include <sys/adi_core.h>

/* Simple driver includes */
#include "spi_simple.h"
#include "twi_simple.h"
#include "sport_simple.h"
#include "uart_simple.h"
#include "uart_stdio.h"

/* Simple service includes */
#include "buffer_track.h"
#include "cpu_load.h"
#include "syslog.h"
#include "sae.h"
#include "fs_devman.h"
#include "fs_devio.h"
#include "fs_dev_spiffs.h"
#include "fs_dev_fatfs.h"

/* oss-services includes */
#include "shell.h"
#include "umm_malloc.h"
#include "spiffs.h"
#include "spiffs_fs.h"
#include "xmodem.h"

/* Project includes */
#include "context.h"
#include "init.h"
#include "clocks.h"
#include "util.h"
#include "ethernet_init.h"
#include "ipc.h"
#include "uac2.h"
#include "wav_audio.h"
#include "data_xfer.h"
#include "a2b_slave.h"
#include "clock_domain.h"
#include "rtp_audio.h"
#include "vban_audio.h"
#include "ss_init.h"

/* Application context */
APP_CONTEXT mainAppContext;

/* Select proper driver API for stdio operations */
#ifdef USB_CDC_STDIO
#define uart_open uart_cdc_open
#define uart_setProtocol uart_cdc_setProtocol
#else
#define uart_open uart_open
#define uart_setProtocol uart_setProtocol
#endif

/***********************************************************************
 * Shell console I/O functions
 **********************************************************************/
static void term_out( char data, void *usr )
{
    putc(data, stdout); fflush(stdout);
}

static int term_in( int mode, void *usr )
{
    int c;
    int timeout;

    if (mode == TERM_INPUT_DONT_WAIT) {
        timeout = STDIO_TIMEOUT_NONE;
    } else if (mode == TERM_INPUT_WAIT) {
        timeout = STDIO_TIMEOUT_INF;
    } else {
        timeout = mode / 1000;
    }

    uart_stdio_set_read_timeout(timeout);

    if ((c = getc(stdin)) == EOF) {
        return(-1);
    }

    return(c);
}

/***********************************************************************
 * CPU idle time / High precision timestamp functions
 **********************************************************************/
uint32_t getTimeStamp(void)
{
    uint32_t timeStamp;
    timeStamp = *pREG_CGU0_TSCOUNT0;
    return timeStamp;
}

void taskSwitchHook(void *taskHandle)
{
    cpuLoadtaskSwitchHook(taskHandle);
}

uint32_t elapsedTimeMs(uint32_t elapsed)
{
    return(((1000ULL) * (uint64_t)elapsed) / CGU_TS_CLK);
}

/***********************************************************************
 * Misc application utility functions (util.h)
 **********************************************************************/
void delay(unsigned ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

time_t util_time(time_t *tloc)
{
    APP_CONTEXT *context = &mainAppContext;
    time_t t;

    /*
     * Our time starts at zero so add 10 years + 2 days of milliseconds
     * to adjust UNIX epoch of 1970 to FAT epoch of 1980 to keep
     * FatFS happy.  See get_fattime() in diskio.c.
     *
     * https://www.timeanddate.com/date/timeduration.html
     *
     */
    vTaskSuspendAll();
    t = (context->now + (uint64_t)315532800000) / configTICK_RATE_HZ;
    xTaskResumeAll();

    if (tloc) {
        memcpy(tloc, &t, sizeof(*tloc));
    }

    return(t);
}

/***********************************************************************
 * Application IPC functions
 **********************************************************************/
SAE_RESULT ipcToCore(SAE_CONTEXT *saeContext, SAE_MSG_BUFFER *ipcBuffer, SAE_CORE_IDX core)
{
    SAE_RESULT result;

    result = sae_sendMsgBuffer(saeContext, ipcBuffer, core, true);
    if (result != SAE_RESULT_OK) {
        sae_unRefMsgBuffer(saeContext, ipcBuffer);
    }

    return(result);
}

SAE_RESULT quickIpcToCore(APP_CONTEXT *context, enum IPC_TYPE type, SAE_CORE_IDX core)
{
    SAE_CONTEXT *saeContext = context->saeContext;
    SAE_MSG_BUFFER *ipcBuffer;
    SAE_RESULT result;
    IPC_MSG *msg;

    ipcBuffer = sae_createMsgBuffer(saeContext, sizeof(*msg), (void **)&msg);
    msg->type = type;

    result = ipcToCore(saeContext, ipcBuffer, core);

    return(result);
}

static void ipcMsgHandler(SAE_CONTEXT *saeContext, SAE_MSG_BUFFER *buffer,
    void *payload, void *usrPtr)
{
    APP_CONTEXT *context = (APP_CONTEXT *)usrPtr;
    IPC_MSG *msg = (IPC_MSG *)payload;
    IPC_MSG_AUDIO *audio;
    IPC_MSG_CYCLES *cycles;
    SAE_RESULT result;
    uint32_t max, i;

    UNUSED(context);

    /* Process the message */
    switch (msg->type) {
        case IPC_TYPE_PING:
            /* Do nothing */
            break;
        case IPC_TYPE_SHARC0_READY:
            context->sharc0Ready = true;
            break;
        case IPC_TYPE_AUDIO:
            audio = (IPC_MSG_AUDIO *)&msg->audio;
            break;
        case IPC_TYPE_CYCLES:
            cycles = (IPC_MSG_CYCLES *)&msg->cycles;
            max = CLOCK_DOMAIN_MAX < IPC_CYCLE_DOMAIN_MAX ?
                CLOCK_DOMAIN_MAX : IPC_CYCLE_DOMAIN_MAX;
            for (i = 0; i < max; i++) {
                if (cycles->core == IPC_CORE_SHARC0) {
                        context->sharc0Cycles[i] = cycles->cycles[i];
                } else if (cycles->core == IPC_CORE_SHARC1) {
                        context->sharc1Cycles[i] = cycles->cycles[i];
                }
            }
            break;
        default:
            break;
    }

    /* Done with the message so decrement the ref count */
    result = sae_unRefMsgBuffer(saeContext, buffer);
}

/***********************************************************************
 * Tasks
 **********************************************************************/

/* Background housekeeping task */
static portTASK_FUNCTION( houseKeepingTask, pvParameters )
{
    APP_CONTEXT *context = (APP_CONTEXT *)pvParameters;
    SAE_CONTEXT *saeContext = context->saeContext;
    SAE_MSG_BUFFER *msgBuffer;
    TickType_t flashRate, lastFlashTime, clk, lastClk;
    bool calcLoad;
    IPC_MSG *msg;

    /* Configure the LED to flash at a 1Hz rate */
    flashRate = pdMS_TO_TICKS(500);
    lastFlashTime = xTaskGetTickCount();
    lastClk = xTaskGetTickCount();

    /* Calculate the system load every other cycle */
    calcLoad = false;

    /* Spin forever doing houseKeeping tasks */
    while (1) {

        /* Calculate the system load */
        if (calcLoad) {
            cpuLoadCalculateLoad(NULL);
            calcLoad = false;
        } else {
            calcLoad = true;
        }

        /* Toggle the LED */
        adi_gpio_Toggle(ADI_GPIO_PORT_E, ADI_GPIO_PIN_1);

        /* Ping both SHARCs with the same message */
        msgBuffer = sae_createMsgBuffer(saeContext, sizeof(*msg), (void **)&msg);
        if (msgBuffer) {
            msg->type = IPC_TYPE_PING;
            sae_refMsgBuffer(saeContext, msgBuffer);
            ipcToCore(saeContext, msgBuffer, IPC_CORE_SHARC0);
            ipcToCore(saeContext, msgBuffer, IPC_CORE_SHARC1);
        }

        /* Get cycles from both SHARCs */
        msgBuffer = sae_createMsgBuffer(saeContext, sizeof(*msg), (void **)&msg);
        if (msgBuffer) {
            msg->type = IPC_TYPE_CYCLES;
            sae_refMsgBuffer(saeContext, msgBuffer);
            ipcToCore(saeContext, msgBuffer, IPC_CORE_SHARC0);
            ipcToCore(saeContext, msgBuffer, IPC_CORE_SHARC1);
        }

        clk = xTaskGetTickCount();
        context->now += (uint64_t)(clk - lastClk);
        lastClk = clk;

        /* Sleep for a while */
        vTaskDelayUntil( &lastFlashTime, flashRate );

    }
}

/* Background A2B discovery task */
static portTASK_FUNCTION( a2bDiscoveryTask, pvParameters )
{
    APP_CONTEXT *context = (APP_CONTEXT *)pvParameters;
    bool doDiscover;

    while (1) {
        /* Sleep */
        delay(1000);
        /* Do A2B discovery if required */
        doDiscover = (context->discoverCmdStatus == false);
        if (doDiscover) {
            shell_exec(NULL, "discover revel-bo.xml");
        }
    }
}

/*pushButton task */
static portTASK_FUNCTION( pushButtonTask, pvParameters )
{
    APP_CONTEXT *context = (APP_CONTEXT *)pvParameters;
    TickType_t flashRate, lastFlashTime, clk, lastClk;
    uint32_t inputPort;
    uint32_t num;
    int rn;
    bool exists = true;
    bool wavOn = false;

    FILE *f = NULL;

    char fname[32];


    /* TODO:  some cleanup is needed here, not sure of the programming
       model of the TRNG, so just using the first number as a seed */

    /* enable true randome number generator */
    *pREG_TRNG0_CTL |= 0x1<<10;
    delay(250);  /* need to wait for TRNG */

    rn = *pREG_TRNG0_OUTPUT0;
    srand(rn);
    context->wavFileIndex = 0;

    /* make sure the generated file name doesn't already exist */
    while (exists) {
        num = rand() & 0xffffff;
        sprintf(fname, "rec%06lx_", num);
        context->wavRecordFile = fname;	/* base file name */
        sprintf(fname, "rec%06lx_%03d.wav" ,num , context->wavFileIndex);
        f = fopen(fname, "r");
        syslog_printf("filename: %s\n",fname);
        if (f == NULL) {
            exists = false;
        } else {
            fclose(f);
        }
    }

    /* Configure the LED to flash at a 1Hz rate */
    flashRate = pdMS_TO_TICKS(100);
    lastFlashTime = xTaskGetTickCount();
    lastClk = xTaskGetTickCount();

    while (1) {
        /* light LED 2 if PB2 pressed */
        adi_gpio_GetData(PUSHBUTTON_PORT, &inputPort);
        if (( inputPort & PB2 ) && wavOn) {
            wavOn = false;
#define PUSHBUTTON_CMD
#if defined(PUSHBUTTON_CMD)
            shell_exec(NULL, "run pushbtn2.cmd");
            adi_gpio_Set(LED_PORT, LED2 );
#else
            shell_exec(NULL, "wav sink off");
            adi_gpio_Clear(LED_PORT, LED3 );
#endif
        } else if (( inputPort & PB1) & !wavOn) {
            wavOn = true;
#if defined(PUSHBUTTON_CMD)
            shell_exec(NULL, "run pushbtn1.cmd");
            adi_gpio_Set(LED_PORT, LED2 );
#else
            char cmd[40];
            cmd[0] = '\0';
            strcat(cmd, "wav sink on "); strcat(cmd, fname);
            strcat(cmd, " 12 16");
            syslog_printf("wav record: %s\n",cmd);
            shell_exec(NULL, cmd);
            context->wavFileIndex++;
            sprintf(fname, "rec%06lx_%03d.wav" ,num , context->wavFileIndex);
            adi_gpio_Set(LED_PORT, LED3 );
#endif
        } else {
#if defined(PUSHBUTTON_CMD)
            adi_gpio_Clear(LED_PORT, LED2 );
#endif
        }

        clk = xTaskGetTickCount();
        context->now += (uint64_t)(clk - lastClk);
        lastClk = clk;

        /* Sleep for a while */
        vTaskDelayUntil( &lastFlashTime, flashRate );
    }
}

static void setAppDefaults(APP_CFG *cfg)
{
    cfg->usbOutChannels = USB_DEFAULT_OUT_AUDIO_CHANNELS;
    cfg->usbInChannels = USB_DEFAULT_IN_AUDIO_CHANNELS;
    cfg->usbWordSizeBits = USB_DEFAULT_WORD_SIZE_BITS;
    cfg->usbRateFeedbackHack = false;
    cfg->ip_addr = DEFAULT_IP_ADDR;
    cfg->gateway_addr = DEFAULT_GW_ADDR;
    cfg->netmask = DEFAULT_NETMASK;
    cfg->static_ip = DEFAULT_STATIC_IP;
}

static void execShellCmdFile(SHELL_CONTEXT *context)
{
    FILE *f = NULL;
    char *name = NULL;
    char cmd[32];

    name = "sf:shell.cmd";
    f = fopen(name, "r");
    
    if (f) {
        fclose(f);
        cmd[0] = '\0';
        strcat(cmd, "run "); strcat(cmd, name);
        shell_exec(context, cmd);
    }
}

/* System startup task -> background shell task */
static portTASK_FUNCTION( startupTask, pvParameters )
{
    APP_CONTEXT *context = (APP_CONTEXT *)pvParameters;
    SPI_SIMPLE_RESULT spiResult;
    TWI_SIMPLE_RESULT twiResult;
    SPORT_SIMPLE_RESULT sportResult;
    FS_DEVMAN_DEVICE *device;
    FS_DEVMAN_RESULT fsdResult;
    s32_t spiffsResult;

    /* Initialize the CPU load module. */
    cpuLoadInit(getTimeStamp, CGU_TS_CLK);

    /* Initialize the simple SPI driver */
    spiResult = spi_init();

    /* Initialize the simple TWI driver */
    twiResult = twi_init();

    /* Initialize the simple SPORT driver */
    sportResult = sport_init();

    /* Intialize the filesystem device manager */
    fs_devman_init();

    /* Intialize the filesystem device I/O layer */
    fs_devio_init();

    /* Open up a global device handle for TWI0 @ 400KHz */
    twiResult = twi_open(TWI0, &context->twi0Handle);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        syslog_print("Could not open TWI0 device handle!");
        return;
    }
    twi_setSpeed(context->twi0Handle, TWI_SIMPLE_SPEED_400);
    
    /* Open up a global device handle for TWI2 @ 400KHz */
    twiResult = twi_open(TWI2, &context->twi2Handle);
    if (twiResult != TWI_SIMPLE_SUCCESS) {
        syslog_print("Could not open TWI2 device handle!");
        return;
    }
    twi_setSpeed(context->twi2Handle, TWI_SIMPLE_SPEED_400);

    /* Set adau1962 (on-board DAC)
     * TWI handles to TWI0 - ad2425 (A2B) uses TWI2
     */
    context->ad2425TwiHandle = context->twi2Handle;
    context->adau1962TwiHandle = context->twi0Handle;
    context->softSwitchHandle = context->twi0Handle;
    context->adau1977TwiHandle = context->twi0Handle;
    
    /* Initialize the soft switches */
    ss_init(context);

    /* Init the SHARC Audio Engine.  This core is configured to be the
     * IPC master so this function must run to completion before any
     * other core calls sae_initialize().
     */
    sae_initialize(&context->saeContext, SAE_CORE_IDX_0, true);

    /* Register an IPC message callback */
    sae_registerMsgReceivedCallback(context->saeContext,
        ipcMsgHandler, context);

    /* Start the SHARC cores after the IPC is ready to go */
    adi_core_enable(ADI_CORE_SHARC0);
    adi_core_enable(ADI_CORE_SHARC1);

    /* Initialize the flash */
    flash_init(context);

    /* Initialize the SPIFFS filesystem */
    context->spiffsHandle = umm_calloc(1, sizeof(*context->spiffsHandle));
    spiffsResult = spiffs_mount(context->spiffsHandle, context->flashHandle);
    if (spiffsResult == SPIFFS_OK) {
        device = fs_dev_spiffs_device();
        fsdResult = fs_devman_register(SPIFFS_VOL_NAME, device, context->spiffsHandle);
    } else {
        syslog_print("SPIFFS mount error, reformat via command line\n");
    }

    /* Load configuration */
    setAppDefaults(&context->cfg);

    /* Initialize the IPC audio buffers in shared L2 SAE memory */
    sae_buffer_init(context);

    /* Initialize the IPC audio routing message in shared L2 SAE memory */
    audio_routing_init(context);

    /* Initialize the wave audio module */
    wav_audio_init(context);

    /* Initialize the RTP audio module */
    rtp_audio_init(context);

    /* Initialize the VBAN audio module */
    vban_audio_init(context);

    /* Initialize the data file stream module */
    data_file_init(context);

    /* Tell SHARC0 where to find the routing table.  Add a reference to
     * so it doesn't get destroyed upon receipt.
     */
    sae_refMsgBuffer(context->saeContext, context->routingMsgBuffer);
    ipcToCore(context->saeContext, context->routingMsgBuffer, IPC_CORE_SHARC0);

    /* Disable main MCLK/BCLK */
    disable_sport_mclk(context);
    
    /* Initialize main MCLK/BCLK */
    mclk_init(context);
    
    /* Initialize the ADAU1962 DAC */
    adau1962_init(context);
    
    /* Initialize the ADAU1977 ADC */
    adau1977_init(context);
    
    /* Initialize the ADAU1979 ADC */
    adau1979_init(context);
    
    /* Initialize the SPDIF I/O */
    spdif_init(context);

    /* Initialize the AD2425 in master mode */
    ad2425_init_master(context);
    ad2425_restart(context);

    /* Initialize the A2B, WAV, and UAC2 audio clock domains */
    clock_domain_init(context);

    /* Enable all SPORT clocks for a synchronous start */
    enable_sport_mclk(context);

    /* Configure the Ethernet event group */
    context->ethernetEvents = xEventGroupCreate();

    /* Initialize the Ethernet related HW */
    emac0_phy_init(context);

    /* Initialize lwIP and the Ethernet interface */
    ethernet_init(context);

    /* Get the idle task handle */
    context->idleTaskHandle = xTaskGetIdleTaskHandle();

    /* Start the housekeeping tasks */
    xTaskCreate( houseKeepingTask, "HouseKeepingTask", GENERIC_TASK_STACK_SIZE,
        context, HOUSEKEEPING_PRIORITY, &context->houseKeepingTaskHandle );
    xTaskCreate( a2bSlaveTask, "A2BSlaveTask", GENERIC_TASK_STACK_SIZE,
        context, HOUSEKEEPING_PRIORITY, &context->a2bSlaveTaskHandle );
#if 0
    xTaskCreate( pushButtonTask, "PushbuttonTask", GENERIC_TASK_STACK_SIZE,
        context, HOUSEKEEPING_PRIORITY, &context->pushButtonTaskHandle );
#endif
    /* Start the UAC20 task */
    xTaskCreate( uac2Task, "UAC2Task", UAC20_TASK_STACK_SIZE,
        context, UAC20_TASK_PRIORITY, &context->uac2TaskHandle );

    /* Lower the startup task priority for the shell */
    vTaskPrioritySet( NULL, STARTUP_TASK_LOW_PRIORITY);

    /* Initialize the shell */
    shell_init(&context->shell, term_out, term_in, SHELL_MODE_BLOCKING, NULL);

#ifdef USB_CDC_STDIO
    /* Delay a little bit for USB enumeration to complete to see
     * the entire shell banner.
     */
    delay(1000);
#endif

    /* Execute shell initialization command file */
    execShellCmdFile(&context->shell);

    /* Drop into the shell */
    while (1) {
        shell_start(&context->shell);
    }
}

int main(int argc, char *argv[])
{
    APP_CONTEXT *context = &mainAppContext;
    UART_SIMPLE_RESULT uartResult;

    /* Initialize system clocks */
    system_clk_init();

    /* Enable the CGU timestamp */
    cgu_ts_init();

    /* Initialize the application context */
    memset(context, 0, sizeof(*context));

    /* Initialize the GIC */
    gic_init();

    /* Initialize GPIO */
    gpio_init();

    /* Init the system heaps */
    heap_init();

    /* Init the system logger */
    syslog_init();

    /* Initialize the simple UART driver */
    uartResult = uart_init();

    /* Initialize the simple CDC UART driver */
    uartResult = uart_cdc_init();

    /* Open UART0 as the console device (115200,N,8,1) */
    uartResult = uart_open(UART0, &context->stdioHandle);
    uart_setProtocol(context->stdioHandle,
        UART_SIMPLE_BAUD_115200, UART_SIMPLE_8BIT,
        UART_SIMPLE_PARITY_DISABLE, UART_SIMPLE_STOP_BITS1
    );

    /* Initialize the UART stdio driver with the console device */
    uart_stdio_init(context->stdioHandle);

    /* Init the rest of the system and launch the remaining tasks */
    xTaskCreate( startupTask, "StartupTask", STARTUP_TASK_STACK_SIZE,
        context, STARTUP_TASK_HIGH_PRIORITY, &context->startupTaskHandle );

    /* Start the scheduler. */
    vTaskStartScheduler();

    return(0);
}

/*-----------------------------------------------------------
 * FreeRTOS idle hook
 *-----------------------------------------------------------*/
void vApplicationIdleHook( void )
{
}

/*-----------------------------------------------------------
 * FreeRTOS critical error and debugging hooks
 *-----------------------------------------------------------*/
void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
    ( void ) pcFile;
    ( void ) ulLine;

    /* Disable interrupts so the tick interrupt stops executing, then sit in a loop
    so execution does not move past the line that failed the assertion. */
    taskDISABLE_INTERRUPTS();
    adi_gpio_Set(ADI_GPIO_PORT_D, ADI_GPIO_PIN_1);
    while (1);
}

/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
    ( void ) pcTaskName;
    ( void ) pxTask;

    /* Run time stack overflow checking is performed if
    configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
    function is called if a stack overflow is detected. */
    taskDISABLE_INTERRUPTS();
    adi_gpio_Set(ADI_GPIO_PORT_D, ADI_GPIO_PIN_1);
    while (1);
}

/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
    /* Run time allocation failure checking is performed if
    configUSE_MALLOC_FAILED_HOOK is defined.  This hook
    function is called if an allocation failure is detected. */
    taskDISABLE_INTERRUPTS();
    adi_gpio_Set(ADI_GPIO_PORT_D, ADI_GPIO_PIN_1);
    while (1);
}

/*-----------------------------------------------------------*/
