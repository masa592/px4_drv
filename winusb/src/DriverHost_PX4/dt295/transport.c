#include "transport.h"

uint32_t ts_crc(const uint8_t *p, size_t n) {
    uint32_t crc = 0xffffffff;
    while (n--) {
        crc ^= (uint32_t)*p++ << 24;
        for (unsigned i = 0; i < 8; i++)
            crc = (crc << 1) ^ ((crc & 0x80000000) ? 0x04c11db7 : 0);
    }
    return crc;
}
