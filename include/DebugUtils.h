#pragma once
#include <stddef.h>
#include <stdint.h>

void dump_hex(const uint8_t* data, size_t len);
bool compare_buffers(const uint8_t* a, const uint8_t* b, size_t len);
