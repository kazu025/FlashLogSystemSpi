#include "FlashLogStorage.h"
#include "Crc32.h"

#include <stdio.h>
#include <string.h>
FlashLogStorage::FlashLogStorage(FlashDriver& flash)
    : FlashLogStorage(flash, LOG_START_ADDR, LOG_END_ADDR){}
FlashLogStorage::FlashLogStorage(FlashDriver& flash, uint32_t start_addr, uint32_t end_addr)
    : flash_(flash), write_addr_(0), oldest_addr_(0), newest_addr_(0), newest_seq_(0),
    valid_frame_count_(0), initialized_(false){
        config_.start_addr = start_addr;
        config_.end_addr   = end_addr;
}
/*
初期化
*/
bool FlashLogStorage::init(){
    if(!isConfigValid(config_)) return false;
    
    write_addr_ = config_.start_addr;
    oldest_addr_ = config_.start_addr;
    newest_addr_ = config_.start_addr;
    newest_seq_ = 0;
    valid_frame_count_ = 0;
    initialized_ = true;
    restoreWriteAddress();
    return true;
}
/*
    Flash Log エリアチェック
*/
bool FlashLogStorage::isConfigValid(const Config& config) const {
    if(config.start_addr >= config.end_addr)                return false;
    if((config.start_addr % FlashDriver::SECTOR_SIZE) != 0u) return false;
    if(config.end_addr > FlashDriver::FLASH_SIZE)           return false;
    if((config.end_addr % FlashDriver::SECTOR_SIZE) != 0u)   return false;
    return true;
}
/*
    Log Storage Area消去＆ログ初期化
*/
bool FlashLogStorage::eraseLogArea(){
    if(!initialized_) return false;
    for(uint32_t addr = config_.start_addr; addr < config_.end_addr; addr += FlashDriver::SECTOR_SIZE){
        if(!flash_.sectorErase(addr)){
            printf("!!! error eraseLogArea address: 0x%08x\n", addr);
            return false;
        }    
    }    
    write_addr_ = config_.start_addr;
    oldest_addr_ = config_.start_addr;
    newest_addr_ = config_.start_addr;
    newest_seq_ = 0;
    valid_frame_count_ = 0;
    return true;
}    
/*
    アドレス及びサイズのチェック
*/
bool FlashLogStorage::isRangeValid(uint32_t addr, size_t len) const {
    if(!initialized_ || len ==0) return false;
    if(addr < config_.start_addr) return false;
    if(addr >= config_.end_addr) return false;
    if(addr + len > config_.end_addr) return false;
    return true;
}
/*
    Log書き込み
*/
bool FlashLogStorage::append(const uint8_t* data, size_t len){
    if(!initialized_) return false;
    if(data == nullptr || len == 0) return false;

    // 1フレームがセクターを跨ぐ場合は
    if(len > FlashDriver::SECTOR_SIZE){
        printf("!!! %s:append too large len=%u\n", __func__);
        return false;
    }

    uint32_t addr = wrapAddress(write_addr_);

    // このログが現在のsectorを跨ぐなら次のsectorへ
    uint32_t sector_end = currentSectorEnd(addr);
    if(addr + len > sector_end){
        addr = nextSectorAddress(addr);
        addr = wrapAddress(addr);
    }

/*      sector先頭から書く場合は、そのsectorを丸ごとerase。
        ここで古いログが消えるが、後でrebuildIndexFromFlash()して
        oldest/newest/write_addrを再構築する。*/
    if(isSectorStart(addr)){
        if(!flash_.sectorErase(addr)){
            printf("!!! %s:sectorErase failed addr=0x%08X\n", __func__, addr);
            return false;
        }
    }

    const uint32_t frame_start_addr = addr;
    // page境界を跨ぐ可能性があるので、page単位に分解して書く
    size_t remaining = len;
    const uint8_t* p = data;

    while(remaining > 0){
        const uint32_t page_offset = addr & (FlashDriver::PAGE_SIZE - 1u);
        const size_t page_remain = FlashDriver::PAGE_SIZE - page_offset;
        const size_t chunk = (remaining < page_remain) ? remaining : page_remain;

        if(addr + chunk > config_.end_addr){
            printf("!!! %s: append crosses log end unexpectedly\n", __func__);
            return false;
        }

        if(!flash_.pageProgram(addr, p, chunk)){
            printf("!!! %s: pageProgram failed addr=0x%08X chunk=%u\n", __func__, addr, static_cast<unsigned>(chunk));
            return false;
        }

        addr += static_cast<uint32_t>(chunk);
        p += chunk;
        remaining -= chunk;
    }

    write_addr_ = wrapAddress(addr);
    
    LogFrameHeader written_header{};
    size_t written_frame_size = 0;
    if(!isValidFrameAt(frame_start_addr, &written_header, &written_frame_size)){
        printf("!!! appended frame invalid at 0x%08X\n", frame_start_addr);
        return false;
    }
    updateIndexAfterAppend(frame_start_addr, written_header);

    return true;
}
/*
    読み込み
*/
bool FlashLogStorage::read(uint32_t addr, uint8_t* out, size_t len){
    if(out == nullptr || !isRangeValid(addr, len))  return false;
    return flash_.read(addr, out, len);
} 

bool FlashLogStorage::restoreWriteAddress(){
    if(!initialized_) return false;
    return rebuildIndexFromFlash();
}

bool FlashLogStorage::rebuildIndexFromFlash(){

    uint32_t addr = config_.start_addr;
    bool found = false;
    uint32_t oldest_seq = 0;
    uint32_t oldest_addr = config_.start_addr;

    uint32_t newest_seq = 0;
    uint32_t newest_addr = config_.start_addr;
    uint32_t newest_next_addr = config_.start_addr;

    uint32_t count = 0;

    while(addr < config_.end_addr){
        LogFrameHeader header{};
        size_t frame_size = 0;
        if(isValidFrameAt(addr, &header, &frame_size)){
            uint32_t next_addr = wrapAddress(addr + static_cast<uint32_t>(frame_size));
            if(!found){
                found = true;
                oldest_seq = header.seq;
                oldest_addr = addr;

                newest_seq = header.seq;
                newest_addr = addr;
                newest_next_addr = next_addr;
            }else{
                if(isSeqNewer(header.seq, newest_seq)){
                    newest_seq = header.seq;
                    newest_addr = addr;
                    newest_next_addr = next_addr;
                }

                if(isSeqNewer(oldest_seq, header.seq)){
                    oldest_seq = header.seq;
                    oldest_addr = addr;
                }
            }
            count++;
            addr += static_cast<uint32_t>(frame_size);
        }else{
            addr ++;
        }
    }
    if(found){
        oldest_addr_ = oldest_addr;
        newest_addr_ = newest_addr;
        newest_seq_ = newest_seq;
        write_addr_ = newest_next_addr;
        valid_frame_count_ = count;
    }else{
        oldest_addr_ = config_.start_addr;
        newest_addr_ = config_.start_addr;
        newest_seq_ = 0;
        write_addr_ = config_.start_addr;
        valid_frame_count_ = 0;
    }
    printf("rebuildIndex: found=%d count=%u oldest=0x%08X newest=0x%08X newest_seq=%u write=0x%08X\n",
           found ? 1 : 0,
           static_cast<unsigned>(valid_frame_count_),
           oldest_addr_,
           newest_addr_,
           newest_seq_,
           write_addr_);
    return true;
}

bool FlashLogStorage::findOldestFrame(uint32_t* out_addr){
    if(out_addr == nullptr) return false;
    if(valid_frame_count_ == 0) return false;

    *out_addr = oldest_addr_;
    return true;
}

bool FlashLogStorage::findNewestFrame(uint32_t* out_addr, uint32_t* out_seq){
    if(out_addr == nullptr || out_seq == nullptr) return false;
    if(valid_frame_count_ == 0) return false;

    *out_addr = newest_addr_;
    *out_seq = newest_seq_;
    return true;
}

bool FlashLogStorage::dumpFramesOldestFirst(){
    if(!initialized_) return false;
    if(valid_frame_count_ == 0){
        printf("dumpFramesOldestFirst: no valid frame\n");
        return true;
    }

    uint32_t addr = oldest_addr_;

    printf("\n=== dumpFramesOldestFirst ===\n");
    printf("oldest=0x%08X newest=0x%08X newest_seq=%u count=%u\n", oldest_addr_, newest_addr_, newest_seq_,
           static_cast<unsigned>(valid_frame_count_));

    uint32_t printed = 0;
    uint32_t scanned = 0;
    const uint32_t area_size = config_.end_addr - config_.start_addr;
    
    while(scanned < area_size){
        LogFrameHeader header{};
        size_t frame_size = 0;

        if(isValidFrameAt(addr, &header, &frame_size)){
            printf("frame: addr=0x%08X seq=%u event_id=%u level=%u len=%u\n",
                   addr, header.seq, header.event_id, header.level, header.length);
            printed++;

            if(header.seq == newest_seq_){
                break;
            }

            addr = wrapAddress(addr + static_cast<uint32_t>(frame_size));
            scanned += static_cast<uint32_t>(frame_size);
        }else{
            addr = wrapAddress(addr + 1u);
            scanned ++;
        }
    }

    printf("dumpFramesOldestFirst: printed=%u\n", static_cast<unsigned>(printed));
    printf("=== dumpFramesOldestFirst done ===\n");
    return true;
}

bool FlashLogStorage::isValidFrameAt(uint32_t addr, LogFrameHeader* out_header, size_t* out_frame_size){
    if(out_header == nullptr || out_frame_size == nullptr) return false;
    *out_frame_size = 0;
    *out_header = {};

    const size_t min_size = LOG_FRAME_MAGIC_SIZE + LOG_FRAME_HEADER_SIZE + LOG_FRAME_CRC_SIZE;
    if(addr < config_.start_addr) return false;
    if(addr + min_size > config_.end_addr) return false;

    uint8_t magic[LOG_FRAME_MAGIC_SIZE];

    if(!flash_.read(addr, magic, sizeof(magic))){
        return false;
    }
    if(magic[0] != LOG_FRAME_MAGIC[0] || magic[1] != LOG_FRAME_MAGIC[1]){
        return false;
    }
    
    LogFrameHeader header{};
    const uint32_t header_addr = addr + LOG_FRAME_MAGIC_SIZE;

    if(!flash_.read(header_addr, reinterpret_cast<uint8_t*>(&header), sizeof(header))){
        return false;
    }

    if(header.length > LOG_FRAME_MAX_PAYLOAD){
        printf("!!! invalid length=%u\n", header.length);
        return false;
    }

    const size_t frame_size =
        LOG_FRAME_MAGIC_SIZE +
        LOG_FRAME_HEADER_SIZE +
        header.length +
        LOG_FRAME_CRC_SIZE;

    if(addr + frame_size > config_.end_addr){
        printf("!!! frame exceeds log area\n");
        return false;
    }

    uint8_t frame_buf[LOG_FRAME_MAX_SIZE];

    if(!flash_.read(addr, frame_buf, frame_size)){
        return false;
    }

    const uint32_t crc_offset = LOG_FRAME_MAGIC_SIZE + LOG_FRAME_HEADER_SIZE + header.length;

    uint32_t stored_crc = 0;
    memcpy(&stored_crc, frame_buf + crc_offset, sizeof(stored_crc));

    const uint32_t calc_crc = Crc32::calculate(frame_buf, frame_size - LOG_FRAME_CRC_SIZE);

           if(calc_crc != stored_crc){
        printf("!!! crc mismatch at 0x%08X\n", addr);
        return false;
    }

    *out_header = header;
    *out_frame_size = frame_size;
    //printf("valid frame at 0x%08X size=%u\n", addr, static_cast<unsigned>(frame_size));
    return true;
}

bool FlashLogStorage::isSectorStart(uint32_t addr) const {
    return (addr % FlashDriver::SECTOR_SIZE) == 0u;
}

uint32_t FlashLogStorage::nextSectorAddress(uint32_t addr) const {
    const uint32_t sector_offset = addr % FlashDriver::SECTOR_SIZE;

    if(sector_offset == 0u){
        return addr + FlashDriver::SECTOR_SIZE;
    }

    return addr + (FlashDriver::SECTOR_SIZE - sector_offset);
}

uint32_t FlashLogStorage::currentSectorEnd(uint32_t addr) const {
    return (addr & ~(FlashDriver::SECTOR_SIZE - 1u)) + FlashDriver::SECTOR_SIZE;
}

uint32_t FlashLogStorage::wrapAddress(uint32_t addr) const {
    if(addr >= config_.end_addr){
        return config_.start_addr;
    }
    return addr;
}

bool FlashLogStorage::isSeqNewer(uint32_t a, uint32_t b) const {
    return static_cast<int32_t>(a - b) > 0;
}

uint32_t FlashLogStorage::getWriteAddress() const {
    return write_addr_;
}

uint32_t FlashLogStorage::getOldestAddress() const {
    return oldest_addr_;
}

uint32_t FlashLogStorage::getNewestAddress() const {
    return newest_addr_;
}

uint32_t FlashLogStorage::getStartAddress() const {
    return config_.start_addr;
}

uint32_t FlashLogStorage::getEndAddress() const {
    return config_.end_addr;
}

uint32_t FlashLogStorage::getValidFrameCount() const {
    return valid_frame_count_;
}

size_t FlashLogStorage::getRemainingSize() const {
    if(!initialized_) return 0;
    if(write_addr_ >= config_.end_addr) return 0;
    return static_cast<size_t>(config_.end_addr - write_addr_);
}    

bool FlashLogStorage::readHeader(uint32_t addr, LogFrameHeader* out_header){
    if(out_header == nullptr) return false;
    return flash_.read(addr, reinterpret_cast<uint8_t*>(out_header), sizeof(LogFrameHeader));
}

void FlashLogStorage::updateIndexAfterAppend(uint32_t frame_addr, const LogFrameHeader& header){
    if(valid_frame_count_ == 0){
        oldest_addr_ = frame_addr;
        newest_addr_ = frame_addr;
        newest_seq_ = header.seq;
        valid_frame_count_ = 1;
        return;
    }
    newest_addr_ = frame_addr;
    newest_seq_ = header.seq;
    valid_frame_count_++;
}

void FlashLogStorage::invalidateFramesInErasedSector(uint32_t sector_addr){
    if(valid_frame_count_ == 0) return;

    const uint32_t sector_end = sector_addr + FlashDriver::SECTOR_SIZE;

    uint32_t addr = sector_addr;

    uint32_t removed = 0;

    while(addr < sector_end && addr < config_.end_addr){
        LogFrameHeader header{};
        size_t frame_size = 0;

        if(isValidFrameAt(addr, &header, &frame_size)){
            removed++;
            addr += static_cast<uint32_t>(frame_size);
        }else{
            addr++;
        }
    }

    if(removed >= valid_frame_count_){
        valid_frame_count_ = 0;
        oldest_addr_ = config_.start_addr;
        newest_addr_ = config_.start_addr;
        newest_seq_ = 0;
        write_addr_ = sector_addr;
        return;
    }

    valid_frame_count_ -= removed;

    if(oldest_addr_ >= sector_addr && oldest_addr_ < sector_end){
        uint32_t scan = wrapAddress(sector_end);

        const uint32_t area_size = config_.end_addr - config_.start_addr;
        uint32_t scanned = 0;

        while(scanned < area_size){
            LogFrameHeader header{};
            size_t frame_size = 0;

            if(isValidFrameAt(scan, &header, &frame_size)){
                oldest_addr_ = scan;
                return;
            }

            scan = wrapAddress(scan + 1u);
            scanned++;
        }

        valid_frame_count_ = 0;
        oldest_addr_ = config_.start_addr;
        newest_addr_ = config_.start_addr;
        newest_seq_ = 0;
    }
}