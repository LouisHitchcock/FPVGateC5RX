// Shared types and constants.
#ifndef C5RX_TYPES_H
#define C5RX_TYPES_H

#include <stdint.h>

namespace c5rx {

// The ESP32-C5's 5 GHz receive range, from the datasheet.
static const uint16_t kC5MinMhz = 5180;
static const uint16_t kC5MaxMhz = 5885;

// One RX5808 bus word, split into its fields.
struct Rx5808Word {
    uint8_t  address;   // 0 to 15
    bool     write;     // true = write, false = read
    uint32_t data;      // 20 bits (D0 to D19)
    bool     valid;
};

// RX5808 registers the firmware handles.
enum Rx5808Reg : uint8_t {
    REG_SYNTH_A   = 0x0,
    REG_SYNTH_RF  = 0x1,  // frequency (N/A synth word)
    REG_POWER     = 0xA,  // power-down and feature bits
    REG_STATE     = 0xF,  // reset
};

} // namespace c5rx

#endif // C5RX_TYPES_H
