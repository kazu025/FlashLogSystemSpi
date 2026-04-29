#ifndef LOG_PROTOCOL_H
#define LOG_PROTOCOL_H
/* ----------------------------------------------- */
#include <stdint.h>
#include <stddef.h>
#include "LogTypes.h"

constexpr uint8_t LOG_FRAME_MAGIC[2] = {0xA5, 0x5A};
#pragma pack(push, 1)
struct LogFrameHeader {
    uint32_t seq;
    uint16_t event_id;
    uint8_t level;
    uint8_t length;
    uint32_t timestamp_us;
};
#pragma pack(pop)

constexpr uint16_t LOG_FRAME_MAGIC_SIZE = 2;
constexpr uint16_t LOG_FRAME_HEADER_SIZE = sizeof(LogFrameHeader);
constexpr uint16_t LOG_FRAME_MAX_PAYLOAD = LOG_PAYLOAD_MAX;
constexpr uint16_t LOG_FRAME_CRC_SIZE = 4;
// Frame layout:
// [magic(2)][header(12)][payload(length bytes)][crc32(4)]
// CRC32 is calculated over magic + header + payload.
constexpr uint16_t LOG_FRAME_MAX_SIZE = 
    LOG_FRAME_MAGIC_SIZE + LOG_FRAME_HEADER_SIZE + LOG_FRAME_MAX_PAYLOAD + LOG_FRAME_CRC_SIZE;
/* ----------------------------------------------- */
#endif // LOG_PROTOCOL_H