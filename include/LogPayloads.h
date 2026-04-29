#ifndef LOG_PAYLOADS_H
#define LOG_PAYLOADS_H
/* --------------------------------------------*/
#include <stdint.h>

#pragma pack(push, 1)
struct LoggerStatsPayloadV1 {
    uint8_t version;
    uint8_t reserved1;
    uint16_t reserved2;
    uint32_t enqueue_ok_count;
    uint32_t enqueue_drop_count;
    uint32_t uart_tx_count;
    uint32_t uart_tx_bytes;
    uint32_t high_water_mark;
};
#pragma pack(pop)
/* --------------------------------------------*/
#endif //LOG_PAYLOADS_H