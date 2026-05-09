#ifndef EVENT_LOGGER_H
#define EVENT_LOGGER_H
/* --------------------- */
#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "LogTypes.h"
#include "UartDma.h"
#include "LogProtocol.h"
#include "hardware/uart.h"
#include "FlashLogStorage.h"

class EventLogger{
public:
    explicit EventLogger(UartDma& uart_dma, FlashLogStorage* flash = nullptr);
    bool init(size_t queue_length);

    /* 高速イベント、バイナリデータ */
    bool log(LogLevel level, EventId event_id, const void* payload, uint16_t length);
    bool logFromISR(LogLevel level, EventId event_id, const void* payload, uint16_t len, BaseType_t* higher_woken);
    /* 状態遷移、デバッグメッセージ */
    bool logf(LogLevel level, const char * fmt, ...);
    bool receive(LogEvent& out, TickType_t timeout);
    uint32_t getSequence() const;
    uint32_t getDropCount() const;
    uint32_t getTxByteDropCount() const;
    void sendStats();

    static void taskEntry(void* arg);
    void taskLoop(void);
    size_t buildFrame(const LogEvent& event, uint8_t* out_buf, size_t out_buf_size);
    /* seq連続性 */ 
    void setNextSeq(uint32_t seq);
private:
    bool makeEvent(LogEvent& event, LogLevel level, EventId event_id, const void* data, uint16_t len);
    bool makeEventFromISR(LogEvent& event, LogLevel level, EventId event_id, const void* data, uint16_t len);
    void incrementDropCount();
    void incrementDropCountFromISR();
    
	/* 連番取得関数 */
	uint32_t nextSeq();
	uint32_t nextSeqFromISR();
    /* ログ送信関数 */
    void sendBinary(const LogEvent& event);
    bool sendFrame(const uint8_t* frame, uint16_t frame_len);
private:
    QueueHandle_t queue_;
	volatile uint32_t seq_;
    volatile uint32_t drop_count_;
    UartDma &uart_dma_;
    FlashLogStorage* flash_;
    //統計量
    volatile uint32_t enqueue_ok_count_;
    volatile uint32_t uart_tx_count_;
    volatile uint32_t uart_tx_bytes_;
    volatile uint32_t high_water_mark_;
};
/* --------------------- */
#endif// EVENT_LOGGER_H