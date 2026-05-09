#include "FlashDriver.h"
#include <cstdio>

FlashDriver::FlashDriver(spi_inst_t* spi, uint cs_pin, uint sck_pin, uint mosi_pin, uint miso_pin, uint32_t spi_baud_hz):
spi_(spi), cs_pin_(cs_pin), sck_pin_(sck_pin), mosi_pin_(mosi_pin), miso_pin_(miso_pin), spi_baud_hz_(spi_baud_hz){}

/*
    SPI 初期化
*/
bool FlashDriver::init(){
    spi_init(spi_, spi_baud_hz_);

    gpio_set_function(sck_pin_, GPIO_FUNC_SPI);
    gpio_set_function(mosi_pin_, GPIO_FUNC_SPI);
    gpio_set_function(miso_pin_, GPIO_FUNC_SPI);
    
    gpio_init(cs_pin_);
    gpio_set_dir(cs_pin_, GPIO_OUT);
    csDeselect(); 
    uint32_t jedec = readJedecId();
    return (jedec != 0u && jedec != 0xFFFFFFFFu);
}
/*
    Chip Select Control
    */
void FlashDriver::csSelect(){
    gpio_put(cs_pin_, 0);
}
void FlashDriver::csDeselect(){
    gpio_put(cs_pin_, 1);
}

bool FlashDriver::isAddressValid(uint32_t addr, size_t len) const {
    if (len == 0) return false;
    if (addr >= FLASH_SIZE) return false;
    if (len  > FLASH_SIZE) return false;
    if (addr + len > FLASH_SIZE) return false;
    return true;
}

/*
SR1ステータス取得
*/
uint8_t FlashDriver::readStatus1(){
    uint8_t cmd = CMD_READ_STATUS1;
    uint8_t sr1 = 0;

    csSelect();
    spi_write_blocking(spi_, &cmd, 1);
    spi_read_blocking(spi_, 0x00, &sr1, 1);
    csDeselect();
    return sr1;
}
/*
    書き込み許可待ち
*/
bool FlashDriver::writeEnable(uint32_t timeout_ms){
    csSelect();
    uint8_t cmd = CMD_WRITE_ENABLE;
    spi_write_blocking(spi_, &cmd, 1);
    csDeselect();

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while(absolute_time_diff_us(get_absolute_time(), deadline) > 0){
        uint8_t sr1 = readStatus1();
        if(sr1 & 0x02){
            return true;
        }
        sleep_us(10);
    }
    return false;
}
/*
    書き込み完了まで待つ
*/
bool FlashDriver::waitWhileBusy(uint32_t timeout_ms){
    const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while(absolute_time_diff_us(get_absolute_time(), deadline) > 0){
        const uint8_t sr1 = readStatus1();
        if((sr1 & 0x01u) == 0){
            return true;
        }
        sleep_ms(1);
    }
    return false;
}
/*
    読み込み
*/
bool FlashDriver::read(uint32_t addr, uint8_t* buf, size_t len){
    if(buf == nullptr) return false;
    if(!isAddressValid(addr, len)) return false;
    uint8_t cmd[4];
    cmd[0] = CMD_READ_DATA;
    cmd[1] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    cmd[2] = static_cast<uint8_t>((addr >>  8) & 0xFF);
    cmd[3] = static_cast<uint8_t>(addr & 0xFF);
    
    csSelect();
    spi_write_blocking(spi_, cmd, sizeof(cmd));
    spi_read_blocking(spi_, 0x00, buf, len);
    csDeselect();
    return true;
}
/*
    書き込み
*/
bool FlashDriver::pageProgram(uint32_t addr, const uint8_t* data, size_t len){
    if(data == nullptr) return false;
    if(!isPageProgramValid(addr, len)) return false;
    
    if(!writeEnable()) return false;
    
    uint8_t cmd[4];
    cmd[0] = CMD_PAGE_PROGRAM;
    cmd[1] = static_cast<uint8_t>((addr >> 16) & 0xFF);
    cmd[2] = static_cast<uint8_t>((addr >>  8) & 0xFF);
    cmd[3] = static_cast<uint8_t>(addr & 0xFF);
    csSelect();
    spi_write_blocking(spi_, cmd, sizeof(cmd));
    spi_write_blocking(spi_, data, len);
    csDeselect();
    return waitWhileBusy();
}
/*
    セクタ消去
*/
bool FlashDriver::sectorErase(uint32_t addr){
    const uint32_t sector_addr = addr & ~(SECTOR_SIZE - 1u);
    if(!isAddressValid(sector_addr, 1)) return false;
    if(!writeEnable()) return false;

    uint8_t cmd[4];
    cmd[0] = CMD_SECTOR_ERASE;
    cmd[1] = static_cast<uint8_t>((sector_addr >> 16) & 0xFF);
    cmd[2] = static_cast<uint8_t>((sector_addr >>  8) & 0xFF);
    cmd[3] = static_cast<uint8_t>(sector_addr & 0xFF);
    csSelect();
    spi_write_blocking(spi_, cmd, sizeof(cmd));
    csDeselect();
    return waitWhileBusy();
}
/*
    chip 消去
*/
bool FlashDriver::chipErase(){
    if(!writeEnable()) return false;
    csSelect();
    uint8_t cmd = CMD_CHIP_ERASE;
    spi_write_blocking(spi_, &cmd, 1);
    csDeselect();
    return waitWhileBusy();
}

uint32_t FlashDriver::readJedecId(uint8_t* out){
    uint8_t cmd = CMD_JEDEC_ID;
    uint8_t id[3] = {0};

    csSelect();
    spi_write_blocking(spi_, &cmd, 1);
    spi_read_blocking(spi_, 0x00, id, sizeof(id));
    csDeselect();
    if(out!=nullptr){
        for(int i=0; i<3; i++) out[i] = id[i];
    }
    return  (static_cast<uint32_t>(id[0]) << 16) |
            (static_cast<uint32_t>(id[1]) << 8)  |
            (static_cast<uint32_t>(id[2]));
}

uint32_t FlashDriver::capacity() const {
    return FLASH_SIZE;
}
bool FlashDriver::isPageProgramValid(uint32_t addr, size_t len) const {
    if(!isAddressValid(addr, len)) return false;
    if(len > PAGE_SIZE) return false;

    const uint32_t page_off = addr & (PAGE_SIZE - 1u);
    return (page_off + len) <= PAGE_SIZE;
}



