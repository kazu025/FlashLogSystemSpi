#ifndef FLASH_LOG_STORAGE_H
#define FLASH_LOG_STORAGE_H
/* ----------------------------------------------------------- */
#include <stddef.h>
#include <stdint.h>

#include "FlashDriver.h"
#include "LogProtocol.h"
#include "UartDma.h"
#include "FreeRTOS.h"
#include "semphr.h"

class FlashDriver;

class FlashLogStorage{
public:
    /* === ログ保存領域 === */
    static constexpr uint32_t LOG_START_ADDR = 0x00001000;
    static constexpr uint32_t LOG_END_ADDR   = 0x00011000;
    struct Config {
        uint32_t start_addr;
        uint32_t end_addr;
    };
    public:
    FlashLogStorage(FlashDriver& flash);
    FlashLogStorage(FlashDriver& flash, uint32_t start_addr, uint32_t end_addr);
    bool init();
    bool eraseLogArea();
    bool append(const uint8_t* data, size_t len);
    bool read(uint32_t addr, uint8_t* buf, size_t buf_size);

    bool restoreWriteAddress();
    bool findOldestFrame(uint32_t* out_addr);
    bool findNewestFrame(uint32_t* out_addr, uint32_t* out_seq);
    bool dumpFramesOldestFirst();
    bool readFrame(uint32_t addr, uint8_t* out, size_t out_size, size_t* out_len);
    bool readFramesOldestFirstTest();
    bool sendFramesOldestFirst(UartDma& uart);
    bool sendLatestFrames(UartDma& uart, uint32_t count);
    uint32_t getWriteAddress() const;
    uint32_t getOldestAddress() const; //
    uint32_t getNewestAddress() const; //
    uint32_t getStartAddress() const;
    uint32_t getEndAddress() const;
    uint32_t getValidFrameCount() const;    //
    size_t   getRemainingSize() const;

private:
    FlashDriver& flash_;
    Config config_;
    uint32_t write_addr_;
    uint32_t oldest_addr_;
    uint32_t newest_addr_;
    uint32_t newest_seq_;
    uint32_t valid_frame_count_;
    bool initialized_;
    
    bool isConfigValid(const Config& config) const;
    bool isRangeValid(uint32_t addr, size_t len) const;
    bool isValidFrameAt(uint32_t addr, LogFrameHeader* out_header, size_t* out_frame_size);
    bool rebuildIndexFromFlash();
    
    bool isSectorStart(uint32_t addr) const;
    uint32_t nextSectorAddress(uint32_t addr) const;
    uint32_t currentSectorEnd(uint32_t addr) const;
    uint32_t wrapAddress(uint32_t addr) const; 
    bool isSeqNewer(uint32_t a, uint32_t b) const;
    
    bool readHeader(uint32_t addr, LogFrameHeader* out_header);
    
    void updateIndexAfterAppend(uint32_t frame_addr, const LogFrameHeader& header);
    void invalidateFramesInErasedSector(uint32_t sector_addr);
    
    // mutex
    SemaphoreHandle_t mutex_;
    bool lock(TickType_t timeout_ticks = portMAX_DELAY);
    void unlock();
    class MutexGuard{
    public:
        explicit MutexGuard(FlashLogStorage& owner, TickType_t timeout_ticks=portMAX_DELAY);
        ~MutexGuard();
        bool locked() const;
        MutexGuard(const MutexGuard&) = delete;
        MutexGuard& operator=(const MutexGuard&) = delete;
    private:
        FlashLogStorage& owner_;
        bool locked_;
    };
};

/* ----------------------------------------------------------- */
#endif // FLASH_LOG_STORAGE_H