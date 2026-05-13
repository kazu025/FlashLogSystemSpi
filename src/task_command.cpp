#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include "CommandTask.h"

static std::atomic<bool> g_log_paused{true};

bool isLogPaused(){
    return g_log_paused.load(std::memory_order_relaxed);
}
void pauseLogGeneration(){
    g_log_paused.store(true, std::memory_order_relaxed);
}
void resumeLogGeneration(){
    g_log_paused.store(false, std::memory_order_relaxed);
}
static void printHelp(){
    printf("\n");
    printf("=== Flash Log Command ===\n");
    printf("h       : help\n");
    printf("s       : show status\n");
    printf("p       : pause log generation\n");
    printf("r       : resume log generation\n");
    printf("a       : send all frames oldest-first\n");
    printf("lN      : send latest N frames. example: l10, l100\n");
    printf("e       : erase log area\n");
    printf("=========================\n");
}

static void printStatus(FlashLogStorage& storage){
    printf("\n");
    printf("=== FlashLogStorage Status ===\n");
    printf("start   = 0x%08X\n", storage.getStartAddress());
    printf("end     = 0x%08X\n", storage.getEndAddress());
    printf("write   = 0x%08X\n", storage.getWriteAddress());
    printf("oldest  = 0x%08X\n", storage.getOldestAddress());
    printf("newest  = 0x%08X\n", storage.getNewestAddress());
    printf("count   = %u\n", static_cast<unsigned>(storage.getValidFrameCount()));
    printf("remain  = %u\n", static_cast<unsigned>(storage.getRemainingSize()));
    printf("Paused  = %s\n", isLogPaused() ? "yes": "no");
    printf("==============================\n");
}

static const char* skipSpaces(const char* p){
    while(*p == ' ' || *p == '\t') p++;
    return p;
}

static bool parseLatestCount(const char* cmd, uint32_t* out_count){
    if(cmd == nullptr || out_count == nullptr) return false;
    if(cmd[0] != 'l' && cmd[0] != 'L') return false;
    const char* p = skipSpaces(cmd + 1);
    if(*p < '0' || *p > '9') return false;
    char* endp = nullptr;
    unsigned long value = strtoul(p, &endp, 10);
    if(endp == p) return false;
    if(value == 0) return false;
    if(value > 100000UL) value = 100000UL;
    *out_count = static_cast<uint32_t>(value);
    return true;
}

void command_task(void *pvParameters){
    auto* ctx = static_cast<AppContext*>(pvParameters);

    if(ctx == nullptr || ctx->storage == nullptr || ctx->uart == nullptr){
        printf("!!! command_task: invalid context\n");
        vTaskDelete(nullptr);
        return;
    }
    FlashLogStorage& storage = *ctx->storage;
    UartDma& uart = *ctx->uart;
    EventLogger& logger = *ctx->logger;

    static constexpr size_t CMD_BUF_SIZE = 32;
    char cmd_buf[CMD_BUF_SIZE];
    size_t pos = 0;

    printf("\nFlashLog command ready.\n");
    printHelp();
    printf("\ncmd> ");

    while(true){
        int ch = getchar_timeout_us(0);
        if(ch == PICO_ERROR_TIMEOUT){
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if(ch == '\r' || ch == '\n'){
            cmd_buf[pos] = '\0';
            if(pos > 0){
                printf("\n");
                uint32_t latest_count = 0;
                if(strcmp(cmd_buf, "h") == 0 || strcmp(cmd_buf, "H") == 0){
                    printHelp();
                }else if(strcmp(cmd_buf, "s") == 0 || strcmp(cmd_buf, "S") == 0) {
                    printStatus(storage);
                }else if (strcmp(cmd_buf, "p") == 0 || strcmp(cmd_buf, "P") == 0) {
                    pauseLogGeneration();
                    printf("log generation paused.\n");
                }else if (strcmp(cmd_buf, "r") == 0 || strcmp(cmd_buf, "R") == 0) {
                    resumeLogGeneration();
                    printf("log generation resumed.\n");
                }else if(strcmp(cmd_buf, "a") == 0 || strcmp(cmd_buf, "A") == 0) {
                    printf("send all frames oldest-first...\n");
                    storage.sendFramesOldestFirst(uart);
                    printf("send all done.\n");
                }else if(strcmp(cmd_buf, "e") == 0 || strcmp(cmd_buf, "E") == 0){
                    printf("erase log area...\n");
                    if(storage.eraseLogArea()){
                        logger.setNextSeq(0); 
                        printf("erase done.\n");
                    } else {
                        printf("!!! erase failed.\n");
                    }
                }else if (parseLatestCount(cmd_buf, &latest_count)) {
                    printf("send latest %u frames...\n", static_cast<unsigned>(latest_count));
                    storage.sendLatestFrames(uart, latest_count);
                    printf("send latest done.\n");
                }
                else {
                    printf("unknown command: %s\n", cmd_buf);
                    printHelp();
                }
            }
            pos = 0;
            printf("\ncmd >");
            continue;
        }
        if(ch == 0x08 || ch == 0x7F){
            if(pos > 0){
                pos--;
                printf("\b \b");
            }
            continue;
        }
        if(pos < CMD_BUF_SIZE - 1){
            cmd_buf[pos++] = static_cast<char>(ch);
            putchar(ch);
        } else {
            pos = 0;
            printf("\n!!! command too long\n");
            printf("\ncmd> ");
        }
    }
}




