#include "Crc32.h"

uint32_t Crc32::calculate(const uint8_t* data, size_t len){
    uint32_t crc = 0xFFFFFFFF;
    for(size_t i = 0; i < len; i++){
        crc ^= static_cast<uint32_t>(data[i]);
        for(int j = 0; j < 8; j++){
            if(crc & 1u){
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}


uint32_t Crc32::calculate2(const uint8_t* data1, size_t len1, const uint8_t* data2, size_t len2){
    uint32_t crc = 0xFFFFFFFFu;
    for(size_t i=0; i < len1; i++){
        crc ^= static_cast<uint32_t>(data1[i]);
        for(int j = 0; j < 8; j++){
            if(crc & 1u){
                crc = (crc >> 1) ^ 0xEDB88320u;
            }else{
                crc >>= 1;
            }
        }
    }

    for(size_t i=0; i < len2; i++){
        crc ^= static_cast<uint32_t>(data2[i]);
        for(int j = 0; j < 8; j++){
            if(crc & 1u){
                crc = (crc >> 1) ^ 0xEDB88320u;
            }else{
                crc >>= 1;
            }
        }
    }
    return ~crc;
}