#ifndef FLASH_DRIVER_H
#define FLASH_DRIVER_H
/* ---------------------------------------------------- */
#include <cstddef>
#include <cstdint>
#include "pico/stdlib.h"
#include "hardware/spi.h"

class FlashDriver{
public:
    /* === SPI設定 === */
    static constexpr uint PIN_SPI_SCK  = 18;
    static constexpr uint PIN_SPI_MOSI = 19;
    static constexpr uint PIN_SPI_MISO = 16;
    static constexpr uint PIN_SPI_CS   = 17;
    static constexpr uint32_t SPI_BAUDRATE_10M  = 10 * 1024 * 1024;
    /* === Flash === */
    static constexpr uint32_t PAGE_SIZE         = 256u;              // 256Byte
    static constexpr uint32_t SECTOR_SIZE       = 4096u;             // 4kByte
    static constexpr uint32_t FLASH_SIZE        = 4u * 1024u * 1024u;  // W25Q32 = 4MB
    static constexpr uint32_t WAIT_BUSY_MS      = 500u;              // WIP(=1：書き込み中/消去中)が0になるを飲まつ
    static constexpr uint32_t WRITE_ENABLE_MS   = 1u;                // WEL（=1:書き込み許可フラグ）が立つまで待つ
public:
    FlashDriver(spi_inst_t* spi, uint cs_pin, uint sck_pin, uint mosi_pin, uint miso_pin, uint32_t spi_baud_hz);
    bool init();
    bool read(uint32_t addr, uint8_t* buf, size_t len);
    bool pageProgram(uint32_t addr, const uint8_t* data, size_t len);
    bool sectorErase(uint32_t addr);
    uint8_t readStatus1();
    bool waitWhileBusy(uint32_t timeout_ms = WAIT_BUSY_MS);

    bool chipErase();

    uint32_t readJedecId();
    uint32_t capacity() const;
private:
    static constexpr int8_t CMD_READ_DATA       = 0x03;
    static constexpr int8_t CMD_FAST_READ       = 0x0B;
    static constexpr int8_t CMD_PAGE_PROGRAM    = 0x02;
    static constexpr int8_t CMD_WRITE_ENABLE    = 0x06;
    static constexpr int8_t CMD_READ_STATUS1    = 0x05;
    static constexpr int8_t CMD_SECTOR_ERASE    = 0x20;     // 4kB
    static constexpr int8_t CMD_CHIP_ERASE      = 0xC7;
    static constexpr int8_t CMD_JEDEC_ID        = 0x9F;

    spi_inst_t* spi_;
    uint cs_pin_;
    uint sck_pin_;
    uint miso_pin_;
    uint mosi_pin_;
    uint32_t spi_baud_hz_;

    void csSelect();
    void csDeselect();

    bool writeEnable(uint32_t timeout_ms = WRITE_ENABLE_MS);
    
    bool isPageProgramValid(uint32_t addr, size_t len) const;
    bool isAddressValid(uint32_t addr, size_t len) const;
};
/* ---------------------------------------------------- */
#endif // FLASH_DRIVER_H