#include "rx5808_decode.h"

namespace c5rx {

Rx5808Word parseWord(uint32_t bits25) {
    Rx5808Word w;
    w.address = (uint8_t)(bits25 & 0x0F);         // bits [0:3]
    w.write   = (bits25 >> 4) & 0x1;              // bit  [4]
    w.data    = (bits25 >> 5) & 0xFFFFF;          // bits [5:24], 20 bits
    w.valid   = true;
    return w;
}

uint32_t buildWord(uint8_t address, bool write, uint32_t data20) {
    uint32_t w = (uint32_t)(address & 0x0F);
    w |= (uint32_t)(write ? 1u : 0u) << 4;
    w |= (uint32_t)(data20 & 0xFFFFF) << 5;
    return w & 0x1FFFFFF; // 25 bits
}

// FPVGate's freqMhzToRegVal() computes
//   tf = (f - 479) / 2;  N = tf / 32;  A = tf % 32;  reg = (N << 7) + A
// so going back:
//   N = reg >> 7;  A = reg & 0x7F;  tf = N * 32 + A;  f = tf * 2 + 479
uint16_t synthRegToMhz(uint16_t reg) {
    uint16_t N  = reg >> 7;
    uint16_t A  = reg & 0x7F;
    uint32_t tf = (uint32_t)N * 32u + A;
    return (uint16_t)(tf * 2u + 479u);
}

uint16_t mhzToSynthReg(uint16_t mhz) {
    uint16_t tf = (uint16_t)((mhz - 479) / 2);
    uint16_t N  = tf / 32;
    uint16_t A  = tf % 32;
    return (uint16_t)((N << 7) + A);
}

} // namespace c5rx
