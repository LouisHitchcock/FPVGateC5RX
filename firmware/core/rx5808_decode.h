// Converting between RX5808 bus words and frequencies.
//
// The register format comes from the RX5808 datasheet and FPVGate's own
// RX5808 driver.
#ifndef C5RX_RX5808_DECODE_H
#define C5RX_RX5808_DECODE_H

#include <stdint.h>
#include "c5rx_types.h"

namespace c5rx {

// Split a 25-bit word into its fields. Bit 0 (the first bit on the wire) is
// the least significant bit of `bits25`.
Rx5808Word parseWord(uint32_t bits25);

// Build the 25-bit word for a register access.
uint32_t buildWord(uint8_t address, bool write, uint32_t data20);

// RX5808 synth register value (D0 to D15 of register 0x1) to MHz.
uint16_t synthRegToMhz(uint16_t reg);

// MHz to synth register value, as FPVGate's freqMhzToRegVal() computes it.
uint16_t mhzToSynthReg(uint16_t mhz);

} // namespace c5rx

#endif // C5RX_RX5808_DECODE_H
