#include "FlashLogStorage.h"
#include "Crc32.h"

#include <stdio.h>
#include <string.h>

//#define DEBUG_FlashLogStorage

FlashLogStorage::FlashLogStorage(FlashDriver& flash)
    : FlashLogStorage(flash, LOG_START_ADDR, LOG_END_ADDR){}
FlashLogStorage::FlashLogStorage(FlashDriver& flash, uint32_t start_addr, uint32_t end_addr)
    : flash_(flash), write_addr_(0), oldest_addr_(0), newest_addr_(0), newest_seq_(0),
        valid_frame_count_(0), mutex_(nullptr), initialized_(false){
        config_.start_addr = start_addr;
        config_.end_addr   = end_addr;
}
/*
    mutex
*/
bool FlashLogStorage::lock(TickType_t timeout_ticks){
    if(mutex_ == nullptr)    return false;
    return xSemaphoreTakeRecursive(mutex_, timeout_ticks) == pdTRUE;
}
void FlashLogStorage::unlock(){
    if(mutex_ == nullptr) return; 
    xSemaphoreGiveRecursive(mutex_);
}
FlashLogStorage::MutexGuard::MutexGuard(FlashLogStorage& owner, TickType_t timeout_ticks):
    owner_(owner), locked_(owner_.lock(timeout_ticks)){}
FlashLogStorage::MutexGuard::~MutexGuard(){
    if(locked_) owner_.unlock();
}
bool FlashLogStorage::MutexGuard::locked()const {
    return locked_;
}
/*
初期化
*/
bool FlashLogStorage::init(){
    if(!isConfigValid(config_)) return false;

    if(mutex_ == nullptr){
        mutex_ = xSemaphoreCreateRecursiveMutex();
        if(mutex_ == nullptr){
            printf("!!! %s: XSemaphoreCreateRecursiveMutex failed\n", __func__);
            return false;
        }
    }
    
    write_addr_ = config_.start_addr;
    oldest_addr_ = config_.start_addr;
    newest_addr_ = config_.start_addr;
    newest_seq_ = 0;
    valid_frame_count_ = 0;
    initialized_ = true;
    //restoreWriteAddress();
    rebuildIndexFromFlash();
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

    MutexGuard guard(*this);
    if(!guard.locked()) return false;

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

    MutexGuard gaurd(*this);
    if(!gaurd.locked()) return false;

    const uint32_t old_write_addr = write_addr_;

    // 1フレームがセクターを跨ぐ場合は
    if(len > FlashDriver::SECTOR_SIZE){
        printf("!!! %s:append too large len=%u\n", __func__, static_cast<unsigned>(len));
        return false;
    }

    uint32_t addr = wrapAddress(write_addr_);

    // このログが現在のsectorを跨ぐなら次のsectorへ
    uint32_t sector_end = currentSectorEnd(addr);
    if(addr + len > sector_end){
        addr = nextSectorAddress(addr);
        addr = wrapAddress(addr);
    }

/*      sector先頭から書く場合は、そのsectorを丸ごとeraseする。
        eraseの前に、そのsector内の既存フレームをindexから除外する*/
    if(isSectorStart(addr)){
        invalidateFramesInErasedSector(addr);
        if(!flash_.sectorErase(addr)){
            printf("!!! %s:sectorErase failed addr=0x%08X\n", __func__, addr);
            rebuildIndexFromFlash();
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
            write_addr_ = old_write_addr;
            rebuildIndexFromFlash();
            return false;
        }

        if(!flash_.pageProgram(addr, p, chunk)){
            printf("!!! %s: pageProgram failed addr=0x%08X chunk=%u\n", __func__, addr, static_cast<unsigned>(chunk));
            write_addr_ = old_write_addr;
            rebuildIndexFromFlash();
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
        write_addr_ = old_write_addr; //失敗時もとに戻す
        rebuildIndexFromFlash();
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

    MutexGuard guard(*this);
    if(!guard.locked()) return false;

    if(!isRangeValid(addr, len)) return false;

    return flash_.read(addr, out, len);
} 

bool FlashLogStorage::restoreWriteAddress(){
    if(!initialized_) return false;

    MutexGuard guard(*this);
    if(!guard.locked()) return false;

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
                /* header.seq がnewest_seqより新しければnewestを更新*/
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
    // write_addr_ の位置が汚れている場合は、次の sector へ逃がす
    if(!isErasedAt(write_addr_)){
        uint32_t safe_addr = nextSectorAddress(write_addr_);
        write_addr_ = wrapAddress(safe_addr);

#ifdef DEBUG_FlashLogStorage
        printf("rebuildIndex: dirty tail detected, move write_addr to 0x%08X\n",
               write_addr_);
#endif
        }
    }else{
        oldest_addr_ = config_.start_addr;

        newest_addr_ = config_.start_addr;
        newest_seq_ = 0;
        write_addr_ = config_.start_addr;
        valid_frame_count_ = 0;
    if(!isErasedAt(write_addr_)){
        uint32_t safe_addr = nextSectorAddress(write_addr_);
        write_addr_ = wrapAddress(safe_addr);

#ifdef DEBUG_FlashLogStorage
        printf("rebuildIndex: dirty start detected, move write_addr to 0x%08X\n",
               write_addr_);
#endif
        }
    }
#ifdef DEBUG_FlashLogStorage
    printf("rebuildIndex: found=%d count=%u oldest=0x%08X newest=0x%08X newest_seq=%u write=0x%08X\n",
           found ? 1 : 0,
           static_cast<unsigned>(valid_frame_count_),
           oldest_addr_,
           newest_addr_,
           newest_seq_,
           write_addr_);
#endif
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
#ifdef DEBUG_FlashLogStorage
    printf("\n=== dumpFramesOldestFirst ===\n");
    printf("oldest=0x%08X newest=0x%08X newest_seq=%u count=%u\n", oldest_addr_, newest_addr_, newest_seq_,
           static_cast<unsigned>(valid_frame_count_));
#endif
    uint32_t printed = 0;
    uint32_t scanned = 0;
    const uint32_t area_size = config_.end_addr - config_.start_addr;
    
    while(scanned < area_size){
        LogFrameHeader header{};
        size_t frame_size = 0;

        if(isValidFrameAt(addr, &header, &frame_size)){
#ifdef DEBUG_FlashLogStorage
            printf("frame: addr=0x%08X seq=%u event_id=%u level=%u len=%u\n",
                   addr, header.seq, header.event_id, header.level, header.length);
#endif
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
#ifdef DEBUG_FlashLogStorage
    printf("dumpFramesOldestFirst: printed=%u\n", static_cast<unsigned>(printed));
    printf("=== dumpFramesOldestFirst done ===\n");
#endif
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
#ifdef DEBUG_FlashLogStorage
        printf("!!! crc mismatch at 0x%08X\n", addr);
#endif
        return false;
    }

    *out_header = header;
    *out_frame_size = frame_size;
#ifdef DEBUG_FlashLogStorage
    printf("valid frame at 0x%08X size=%u\n", addr, static_cast<unsigned>(frame_size));
#endif
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
    return flash_.read(addr + LOG_FRAME_MAGIC_SIZE, reinterpret_cast<uint8_t*>(out_header), sizeof(LogFrameHeader));
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

bool FlashLogStorage::readFrame(uint32_t addr, uint8_t* out, size_t out_size, size_t* out_len){
    if(out == nullptr || out_len == nullptr){
        printf("!!! %s: null arg\n", __func__);
        return false;
    }
    
    MutexGuard guard(*this);
    if(!guard.locked()) return false;

    *out_len = 0;
    LogFrameHeader header{};
    size_t frame_size = 0;
    if(!isValidFrameAt(addr, &header, &frame_size)){
        return false;
    }
    if(frame_size > out_size){
        printf("!!! %s: buffer too small frame_size=%u out_size=%u\n",
               __func__,
               static_cast<unsigned>(frame_size),
               static_cast<unsigned>(out_size));
        return false;
    }
    if(!flash_.read(addr, out, frame_size)){
        printf("!!! %s: flash read failed addr=0x%08X size=%u\n", __func__, addr, static_cast<unsigned>(frame_size));
        return false;
    }
    *out_len = frame_size;
#ifdef DEBUG_FlashLogStorage
    printf("%s: OK addr=0x%08X seq=%u frame_size=%u\n", __func__, addr, header.seq, static_cast<unsigned>(frame_size));
#endif
    return true;
}
/* テスト用の関数 */
bool FlashLogStorage::readFramesOldestFirstTest(){
    if(!initialized_) return false;
    if(valid_frame_count_ == 0){
        printf("readFramesOldestFirstTest: no valid frame\n");
        return true;
    }
    uint8_t frame_buf[LOG_FRAME_MAX_SIZE];
    uint32_t addr = oldest_addr_;
    uint32_t read_count = 0;
    uint32_t scanned = 0;
    const uint32_t area_size = config_.end_addr - config_.start_addr;
#ifdef DEBUG_FlashLogStorage
    printf("\n=== readFramesOldestFirstTest ===\n");
#endif
    while(scanned < area_size){
        LogFrameHeader header{};
        size_t frame_size = 0;
        if(isValidFrameAt(addr, &header, &frame_size)){
            size_t read_len = 0;
            if(!readFrame(addr, frame_buf, sizeof(frame_buf), &read_len)){
                printf("!!! readFrame failed addr=0x%08X\n", addr);
                return false;
            }
#ifdef DEBUG_FlashLogStorage
            printf("read frame: addr=0x%08X seq=%u size=%u len=%u\n", addr, header.seq, static_cast<unsigned>(read_len), header.length);
#endif
            read_count++;
            if(header.seq == newest_seq_){
                break;
            }
#ifdef DEBUG_FlashLogStorage
            printf("addr before=0x%08X next=0x%08X\n",  addr, wrapAddress(addr + static_cast<uint32_t>(read_len)));
#endif
            addr = wrapAddress(addr + static_cast<uint32_t>(read_len));
            scanned += static_cast<uint32_t>(read_len);
        } else {
            addr = wrapAddress(addr + 1u);
            scanned++;
        }
    }
#ifdef DEBUG_FlashLogStorage
    printf("readFramesOldestFirstTest: read_count=%u\n", static_cast<unsigned>(read_count));
    printf("=== readFrameOldestFirstTest done ===\n");
#endif
    return true;
}

bool FlashLogStorage::sendFramesOldestFirst(UartDma& uart){
    if(!initialized_) return false;

    MutexGuard guard(*this);
    if(!guard.locked()) return false;

    if(valid_frame_count_==0){
        printf("sendFramesOldestFirst: no valid frame\n");
        return true;
    }
    uint8_t frame_buf[LOG_FRAME_MAX_SIZE];
    uint32_t addr = oldest_addr_;
    uint32_t sent_count = 0;
    uint32_t scanned = 0;

    const uint32_t area_size = config_.end_addr - config_.start_addr;
#ifdef DEBUG_FlashLogStorage
    printf("\n=== sendFramesOldestFirst ===\n");
#endif
    while(scanned < area_size){
        LogFrameHeader header{};
        size_t frame_size = 0;
        if(isValidFrameAt(addr, &header, &frame_size)){
            size_t read_len = 0;
            if(!readFrame(addr, frame_buf, sizeof(frame_buf), &read_len)){
                printf("!!! %s: readFrame failed addr=0x%08X\n", __func__, addr);
                return false;
            }
            uart.write_buffer_blocking(frame_buf, read_len);
            sent_count++;
            if(header.seq == newest_seq_){
                break;
            }
            addr = wrapAddress(addr + static_cast<uint32_t>(read_len));
            scanned += static_cast<uint32_t>(read_len);
        }else{
            addr = wrapAddress(addr + 1u);
            scanned++;
        }
    }
#ifdef DEBUG_FlashLogStorage
    printf("sendFramesOldestFirst: send_count=%u\n", static_cast<unsigned>(sent_count));
    printf("=== sendFramesOldestFirst done ===\n");
#endif
    return true;
}

bool FlashLogStorage::sendLatestFrames(UartDma& uart, uint32_t count){
    if(!initialized_) return false;
    
    MutexGuard guard(*this);
    if(!guard.locked()) return false;

    if(count == 0){
        printf("sendLatestFrames: count is 0\n");
        return true;
    }
    if(valid_frame_count_ == 0){
        printf("sendLatestFrames: no valid frame\n");
        return true;
    }
    /* 保存されている件数より多く要求された場合は、全件送信する */
    uint32_t target_count = count;
    if(target_count > valid_frame_count_){
        target_count = valid_frame_count_;
    }
    /* 最新N件を出すには、古い順に見たときに先頭側をskipする */
    const uint32_t skip_count = valid_frame_count_ - target_count;
    uint8_t frame_buf[LOG_FRAME_MAX_SIZE];
    uint32_t addr = oldest_addr_;
    uint32_t scanned = 0;
    uint32_t valid_seen = 0;
    uint32_t sent_count = 0;

    const uint32_t area_size = config_.end_addr - config_.start_addr;

#ifdef DEBUG_FlashLogStorage
    printf("\n=== sendLatestFrames ===\n");
    printf("request=%u target=%u valid=%u skip=%u oldest=0x%08X newest=0x%08X newest_seq=%u\n",
           static_cast<unsigned>(count),
           static_cast<unsigned>(target_count),
           static_cast<unsigned>(valid_frame_count_),
           static_cast<unsigned>(skip_count),
           oldest_addr_,
           newest_addr_,
           newest_seq_);
#endif
    while(scanned < area_size){
        LogFrameHeader header{};
        size_t frame_size = 0;

        if(isValidFrameAt(addr, &header, &frame_size)){
            size_t read_len = 0;
            if(!readFrame(addr, frame_buf, sizeof(frame_buf), &read_len)){
                printf("!!! %s: readFrame failed addr=0x%08X\n", __func__, addr);
                return false;
            }
            /* skip_count まで読み飛ばす。それ以降UARTへ送る */
            if(valid_seen >= skip_count){
                uart.write_buffer_blocking(frame_buf, read_len);
                sent_count++;
            }
            valid_seen++;
            if(header.seq == newest_seq_){
                break;
            }
            addr = wrapAddress(addr + static_cast<uint32_t>(read_len));
            scanned += static_cast<uint32_t>(read_len);
        } else {
            addr = wrapAddress(addr + 1u);
            scanned++;
        }
    }
#ifdef DEBUG_FlashLogStorage
    printf("sendLatestFrames: sent_count=%u valid_seen=%u\n",
           static_cast<unsigned>(sent_count),
           static_cast<unsigned>(valid_seen));

    if(sent_count != target_count){
        printf("!!! %s: sent_count mismatch target=%u sent=%u valid_seen=%u\n",
            __func__,
            static_cast<unsigned>(target_count),
            static_cast<unsigned>(sent_count),
            static_cast<unsigned>(valid_seen));
    }
    printf("=== sendLatestFrames done ===\n");
#endif
    return true;
}

uint32_t FlashLogStorage::getNewestSeq() const {
    return newest_seq_;
}

uint32_t FlashLogStorage::getCount() const {
    return valid_frame_count_;
}

uint32_t FlashLogStorage::getWriteAddressForTest() const {
    return write_addr_;
}

bool FlashLogStorage::isErasedAt(uint32_t addr) const {
    if(addr < config_.start_addr || addr >= config_.end_addr){
        return false;
    }
    uint8_t b = 0x00;
    if(!flash_.read(addr, &b, 1)){
        return false;
    }
    return b == 0xFF;
}

void FlashLogStorage::printStatus(const char* title)
{
    printf("\n=== %s ===\n", title);
    printf("start   = 0x%08lX\n", static_cast<unsigned long>(getStartAddress()));
    printf("end     = 0x%08lX\n", static_cast<unsigned long>(getEndAddress()));
    printf("write   = 0x%08lX\n", static_cast<unsigned long>(getWriteAddress()));
    printf("oldest  = 0x%08lX\n", static_cast<unsigned long>(getOldestAddress()));
    printf("newest  = 0x%08lX\n", static_cast<unsigned long>(getNewestAddress()));
    printf("seq     = %lu\n",     static_cast<unsigned long>(getNewestSeq()));
    printf("count   = %lu\n",     static_cast<unsigned long>(getCount()));
    printf("remain  = %u\n",      static_cast<unsigned>(getRemainingSize()));
    printf("==============================\n");
}
