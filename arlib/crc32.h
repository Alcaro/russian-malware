#pragma once
#include "global.h"
#include "array.h"

//uses the standard 0xEDB88320 generator polynomial
uint32_t crc32(arrayview<uint8_t> data, uint32_t crc = 0);
