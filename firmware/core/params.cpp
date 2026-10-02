#include "params.h"
#include <string.h>

namespace c5rx {

uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            uint32_t mask = -(int32_t)(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// Layout, little-endian: magic (4 bytes), version (2), then each field in
// order, then a CRC32 of everything before it (4). Fields are written one by
// one rather than copying the struct, so padding can't change the format.
namespace {
void put32(uint8_t*& p, uint32_t v) { memcpy(p, &v, 4); p += 4; }
void put16(uint8_t*& p, uint16_t v) { memcpy(p, &v, 2); p += 2; }
void put8 (uint8_t*& p, uint8_t v)  { *p++ = v; }
void putf (uint8_t*& p, float v)    { memcpy(p, &v, 4); p += 4; }

uint32_t get32(const uint8_t*& p){ uint32_t v; memcpy(&v,p,4); p+=4; return v; }
uint16_t get16(const uint8_t*& p){ uint16_t v; memcpy(&v,p,2); p+=2; return v; }
uint8_t  get8 (const uint8_t*& p){ return *p++; }
float    getf (const uint8_t*& p){ float v; memcpy(&v,p,4); p+=4; return v; }
} // namespace

size_t serializedSize() {
    return 4 + 2
         + 4*3 /*dbLo,dbHi,emaAlpha*/ + 1 /*median*/ + 2 /*settle*/ + 2 /*stall*/
         + 4*4 /*vdd,ratio,fs,floor*/ + 2 /*bootMhz*/
         + 4*2 /*kneeDb,kneeRatio*/
         + 4; /*crc*/
}

size_t serializeConfig(const PersistConfig& c, uint8_t* buf, size_t cap) {
    size_t need = serializedSize();
    if (cap < need) return 0;
    uint8_t* p = buf;
    put32(p, kParamMagic);
    put16(p, kParamVersion);
    putf(p, c.dbLo); putf(p, c.dbHi); putf(p, c.emaAlpha);
    put8(p, c.useMedian3); put16(p, c.settleMs); put16(p, c.stallMs);
    putf(p, c.vdd); putf(p, c.dividerRatio); putf(p, c.vFullScale); putf(p, c.vFloor);
    put16(p, c.bootMhz);
    putf(p, c.kneeDb); putf(p, c.kneeRatio);
    uint32_t crc = crc32(buf, (size_t)(p - buf));
    put32(p, crc);
    return (size_t)(p - buf);
}

bool deserializeConfig(PersistConfig& out, const uint8_t* buf, size_t len) {
    size_t need = serializedSize();
    if (len < need) return false;
    const uint8_t* p = buf;
    if (get32(p) != kParamMagic) return false;
    if (get16(p) != kParamVersion) return false;
    PersistConfig c;
    c.dbLo = getf(p); c.dbHi = getf(p); c.emaAlpha = getf(p);
    c.useMedian3 = get8(p); c.settleMs = get16(p); c.stallMs = get16(p);
    c.vdd = getf(p); c.dividerRatio = getf(p); c.vFullScale = getf(p); c.vFloor = getf(p);
    c.bootMhz = get16(p);
    c.kneeDb = getf(p); c.kneeRatio = getf(p);
    uint32_t stored = get32(p);
    uint32_t calc = crc32(buf, need - 4);
    if (stored != calc) return false;
    // Reject values that would make no sense, even if the CRC is good.
    if (!(c.dbHi > c.dbLo)) return false;
    if (c.emaAlpha <= 0.0f || c.emaAlpha > 1.0f) return false;
    if (c.dividerRatio <= 0.0f || c.dividerRatio > 1.0f) return false;
    if (c.bootMhz != 0 && (c.bootMhz < 5180 || c.bootMhz > 5885)) return false;
    if (!(c.kneeRatio >= 1.0f && c.kneeRatio <= 50.0f)) return false;
    out = c;
    return true;
}

} // namespace c5rx
