#ifndef CRC32_H
#define CRC32_H
/* ---------------------------------------------------- */
#include <stddef.h>
#include <stdint.h>

class Crc32 {
public:
    static uint32_t calculate(const uint8_t* data, size_t len);
    static uint32_t calculate2(const uint8_t* data1, size_t len1, const uint8_t* data2, size_t len2);

};
/* ---------------------------------------------------- */
#endif // CRC32_H