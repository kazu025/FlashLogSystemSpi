#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "FreeRTOS.h"
#include "UartDma.h"
#include "EventLogger.h"
#include "FlashDriver.h"
#include "FlashLogStorage.h"
#include "utility.h"
#include <cstring>
#include "check.h"
#include "AdcLoggerTask.h"
#include "CommandTask.h"


/* CMakeListsLists.txtで切り替える
#define RUN_NORMAL_APP            1
#define RUN_PSEUDO_POWER_CUT_TEST 0
*/
static FlashDriver* g_flash_driver = nullptr;
static FlashLogStorage* g_log_storage = nullptr;
static UartDma* g_uart_dma = nullptr;
static EventLogger* g_logger = nullptr;
extern size_t g_flash_log_dump_len;

static constexpr uint LOGGER_TASK_STACK_WORDS = 512;
static constexpr UBaseType_t LOGGER_TASK_PRIORITY = 2;
static constexpr uint32_t LOG_START_ADDR = 0x00001000;
static constexpr uint32_t LOG_END_ADDR = 0x00003000;
static AppContext app_ctx;
#if RUN_NORMAL_APP
int main(){
    stdio_init_all();
    sleep_ms(2000);
    
    printf("start log \r\n");
    /* === UART DMA === */
    static UartDma uart_dma(uart0, UartDma::UART_BAUDRATE_460800, UartDma::UART_TX_PIN, UartDma::UART_RX_BUF_SIZE); 
    if(!uart_dma.init()){
        printf("!!! UartDma.init() failed\n");
        while(true) tight_loop_contents();
    }
    g_uart_dma = &uart_dma;
    /* === SPI Flash === */
    static FlashDriver flash(spi0, FlashDriver::PIN_SPI_CS,FlashDriver::PIN_SPI_SCK, FlashDriver::PIN_SPI_MOSI, FlashDriver::PIN_SPI_MISO, FlashDriver::SPI_BAUDRATE_10M);
    if(!flash.init()){
        printf("!!! FlashDriver.init() failed\n");
        while(true) tight_loop_contents();
    }
    g_flash_driver = &flash;
    /* === Flashストレージ === */
    static FlashLogStorage storage = FlashLogStorage(flash, LOG_START_ADDR, LOG_END_ADDR);
    if(!storage.init()){
        printf("!!! storag.init() : failed\n");
        while(true) { tight_loop_contents(); }
    }
    g_log_storage = &storage;
    /* === EventLooger === */
    static EventLogger logger(uart_dma, &storage);
    if(!logger.init(32)){
        printf("!!! logger.init() failed\n");
        while(true) tight_loop_contents();
    }
    if(storage.getCount()>0){
        logger.setNextSeq(storage.getNewestSeq() + 1);
        printf("logger seq restored to %lu\r\n", static_cast<unsigned long>(storage.getNewestSeq()+1));
    }
    g_logger = &logger;

    /* === logger task === */
    BaseType_t ok = xTaskCreate(
        EventLogger::taskEntry, "logger", LOGGER_TASK_STACK_WORDS, &logger, LOGGER_TASK_PRIORITY, nullptr);
    if(ok != pdPASS){
        printf("!!! xTaskCreate(logger) failed\n");
        while(true) tight_loop_contents();
    }
    /* === ADCタスク === */
    ok = xTaskCreate(adc_task, "cmd", 512, &logger, 1, nullptr);
    if(ok != pdPASS){
        printf("!!! xTaskCreate(adc_task) failed\n");
        while(true) tight_loop_contents();
    }
    app_ctx.storage = &storage;
    app_ctx.uart = &uart_dma;
    app_ctx.logger = &logger;
    ok = xTaskCreate(command_task, "cmd", 512, &app_ctx, 3, nullptr);
    if(ok != pdPASS){
        printf("!!! xTaskCreate(command_task) failed\n");
        while(true) tight_loop_contents();
    }

    vTaskStartScheduler();
    /* 通常ここは来ない */
    printf("!!! Scheduler return\n");
    while(true) tight_loop_contents();
}

#elif RUN_PSEUDO_POWER_CUT_TEST
int main(){
    stdio_init_all();
    sleep_ms(5000);
    printf("start pseudo power cut test\n");
    static FlashDriver flash(spi0, FlashDriver::PIN_SPI_CS,FlashDriver::PIN_SPI_SCK, FlashDriver::PIN_SPI_MOSI, FlashDriver::PIN_SPI_MISO, FlashDriver::SPI_BAUDRATE_10M);
    flash.init();
    uint8_t id[3];
    flash.readJedecId(id);
    printf("JEDEC ID: %02X %02X %02X\n", id[0], id[1], id[2]);
    test_pseudo_power_cut(flash);
    while(true) sleep_ms(1000);
}
#else
#error "Select one main module"
#endif