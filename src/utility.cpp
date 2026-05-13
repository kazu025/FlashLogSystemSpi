#include "utility.h"
#include <stdio.h>

void dump_hex(const uint8_t *data, size_t len){
    if(data == nullptr){
        printf("!!! hump_hex: (null)\n");
        return;
    }
    for(size_t i=0; i<len; i++)    printf("%02X%c", data[i], (i+1)%16 ? ' ':'\n');
    if(len%16) printf("\n");
}
bool compare_buffers(const uint8_t* a, const uint8_t* b, size_t len){
    if(a == nullptr || b == nullptr){
        printf("!!! compare_buffers: null pointer\n");
        return false;
    }
    for(size_t i = 0; i < len; i++){
        if(a[i] == b[i]) continue;
        printf("!!! compare_buffers: mismatch at %u [a=%02X b=%02X]\n", static_cast<unsigned>(i), a[i], b[i]);
        return false;
    }
    return true;
}
