#include "host_regs.h"

namespace c5rx {

static int8_t clampDbToI8(float db) {
    if (db < -128.0f) return -128;
    if (db >  127.0f) return  127;
    return (int8_t)(db < 0 ? (int)(db - 0.5f) : (int)(db + 0.5f));
}

uint32_t buildReadData(const Controller& c, uint8_t addr) {
    switch (addr) {
        case REG_SYNTH_RF:
            // The last frequency value written.
            return c.synthReadback() & 0xFFFF;

        case REG_EXT_STATUS: {
            uint32_t d = 0;
            d |= (uint32_t)c.rssiCounts() & 0xFF;          // D0-7
            d |= (c.rssiValid()      ? 1u : 0u) << 8;      // D8
            d |= (c.freqSupported()  ? 1u : 0u) << 9;      // D9
            d |= ((uint32_t)((uint8_t)c.state()) & 0xF) << 11; // D11-14
            d |= 1u << 15;                                  // D15 always 1
            return d & 0xFFFFF;
        }

        case REG_EXT_INFO: {
            uint32_t d = 0;
            d |= (uint8_t)clampDbToI8(c.rssiDb());          // D0-7 signed
            d |= (uint32_t)kC5rxSignature << 8;             // D8-15 signature
            return d & 0xFFFFF;
        }

        default:
            return 0;
    }
}

bool looksLikeC5rx(uint32_t infoRegData, uint32_t statusRegData) {
    bool sig    = ((infoRegData >> 8) & 0xFF) == kC5rxSignature;
    bool alive  = (statusRegData >> 15) & 0x1;
    return sig && alive;
}

} // namespace c5rx
