#ifndef LOG_TYPES_H
#define LOG_TYPES_H
/* ---------------*/
#include <stdint.h>
#include "EventId.h"
/*
    定数定義
*/
constexpr uint8_t LOG_PAYLOAD_MAX = 64;
/*
    ログレベル
*/
enum class LogLevel : uint8_t {
    INFO = 0,
    WARN,
    ERROR
};
/*
    内部イベント構造
*/
#pragma pack(push, 1)
struct LogEvent{
	uint32_t seq;				// シーケンス番号
    EventId event_id;			// イベントID
    LogLevel level;				// ログレベル
    uint8_t length;			// data[]の有効バイト数
    uint32_t timestamp_us;		// time_us_32()の値
    uint8_t payload[LOG_PAYLOAD_MAX];	// 固定長ペイロード
};
#pragma pack(pop)
/*
    文字列変換関数(ログ表示用)
*/
const char * toString(LogLevel level);
const char * toString(EventId event_id);

#endif //LOG_TYPES_H