#include "LogTypes.h"

const char * toString(LogLevel level){
    switch(level){
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARN:    return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

static inline const char* to_string(EventId id) {
    switch(id) {
        case EventId::SYSTEM_START: 		return "SYSTEM_START";
        case EventId::SYSTEM_READY: 		return "SYSTEM_READY";
		case EventId::SYSTEM_SHUTDOWN: 		return "SYSTEM_SHUTDOWN";
		case EventId::WATCHDOG_REBOOT: 		return "WATCHDOG_REBOOT";
        case EventId::HEARTBEAT:    		return "HEARTBEAT";
		case EventId::COUNTER_MARK: 		return "COUNTER_MARK";
		case EventId::CHECKPOINT: 			return "CHECKPOINT";
		case EventId::UART_RX: 				return "UART_RX";
		case EventId::UART_TX: 				return "UART_TX";
		case EventId::DMA_DONE: 			return "DMA_DONE";
        case EventId::TEXT_LOG:     		return "TEXT_LOG";
		case EventId::DEBUG_LOG: 			return "DEBUG_LOG";
		case EventId::LOGGER_STATS:  		return "LOGGER_STATS";
        case EventId::QUEUE_FULL:   		return "QUEUE_FULL";
		case EventId::CRC_ERROR: 			return "CRC_ERROR";
		case EventId::UART_ERROR: 			return "UART_ERROR";
		case EventId::DMA_ERROR: 			return "DMA_ERROR";
		case EventId::SENSOR_ERROR: 		return "SENSOR_ERROR";
		case EventId::UNKNOWN_ERROR: 		return "UNKNOWN_ERROR";
		case EventId::FATAL_ERROR: 			return "FATAL_ERROR";
        default: 							return "UNKNOWN";
    }
}