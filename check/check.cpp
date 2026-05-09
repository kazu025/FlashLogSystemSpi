/*
    チェック関数群
*/
#include "check.h"
#include <stdio.h>
#include <string.h>
#include "FlashDriver.h"

static constexpr uint32_t TEST_ADDR = FlashLogStorage::LOG_START_ADDR;
static bool make_text_frame(EventLogger& logger, uint32_t seq, const char* msg, uint8_t* frame,size_t frame_buf_size, size_t* frame_len);
static void dump_flash_binary_for_viewer(FlashDriver& flash, uint32_t addr, size_t len);
static bool rawProgramForTest(FlashDriver& flash, uint32_t addr, const uint8_t* data, size_t len);
static size_t buildTextFrameForTest(EventLogger &logger, int32_t seq, const char* text, uint8_t* out_buf, size_t out_buf_size);
static void printStorageStatus(const char* title, const FlashLogStorage& storage);

size_t g_flash_log_dump_len = 0;

/* テストフレーム作成 */
static bool make_text_frame(EventLogger& logger, uint32_t seq, const char* msg, uint8_t* frame,
                            size_t frame_buf_size, size_t* frame_len){
    if(msg == nullptr || frame == nullptr || frame_len == nullptr) {
            return false;
    }
    LogEvent ev{};
    ev.seq = seq;
    ev.timestamp_us = time_us_32();
    ev.level = LogLevel::INFO;
    ev.event_id = EventId::TEXT_LOG;
    ev.length = static_cast<uint8_t>(strlen(msg));

    if(ev.length > sizeof(ev.payload)){
        printf("!!! %s: message too long", __func__);
        return false;
    }
    memcpy(ev.payload, msg, ev.length);
    const size_t len = logger.buildFrame(ev, frame, frame_buf_size);
    if(len == 0){
        printf("!!! %s: buildFrame failed len=%u", __func__, static_cast<unsigned int>(len));
        return false;
    }
    *frame_len = len;
    return true;
}
static void dump_flash_binary_for_viewer(FlashDriver& flash, uint32_t addr, size_t len)
{
    uint8_t buf[64];

    size_t done = 0;

    while(done < len){
        size_t chunk = len - done;
        if(chunk > sizeof(buf)){
            chunk = sizeof(buf);
        }

        if(!flash.read(addr + done, buf, chunk)){
            printf("flash.read failed in binary dump\n");
            return;
        }

        for(size_t i = 0; i < chunk; i++){
            putchar_raw(buf[i]);
        }

        done += chunk;
    }
}
/*
    Flashチェック
    ダミーデーをライト→リード
*/
bool test_flash_basic_rw(FlashDriver& flash)
{
    uint8_t write_data[32];
    uint8_t read_data[32];
    printf("\n=== test_flash_basic_rw ===\n");
    for(size_t i = 0; i < sizeof(write_data); i++){
        write_data[i] = static_cast<uint8_t>(i);
        read_data[i] = 0;
    }

    printf("erase sector...\n");
    if(!flash.sectorErase(TEST_ADDR)){
        printf("sectorErase failed\n");
        return false;
    }

    printf("page program...\n");
    if(!flash.pageProgram(TEST_ADDR, write_data, sizeof(write_data))){
        printf("pageProgram failed\n");
        return false;
    }

    printf("read back...\n");
    if(!flash.read(TEST_ADDR, read_data, sizeof(read_data))){
        printf("read failed\n");
        return false;
    }

    printf("write data:\n");
    dump_hex(write_data, sizeof(write_data));

    printf("read data:\n");
    dump_hex(read_data, sizeof(read_data));

    if(compare_buffers(write_data, read_data, sizeof(write_data))){
        printf("=== test_flash_basic_rw OK  ===\n");
    }else{
        printf("!!! test_flash_basic_rw failed\n");
        return false;
    }
    return true;
}

/*
    Flashチェック
    ダミーデーをライト→リード
*/
bool test_flash_log_frame(FlashDriver& flash, EventLogger& logger)
{
    printf("\n=== test_flash_log_frame ===\n");

    constexpr uint32_t addr = TEST_ADDR;

    const char* msg = "hello flash log";

    LogEvent ev{};
    ev.seq = 100;
    ev.timestamp_us = time_us_32();
    ev.level = LogLevel::INFO;
    ev.event_id = EventId::TEXT_LOG;
    ev.length = static_cast<uint8_t>(strlen(msg));

    if(ev.length > sizeof(ev.payload)){
        printf("message too long\n");
        return false;
    }

    memcpy(ev.payload, msg, ev.length);

    uint8_t frame[128];
    uint8_t read_data[128];

    memset(frame, 0, sizeof(frame));
    memset(read_data, 0, sizeof(read_data));

    const size_t frame_len = logger.buildFrame(ev, frame, sizeof(frame));
    if(frame_len == 0){
        printf("buildFrame failed\n");
        return false;
    }

    printf("frame_len = %u\n", static_cast<unsigned>(frame_len));

    printf("built frame:\n");
    dump_hex(frame, frame_len);

    printf("erase sector...\n");
    if(!flash.sectorErase(addr)){
        printf("sectorErase failed\n");
        return false;
    }

    printf("page program log frame...\n");
    if(!flash.pageProgram(addr, frame, frame_len)){
        printf("pageProgram failed\n");
        return false;
    }

    printf("read back log frame...\n");
    if(!flash.read(addr, read_data, frame_len)){
        printf("read failed\n");
        return false;
    }

    printf("read frame:\n");
    dump_hex(read_data, frame_len);

    if(compare_buffers(frame, read_data, frame_len)){
        printf("=== test_flash_log_frame OK  ===\n");
    }else{
        printf("!!! test_flash_log_frame failed\n");
        return false;
    }

    return true;
}

/*
    Flashチェック
    Log Frameをライト→リードチェック
*/
bool test_flash_storage_append_frame(FlashDriver& flash, FlashLogStorage& storage, EventLogger& logger){
    printf("\n=== test_flash_storage_appned_frame ====\n");
    const char* msg = "hello append storage";
    LogEvent ev{};
    ev.seq = 200;
    ev.timestamp_us = time_us_32();
    ev.level = LogLevel::INFO;
    ev.event_id = EventId::TEXT_LOG;
    ev.length = static_cast<uint8_t>(strlen(msg));

    if(ev.length > sizeof(ev.payload)){
        printf("!!! %s: message too long\n", __func__);
        return false;
    }
    memcpy(ev.payload, msg, ev.length);
    uint8_t frame[128];
    uint8_t read_data[128];
    memset(frame, 0, sizeof(frame));
    memset(read_data, 0, sizeof(read_data));
    const size_t frame_len = logger.buildFrame(ev, frame, sizeof(frame));
    if(frame_len == 0){
        printf("!! %s: buildFrame failed\n", __func__);
        return false;
    }
    printf("frame_len = %u\n", static_cast<unsigned>(frame_len));
    printf("append frame:\n");
    dump_hex(frame, frame_len);

    // FlashLogStorage::append()経由で書く
    if(!storage.append(frame, frame_len)){
        printf("!!! %s: stroage.append failed\n", __func__);
        return false;
    }
    if(!flash.read(FlashLogStorage::LOG_START_ADDR, read_data, frame_len)){
        printf("!!! %s: flash.read failed\n", __func__);
        return false;
    }
    printf("read from storage area:\n");
    dump_hex(read_data, frame_len);

    if(!compare_buffers(frame, read_data, frame_len)){
        printf("!!! append frame compare failed\n");
        return false;
    }
    if(read_data[0] != 0xA5 || read_data[1] != 0x5A){
        printf("magic NG\n");
        return false;
    }

    printf("magic OK: A5 5A\n");
    printf("=== test_flash_storage_append_frame OK ===\n");
    return true;
}
/*
    Flashチェック
    LogFrameを複数ライト→リード
*/
bool test_flash_storage_append_multi_frames(FlashDriver& flash, FlashLogStorage &storage, EventLogger& logger){
    printf("\n=== test_flash_storage_append_multi_frames ===\n");
    const char* messages[] ={
        "flash log 0", "flash log 1", "flash_log 2", "flash log 3", "flash logg 4"
    };
    constexpr size_t FRAME_COUNT = sizeof(messages)/sizeof(messages[0]);

    uint8_t frame[128];
    uint8_t readback[512];
    size_t total_len = 0;
    memset(readback, 0, sizeof(readback));

    for(size_t i = 0; i<FRAME_COUNT; i++){
        size_t frame_len = 0;
        if(!make_text_frame(logger, static_cast<uint32_t>(i), messages[i], frame, sizeof(frame), &frame_len)){
            printf("!!! %s: make_text_frame failed\n", __func__);
            return false;
        }
        printf("append frame:%u len:%u msg:%s\n", static_cast<unsigned>(i), static_cast<unsigned>(frame_len), messages[i]);

        dump_hex(frame, frame_len);
        if(total_len + frame_len > sizeof(readback)){
            printf("!!! %s: total_len [%d, %d]failed\n",
                __func__, static_cast<unsigned>(total_len), static_cast<unsigned>(frame_len));
            return false;
        }
        if(!storage.append(frame, frame_len)){
            printf("!!! %s: storage.appned failed\n", __func__);
            return false;
        }
        total_len += frame_len;
    }
    printf("total_len=%u\n", static_cast<unsigned>(total_len));

    g_flash_log_dump_len = total_len;

    if(!flash.read(FlashLogStorage::LOG_START_ADDR, readback, total_len)){
        printf("!!! %s: flash.read failed\n", __func__);
        return false;
    }
    printf("readback all frames:\n");
    dump_hex(readback, total_len);

    /*
     * 簡易チェック:
     * 先頭がMagicか確認
     */
    if(readback[0] != 0xA5 || readback[1] != 0x5A){
        printf("first magic NG\n");
        return false;
    }

    printf("first magic OK\n");

    /*
     * PC側で logger_viewer.py --file に食わせるための
     * バイナリダンプを最後に出す。
     */
//    printf("\n=== BINARY_DUMP_START len=%u ===\n", static_cast<unsigned>(total_len));
//    dump_flash_binary_for_viewer(flash, FlashLogStorage::LOG_START_ADDR, total_len);
//    printf("\n=== BINARY_DUMP_END ===\n");

    printf("=== test_flash_storage_append_multi_frames OK ===\n");
    return true;
}

/* EventLogをUart送信する */
bool send_flash_logs_to_uart(FlashDriver& flash, UartDma& uart_dma, uint32_t addr, size_t len){
    printf("\n=== send_flash_logs_to_uart ===\n");
    printf("add=0x%08lX len=%u\n",
            static_cast<unsigned long>(addr),
            static_cast<unsigned>(len));
    if(len == 0){
        printf("len is zero\n");
        return false;
    }
    uint8_t buf[64];
    uint8_t sent = 0;

    while(sent < len){
        size_t chunk = len - sent;
        if(chunk > sizeof(buf)){
            chunk = sizeof(buf);
        }
        if(!flash.read(addr + sent, buf, chunk)){
            printf("!!! %s: flash.read failed at offset=%u\n", __func__, static_cast<unsigned>(sent));
            return false;
        }
        uart_dma.write_buffer_blocking(buf, chunk);
        sent += chunk;
    }
    printf("send complete: %u bytes\n", static_cast<unsigned>(sent));
    return true;
}

/*

    logger.logf() → EventLogger内部でbuildFrame() → 
    FlashLogStorage::append() → Flash → UART DMA → Viewer
*/
bool test_event_logger_flash_normal_path(FlashDriver& flash, FlashLogStorage& storage, EventLogger& logger, UartDma& uart_dma){
    printf("\n=== test_event_logger_flash_normal_path ===\n");
    logger.logf(LogLevel::INFO, "normal flash log 0");
    logger.logf(LogLevel::INFO, "normal flash log 1");
    logger.logf(LogLevel::WARN, "normal flash log 2");
    sleep_ms(500);

    constexpr size_t READ_LEN = 256;
    uint8_t readback[READ_LEN];
    memset(readback, 0, sizeof(readback));
    if(!flash.read(FlashLogStorage::LOG_START_ADDR, readback, sizeof(readback))){
        printf("!!! %s: flash.read failed\n", __func__);
        return false;
    }
    printf("flash readback:\n");
    dump_hex(readback, sizeof(readback));
    if(readback[0] != 0xA5 || readback[1] != 0x5A){
        printf("!!! %s: magic NG EventLogger normal path may not habe written to flash yet.\n", __func__);
        return false;
    }
    printf("first magic OK\n");
    /* Viewerへ送る */
    printf("send flash readback to uart...\n");
    uart_dma.write_buffer_blocking(readback, sizeof(readback));
    printf("=== test_event_logger_flash_normal_path OK ===\n");
    return true;
}

bool test_flash_storage_ring_write(FlashDriver& flash, FlashLogStorage& storage){
    (void)flash;
    printf("\n=== test_flash_storage_ring_write ===\n");
    static constexpr size_t TEST_DATA_SIZE = 512;
    static constexpr int TEST_COUNT= 40;

    uint8_t data[TEST_DATA_SIZE];
    for(int i=0; i<TEST_COUNT; i++){
        memset(data, 0, sizeof(data));

        data[0] = static_cast<uint8_t>(i);
        data[1] = 0xAA;
        data[2] = 0x55;
        data[3] = 0x5A;

        uint32_t before = storage.getWriteAddress();
        bool ok = storage.append(data, sizeof(data));
        uint32_t after = storage.getWriteAddress();
        printf("[%02d] addpend %s : before=0x%08X after=0x%08X len=0x%08X\n", i, ok ? "OK": "NG", before, after, static_cast<unsigned>(sizeof(data)));
        if(!ok){
            printf("!!! append failed at i=%d\n", i);
            return false;
        }
    }
    printf("=== ring write test done ===\n");
    return true;
}

bool test_flash_storage_ring_sector_boundary(FlashDriver& flash, FlashLogStorage& storage){
    (void)flash;
    printf("\n=== test_flash_storage_ring_sector_boudary ===\n");
    static constexpr size_t TEST_DATA_SIZE = 0x300;
    static constexpr int TEST_COUNT= 20;

    uint8_t data[TEST_DATA_SIZE];
    for(int i=0; i<TEST_COUNT; i++){
        memset(data, 0, sizeof(data));

        data[0] = static_cast<uint8_t>(i);
        data[1] = 0xAA;
        data[2] = 0x55;
        data[3] = 0x5A;

        uint32_t before = storage.getWriteAddress();
        bool ok = storage.append(data, sizeof(data));
        uint32_t after = storage.getWriteAddress();
        printf("[%02d] addpend %s : before=0x%08X after=0x%08X len=0x%08X\n", i, ok ? "OK": "NG", before, after, static_cast<unsigned>(sizeof(data)));
        if(!ok){
            printf("!!! append failed at i=%d\n", i);
            return false;
        }
    }
    printf("=== sector boundary test done ===\n");
    return true;
}

bool test_flash_storage_restore_write_address(FlashLogStorage& storage){
    printf("\n=== test_flash_storage_restore_write_address ==\n");
    uint32_t before = storage.getWriteAddress();
    printf("before restore: write_addr=0x%08X\n", before);
    if(!storage.restoreWriteAddress()){
        printf("!!! restoreWriteAddress failed\n");
        return false;
    }
    uint32_t after = storage.getWriteAddress();
    printf("after  restore: write_addr=0x%08X\n", after);
    printf("=== restore write address test done ==\n");
    return true;
}
bool test_flash_storage_restore_with_log_frame(FlashLogStorage& storage, EventLogger& logger){
    printf("\n=== test_flash_storage_restore_with_log_frame ===\n");
    uint8_t frame_buf[LOG_FRAME_MAX_SIZE];
    const char* msg = "restore test frame";
    LogEvent event{};
    event.seq = 1;
    event.event_id = EventId::TEXT_LOG;
    event.level = LogLevel::INFO;
    event.timestamp_us = time_us_32();

    size_t payload_len = strlen(msg);
    if(payload_len > LOG_FRAME_MAX_PAYLOAD){
        payload_len = LOG_FRAME_MAX_PAYLOAD;
    }
    event.length = static_cast<uint8_t>(payload_len);
    memcpy(event.payload, msg, payload_len);

    size_t frame_size = logger.buildFrame(event, frame_buf, sizeof(frame_buf));
    if(frame_size == 0){
        printf("!!! buildFrame failed\n");
        return false;
    }
    uint32_t before_append = storage.getWriteAddress();
    if(!storage.append(frame_buf, frame_size)){
        printf("!!! storage.append failed\n");
        return false;
    }
    uint32_t after_append = storage.getWriteAddress();
    printf("append: before=0x%08X after=0x%08X frame_size=%u\n",
        before_append, after_append, static_cast<unsigned>(frame_size));
    
    if(!storage.restoreWriteAddress()){
        printf("!!! restoreWriteAddress failed\n");
        return false;
    }
    uint32_t after_restore = storage.getWriteAddress();
    printf("restore: write_addr=0x%08X\n", after_restore);

    if(after_restore != after_append){
        printf("!!! restore mismatch: expected=0x%08X actual=0x%08X\n", after_append, after_restore);
        return false;
    }
    printf("restore OK\n");
    printf("=== restore with log frame test done ===\n");
    return true;
}

bool test_flash_storage_restore_with_multi_log_frame(FlashLogStorage& storage, EventLogger& logger){
    printf("\n=== test_flash_storage_restore_with_multi_log_frame ===\n");

    static constexpr int FRAME_COUNT = 10;
    uint32_t expected_write_addr = storage.getWriteAddress();

    for(int i=0; i<FRAME_COUNT; i++){
        uint8_t frame_buf[LOG_FRAME_MAX_SIZE];
        char msg[48];
        snprintf(msg, sizeof(msg), "restore multi frame %d", i);

        LogEvent event{};
        event.seq = static_cast<uint32_t>(i+1);
        event.event_id = EventId::TEXT_LOG;
        event.level = LogLevel::INFO;
        event.timestamp_us = time_us_32();

        size_t payload_len = strlen(msg);
        if(payload_len > LOG_FRAME_MAX_PAYLOAD){
            payload_len = LOG_FRAME_MAX_PAYLOAD;
        }
        event.length = static_cast<uint8_t>(payload_len);
        memcpy(event.payload, msg, payload_len);
        size_t frame_size = logger.buildFrame(event, frame_buf, sizeof(frame_buf));
        
        if(frame_size == 0){
            printf("!!! buildFrame failed at i=%d\n", i);
            return false;
        }

        uint32_t before = storage.getWriteAddress();
        if(!storage.append(frame_buf, frame_size)){
            printf("!!! storage.append failed at i%d\n", i);
            return false;
        }
        uint32_t after = storage.getWriteAddress();
        expected_write_addr = after;
        printf(
            "[%02d] append OK: before=0x%08X after=0x%08X frame_size=%u msg=\"%s\"\n",
            i, before, after, static_cast<unsigned>(frame_size), msg);
    }
    printf("expected write_addr=0x%08X\n", expected_write_addr);

    if(!storage.restoreWriteAddress()){
        printf("!!! restoreWriteAddress failed\n");
        return false;
    }
    uint32_t restored = storage.getWriteAddress();
    printf("restored write_addr=0x%08X\n", expected_write_addr);

    if(restored != expected_write_addr){
        printf("!!! restore mismatch: expected=0x%08X actual=0x%08X\n", expected_write_addr, restored);
        return false;
    }
    printf("restore multi frame OK\n");
    printf("=== restore with multi log frames test done ===\n");
    return true;
}

bool test_flash_storage_restore_after_wrap(FlashLogStorage& storage, EventLogger &logger){
    printf("=== test_flash_storage_restore_after_wrap ===\n");

    static constexpr int FRAME_COUNT = 300;
    uint32_t expected_write_addr = storage.getWriteAddress();
    for(int i=0; i<FRAME_COUNT; i++){
        uint8_t frame_buf[LOG_FRAME_MAX_SIZE];
        char msg[48];
        snprintf(msg, sizeof(msg), "wrap restore frame %d", i);
        
        LogEvent event{};
        event.seq = static_cast<uint32_t>(i+1);
        event.event_id = EventId::TEXT_LOG;
        event.level = LogLevel::INFO;
        event.timestamp_us = time_us_32();
        size_t payload_len = strlen(msg);
        if(payload_len > LOG_FRAME_MAX_PAYLOAD){
            payload_len = LOG_FRAME_MAX_PAYLOAD;
        }
        event.length = static_cast<uint8_t>(payload_len);
        memcpy(event.payload, msg, payload_len);
        size_t frame_size = logger.buildFrame(event, frame_buf, sizeof(frame_buf));

        if(frame_size == 0){
            printf("!!! buildFrame failed at i=%d\n", i);
            return false;
        }

        uint32_t before = storage.getWriteAddress();
        if(!storage.append(frame_buf, frame_size)){
            printf("!!! storage.append failed at i=%d\n", i);
            return false;
        }

        uint32_t after = storage.getWriteAddress();
        expected_write_addr = after;
        if(after < before){
            printf("wrap detected at i=%d: before=0x%08X after=0x%08X frame_size=%u\n", i, before, after, static_cast<unsigned>(frame_size));
            
        }
    }
    printf("expected write_addr=0x%08X\n", expected_write_addr);

    if(!storage.restoreWriteAddress()){
        printf("!!! restoreWriteAddress failed\n");
        return false;
    }

    uint32_t restored = storage.getWriteAddress();
    printf("restored write_addr=0x%08x\n", restored);

    if(restored != expected_write_addr){
        printf("!!! restore mismatch after wrap: expected=0x%08X, actual=0x%08X\n", expected_write_addr, restored);
        printf("NOTE: this mismatch is expected with simple forward scan after wrap.\n");
        printf("Next step: use seq number to find latest frame.\n");
        return false;
    }
    printf("restore after wrap OK\n");
    printf("=== restore after wrap test done ===\n");

    return true;
}

bool test_flash_storage_dump_oldest_first(FlashLogStorage& storage){
    printf("\n=== test_flash_storage_dump_oldest_first ===\n");
    if(!storage.dumpFramesOldestFirst()){
        printf("!!! dumpFrameOldestFirst failed\n");
        return false;
    }
    printf("=== dump oldest first test done ===\n");
    return true;
}

static bool rawProgramForTest(FlashDriver& flash, uint32_t addr, const uint8_t* data, size_t len){
    while(len>0){
        uint32_t page_remain = FlashDriver::PAGE_SIZE - (addr % FlashDriver::PAGE_SIZE);
        size_t chunk = len;

        if(chunk > page_remain){
            chunk = page_remain;
        }
        if(!flash.pageProgram(addr, data, chunk)){
            return false;
        }
        addr += chunk;
        data += chunk;
        len -= chunk;
    }
    return true;
}

static size_t buildTextFrameForTest(EventLogger &logger, int32_t seq, const char* text, uint8_t* out_buf, size_t out_buf_size){
    LogEvent event{};

    event.seq = seq;
    event.timestamp_us = time_us_32();
    event.level = LogLevel::INFO;
    event.event_id = EventId::TEXT_LOG;

    size_t len = strlen(text);
    if(len > LOG_PAYLOAD_MAX){
        len = LOG_PAYLOAD_MAX;
    }
    event.length = static_cast<uint8_t>(len);
    memcpy(event.payload, text, len);

    return logger.buildFrame(event, out_buf, out_buf_size);
}

void test_pseudo_power_cut(FlashDriver& flash){
    printf("\n=== test_pseudo_power_cut ===\n");
    constexpr uint32_t LOG_START_ADDR = 0x00001000;
    constexpr uint32_t LOG_END_ADDR = 0x00003000;

    UartDma uart(uart0, UartDma::UART_BAUDRATE_460800, UartDma::UART_TX_PIN, UartDma::UART_RX_BUF_SIZE); 
    uart.init();
 
    FlashLogStorage storage(flash, LOG_START_ADDR, LOG_END_ADDR);
    storage.init();
    EventLogger logger(uart, &storage);
    logger.init(32);

    printf("erase log area ...\n");
    storage.eraseLogArea();
    printf("erase done\n");
    storage.restoreWriteAddress();
    uint8_t frame_buf[128];
    // 1.正常frameをかく(5個)
    for(uint32_t i = 0; i<5; i++){
        char msg[64];
        snprintf(msg, sizeof(msg), "normal frame %lu", static_cast<unsigned long>(i));
        size_t frame_len = buildTextFrameForTest(logger, i, msg, frame_buf, sizeof(frame_buf));
        if(!storage.append(frame_buf, frame_len)){
            printf("append failed at seq=%lu\n", static_cast<unsigned long>(i));
            return;
        }
    }
    printf("\n--- before pseudo power cut ---\n");
    storage.printStatus("storage");
    uint32_t cut_addr = storage.getWriteAddressForTest();
    printf("pseudo power cut addr=0x%08lX\n", static_cast<unsigned long>(cut_addr));
    // 2.次のフレームを作る
    size_t bad_frame_len = buildTextFrameForTest(logger, 5, "this frame is partially written", frame_buf, sizeof(frame_buf));
    printf("full frame len=%u\n", static_cast<unsigned>(bad_frame_len));
    // 3.フレーム先頭だけ書く
    // 例: magic + header の途中まで書く
    constexpr size_t CUT_LEN = 8;
    printf("write only first %u bytes\n", static_cast<unsigned>(CUT_LEN));
    if(!rawProgramForTest(flash, cut_addr, frame_buf, CUT_LEN)){
        printf("rawProgramForTest failed\n");
        return;
    }
    // 4.再起動相当
    FlashLogStorage restored(flash, LOG_START_ADDR, LOG_END_ADDR);
    restored.init();
    printf("\n--- after pseudo reboot / restore ---\n");
    restored.printStatus("restored");
    // 5. 続けて３メッセージ書く
    for (uint32_t i = 5; i < 8; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "after restore frame %lu",
                 static_cast<unsigned long>(i));

        size_t frame_len = buildTextFrameForTest(
            logger, i, msg, frame_buf, sizeof(frame_buf));

        if (!restored.append(frame_buf, frame_len)) {
            printf("append after restore failed seq=%lu\n",
                static_cast<unsigned long>(i));
            return;
        }
    }

    restored.printStatus("after append following restore");

    printf("\n--- read frames oldest first after restore append ---\n");

    if (!restored.readFramesOldestFirstTest()) {
        printf("!!! readFramesOldestFirstTest failed\n");
        return;
    }
}












