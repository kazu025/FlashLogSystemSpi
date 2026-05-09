/*
log()/logf()
   ↓
Queue に LogEvent を入れる
   ↓
logger task が受信
   ↓
sendBinary(event)
   ↓
build_frame(...)
   ↓
uart_dma_.write(frame, frame_len)   // DMAでフレーム単位送信

*/

#include <stdio.h>
#include <string.h>
#include "EventLogger.h"
#include "LogPayloads.h"
#include "Crc32.h"

static_assert(sizeof(LoggerStatsPayloadV1) == 24, "LoggerStatsPayloadV1 size mismatch!");
/* --------------------------------------------------------
    コンストラクタ
   -------------------------------------------------------- */
EventLogger::EventLogger(UartDma& uart_dma, FlashLogStorage* flash): queue_(nullptr), seq_(0),
                    drop_count_(0), uart_dma_(uart_dma), flash_(flash), enqueue_ok_count_(0),
                    uart_tx_count_(0), uart_tx_bytes_(0), high_water_mark_(0){}
/* --------------------------------------------------------
    queue_length 個分のLogEvent を入れられるキューを作成
   -------------------------------------------------------- */
bool EventLogger::init(size_t queue_length){
    queue_ = xQueueCreate(queue_length, sizeof(LogEvent));
    return (queue_ != nullptr);
}

/* --------------------------------------------------------
    LogEventを作成
   -------------------------------------------------------- */
bool EventLogger::makeEvent(LogEvent& event, LogLevel level, EventId event_id, const void* data, uint16_t len){
    if(len > LOG_PAYLOAD_MAX){
        return false;
    }
    memset(&event, 0, sizeof(event));
    event.seq = nextSeq();
    event.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
    event.event_id = event_id;
    event.level = level;
    event.length = (uint8_t)len;
    if(data != nullptr && len > 0){
        memcpy(event.payload, data, len);
    }else{
        memset(event.payload, 0, sizeof(event.payload));
    }
    if(len < LOG_PAYLOAD_MAX){
        memset(&event.payload[len], 0, LOG_PAYLOAD_MAX - len);
    }
    return true;
}
bool EventLogger::makeEventFromISR(LogEvent& event, LogLevel level, EventId event_id, const void* data, uint16_t len){
    if(len > LOG_PAYLOAD_MAX){
        return false;
    }
    memset(&event, 0, sizeof(event));
    event.seq = nextSeqFromISR();
    event.timestamp_us = (uint32_t)to_us_since_boot(get_absolute_time());
    event.event_id = event_id;
    event.level = level;
    event.length = (uint8_t)len;
    if(data != nullptr && len > 0){
        memcpy(event.payload, data, len);
    }else{
        memset(event.payload, 0, sizeof(event.payload));
    }
    if(len < LOG_PAYLOAD_MAX){
        memset(&event.payload[len], 0, LOG_PAYLOAD_MAX- len);
    }
    return true;
}
/* --------------------------------------------------------
    drop_count_制御関数s
   -------------------------------------------------------- */
void EventLogger::incrementDropCount(){
    taskENTER_CRITICAL();
    drop_count_++;
    taskEXIT_CRITICAL();
}
void EventLogger::incrementDropCountFromISR(){
    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    drop_count_++;
    taskEXIT_CRITICAL_FROM_ISR(saved);
}

uint32_t EventLogger::getDropCount() const {
    taskENTER_CRITICAL();
    uint32_t v = drop_count_;
    taskEXIT_CRITICAL();
    return v;
}
/* --------------------------------------------------------
    送信バイト欠落カウント（バイト単位送信）
   -------------------------------------------------------- */
uint32_t EventLogger::getTxByteDropCount() const {
    return uart_dma_.getTxByteDropCount();
}
/* --------------------------------------------------------
    EventLogを作成しQueueにpushする関数群 
   -------------------------------------------------------- */
bool EventLogger::log(LogLevel level, EventId event_id, const void* data, uint16_t len){
    if(queue_ == nullptr){
        return false;
    }
    LogEvent event;
    if(!makeEvent(event, level, event_id, data, len)){
        incrementDropCount();
        return false;
    }
    BaseType_t ok = xQueueSend(queue_, &event, 0);
    if(ok != pdPASS){
        incrementDropCount();
        return false;
    }
    taskENTER_CRITICAL();
    enqueue_ok_count_++;
    UBaseType_t used = uxQueueMessagesWaiting(queue_);
    if(used > high_water_mark_) high_water_mark_ = used;
    taskEXIT_CRITICAL();
    return true;
}
bool EventLogger::logFromISR(LogLevel level, EventId event_id, const void* data, uint16_t len, BaseType_t* higher_woken) {
    if(queue_ == nullptr)    return false;
    LogEvent event;
    if(!makeEventFromISR(event, level, event_id, data, len)){
        incrementDropCountFromISR();
        return false;
    }
    BaseType_t ok = xQueueSendFromISR(queue_, &event, higher_woken);
    if(ok != pdPASS){
        incrementDropCountFromISR();
        return false;
    }
    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    enqueue_ok_count_++;
    taskEXIT_CRITICAL_FROM_ISR(saved);
    return true;    
}
bool EventLogger::logf(LogLevel level,  const char* fmt, ...){
    if(fmt == nullptr){
        incrementDropCount();
        return false;
    }
    char text[LOG_PAYLOAD_MAX];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);

    if (n < 0){
        incrementDropCount();
        return false;
    }

    uint8_t len = 0;
    if((size_t)n >= sizeof(text)){
        len = static_cast<uint8_t>(sizeof(text) - 1);
    }else{
        len = static_cast<uint8_t>(n);
    }
    return log(level, EventId::TEXT_LOG, text, len);
}
/* --------------------------------------------------------
    LogEvent取得
  --------------------------------------------------------- */
bool EventLogger::receive(LogEvent& out, TickType_t timeout){
    if(queue_ == nullptr) return false;
    return (xQueueReceive(queue_, &out, timeout) == pdTRUE);
}
/* --------------------------------------------------------
    eventを出力:printf()
    taskEntry() は「戻ってこない」 vTaskDelete() は実行されない
    → タスクは削除されない
  --------------------------------------------------------- */
void EventLogger::taskEntry(void* arg){
    EventLogger* self = static_cast<EventLogger*>(arg);
    if(self != nullptr){
        self->taskLoop();
    }
    printf("FATAl: Logger task died!!\n");
    taskDISABLE_INTERRUPTS(); // 割り込み前停止：すべての動作を停止します。
    while(true) tight_loop_contents();
//      vTaskDelete(nullptr); // 自タスクを削除
}
void EventLogger::taskLoop(void){
    LogEvent event;
    
    while(true){
        /* 無限ループ：Queueが空のときは receive() でブロックして止まる */
        if(receive(event, portMAX_DELAY)) {
            sendBinary(event);
       }
    }
}
/* --------------------------------------------------------
    連番関数群
  --------------------------------------------------------- */
uint32_t EventLogger::nextSeq(){
	taskENTER_CRITICAL();
	uint32_t  v = seq_++;
	taskEXIT_CRITICAL();
	return v;
}
uint32_t EventLogger::nextSeqFromISR(){
	UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
	uint32_t v = seq_++;
	taskEXIT_CRITICAL_FROM_ISR(saved);
	return v;
}
uint32_t EventLogger::getSequence() const {
    taskENTER_CRITICAL();
    uint32_t v = seq_;
    taskEXIT_CRITICAL();
    return v;
}
/* --------------------------------------------------------
    Frame送信処理
  --------------------------------------------------------- */
/*
    [magic 2B][seq 4B][event_id 2B][level 1B][length 1B][timestamp 4B][payload][CRC32 4B]
    CRC32 = [seq]から[payload]
    magic: 0xA5 0x5A
	seq: 4Byte
    event_id: little endian 2Byte
    level: 1Byte
    length: 1Byte
    timestamp_us: 4Byte
    payload
    crc32: little endian 4B
*/
bool EventLogger::sendFrame(const uint8_t* frame, uint16_t frame_len){
    if(frame == nullptr || frame_len ==0) return false;
    return uart_dma_.write_frame(frame, frame_len);
}
void EventLogger::sendBinary(const LogEvent& event){
    uint8_t frame[LOG_FRAME_MAX_SIZE];
    uint16_t frame_len = buildFrame(event, frame, sizeof(frame));
    if(frame_len == 0){
        incrementDropCount();
        return;
    }
    // UART送信
    if(!sendFrame(frame, frame_len)){
        incrementDropCount();
        return;
    }

    // Flash書き込み
    if(flash_ != nullptr){
        if(!flash_->append(frame, frame_len)){
            incrementDropCount();
        }
    }

    taskENTER_CRITICAL();
    uart_tx_count_++;
    uart_tx_bytes_ += frame_len;
    taskEXIT_CRITICAL();
}
/* --------------------------------------------------------
    frame構築
  --------------------------------------------------------- */
size_t EventLogger::buildFrame(const LogEvent& event, uint8_t* out_buf, size_t out_buf_size){
    if(out_buf == nullptr)    return 0;
    if(event.length > 0 && event.payload == nullptr) return 0;
    if(event.length > LOG_FRAME_MAX_PAYLOAD) return 0;
    const size_t frame_size = LOG_FRAME_MAGIC_SIZE + LOG_FRAME_HEADER_SIZE + event.length + LOG_FRAME_CRC_SIZE;
    if(out_buf_size < frame_size) return 0;

    size_t pos = 0;
    /* 1) Magic */
    memcpy(&out_buf[pos], LOG_FRAME_MAGIC, LOG_FRAME_MAGIC_SIZE);
    pos += LOG_FRAME_MAGIC_SIZE;
    /* 2) Header */
    LogFrameHeader header{};
    header.seq      = event.seq;
    header.event_id = static_cast<uint16_t>(event.event_id);
    header.level    = static_cast<uint8_t>(event.level);
    header.length   = event.length;
    header.timestamp_us = event.timestamp_us;
    memcpy(&out_buf[pos], &header, sizeof(header));
    pos += sizeof(header);
    /* 3) payload */
    if(event.length > 0){
        memcpy(&out_buf[pos], event.payload, event.length);
        pos += event.length;
    }
    /* 4) crc */
    uint32_t crc = Crc32::calculate(out_buf, pos);
    memcpy(&out_buf[pos], &crc, sizeof(crc));
    pos += sizeof(crc);
    if(pos != frame_size) return 0;
    return pos;
}

/* --------------------------------------------------------
   LoggerStatsPayload V1 送信処理
  --------------------------------------------------------- */
void EventLogger::sendStats(){
    LoggerStatsPayloadV1 stats{};

    uint32_t enqueue_ok_count_snapshot = 0;
    uint32_t drop_count_snapshot = 0;
    uint32_t uart_tx_count_snapshot = 0;
    uint32_t uart_tx_bytes_snapshot = 0;
    uint32_t high_water_mark_snapshot = 0; 

    taskENTER_CRITICAL();
    enqueue_ok_count_snapshot = enqueue_ok_count_;
    drop_count_snapshot = drop_count_;
    uart_tx_count_snapshot = uart_tx_count_;
    uart_tx_bytes_snapshot = uart_tx_bytes_;
    high_water_mark_snapshot = high_water_mark_; 
    taskEXIT_CRITICAL();

    stats.version = 1;
    stats.reserved1 = 0;
    stats.reserved2 = 0;
    stats.enqueue_ok_count = enqueue_ok_count_snapshot;
    stats.enqueue_drop_count = drop_count_snapshot;
    stats.uart_tx_count = uart_tx_count_snapshot;
    stats.uart_tx_bytes = uart_tx_bytes_snapshot;
    stats.high_water_mark = high_water_mark_snapshot;
    log(LogLevel::INFO, EventId::LOGGER_STATS,
        reinterpret_cast<const uint8_t*>(&stats),
        sizeof(stats));
}  

void EventLogger::setNextSeq(uint32_t seq){
    taskENTER_CRITICAL();
    seq_ = seq;
    taskEXIT_CRITICAL();
}