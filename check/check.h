#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "UartDma.h"
#include "EventLogger.h"
#include "FlashDriver.h"
#include "FlashLogStorage.h"
#include "led25.h"
#include "DebugUtils.h"
#include <cstring>

bool test_flash_basic_rw(FlashDriver& flash);
bool test_flash_log_frame(FlashDriver& flash, EventLogger& logger);
bool test_flash_storage_append_frame(FlashDriver& flash, FlashLogStorage& storage, EventLogger& logger);
bool test_flash_storage_append_multi_frames(FlashDriver& flash, FlashLogStorage& storage, EventLogger& logger);
bool send_flash_logs_to_uart(FlashDriver& flash, UartDma& uart_dma, uint32_t addr, size_t len);
bool test_event_logger_flash_normal_path(FlashDriver& flash, FlashLogStorage& storage, EventLogger& logger, UartDma& uart_dma);
bool test_flash_storage_ring_write(FlashDriver& flash, FlashLogStorage& storage);
bool test_flash_storage_ring_sector_boundary(FlashDriver& flash, FlashLogStorage& storage);
bool test_flash_storage_write_address(FlashLogStorage& storage);
bool test_flash_storage_restore_write_address(FlashLogStorage& storage);
bool test_flash_storage_restore_with_log_frame(FlashLogStorage& storage, EventLogger& logger);
bool test_flash_storage_restore_with_log_frame(FlashLogStorage& storage, EventLogger& logger);
bool test_flash_storage_restore_with_multi_log_frame(FlashLogStorage& storage, EventLogger& logger);
bool test_flash_storage_restore_after_wrap(FlashLogStorage& storage, EventLogger& logger);
bool test_flash_storage_dump_oldest_first(FlashLogStorage& storage);
void test_pseudo_power_cut(FlashDriver& flash);
