#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "FreeRTOS.h"
#include "UartDma.h"
#include "EventLogger.h"
#include "FlashDriver.h"
#include "FlashLogStorage.h"
#include "led25.h"
#include "DebugUtils.h"
#include <cstring>
#include "check.h"

static FlashDriver* g_flash_driver = nullptr;
static FlashLogStorage* g_log_storage = nullptr;
static UartDma* g_uart_dma = nullptr;
static EventLogger* g_logger = nullptr;
extern size_t g_flash_log_dump_len;

static constexpr uint LOGGER_TASK_STACK_WORDS = 512;
static constexpr UBaseType_t LOGGER_TASK_PRIORITY = 2;
static constexpr uint32_t LOG_START_ADDR = 0x00001000;
static constexpr uint32_t LOG_END_ADDR = 0x00003000;
static void app_task(void *arg);

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
    /* テスト段階では、起動時にログ領域を消す */
    printf("erase log area ... \r\n");
    if(!storage.eraseLogArea()){
        printf("!!! storage.eraseLogArea() failed\r\n");
        while(true) tight_loop_contents();
    }
    printf("erase done\r\n");
    /* === EventLooger === */
    static EventLogger logger(uart_dma, &storage);
    if(!logger.init(32)){
        printf("!!! logger.init() failed\n");
        while(true) tight_loop_contents();
    } 
    g_logger = &logger;

    /* === logger task === */
    BaseType_t ok = xTaskCreate(
        EventLogger::taskEntry, "logger", LOGGER_TASK_STACK_WORDS, &logger, LOGGER_TASK_PRIORITY, nullptr);
    if(ok != pdPASS){
        printf("!!! xTaskCreate(logger) failed\n");
        while(true) tight_loop_contents();
    }
    /* === 動作確認用タスク === */
//                  task, task名, stack(word数),タスクパラメータ.priority,
    ok = xTaskCreate(app_task, "app", 512, &logger, 1, nullptr);       // 生成タスクの参照法ハンドラ
    if(ok != pdPASS){
        printf("!!! xTaskCreate(app_task) failed\n");
        while(true) tight_loop_contents();
    }
    vTaskStartScheduler();
    /* 通常ここは来ない */
    printf("!!! Scheduler return\n");
    while(true) tight_loop_contents();
}

/* === タスク === */
static void app_task(void *arg){
    (void)arg;
//    EventLogger* logger = static_cast<EventLogger*>(arg);
    led_init();

    if(!test_flash_storage_restore_after_wrap(*g_log_storage, *g_logger)){
        printf("!!! test_flash_storage_restore_after_wrap failed\n");
    }
    g_log_storage->dumpFramesOldestFirst();

    if(!test_flash_storage_dump_oldest_first(*g_log_storage)){
        printf("!!! test_flash_storage_dump_oldest_first failed\n");
    }
    g_log_storage->dumpFramesOldestFirst();
    g_log_storage->readFramesOldestFirstTest();
    printf("send flash logs to Uart...\n");
    g_log_storage->sendFramesOldestFirst(*g_uart_dma);
    while(true){
        led_sw();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}