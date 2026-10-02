// The standard 5.8 GHz FPV channel plan, and helpers for looking channels up.
#ifndef C5RX_FPV_CHANNELS_H
#define C5RX_FPV_CHANNELS_H

#include <stdint.h>
#include "c5rx_types.h"

namespace c5rx {

struct FpvChannel {
    char     band;    // 'A', 'B', 'E', 'F', 'R' or 'L'
    uint8_t  index;   // 1 to 8
    uint16_t mhz;
};

// All 48 standard channels.
const FpvChannel* fpvChannelTable(int& countOut);

// The standard channel closest to `mhz`, if one is within `tolMhz`, else null.
const FpvChannel* snapToChannel(uint16_t mhz, uint16_t tolMhz = 2);

// True if the C5 can receive `mhz`.
inline bool c5InRange(uint16_t mhz) {
    return mhz >= kC5MinMhz && mhz <= kC5MaxMhz;
}

// The frequency to actually tune for a decoded RX5808 value: the nearest
// standard channel if it's within 2 MHz, otherwise the value unchanged.
uint16_t normaliseFrequency(uint16_t rawMhz);

} // namespace c5rx

#endif // C5RX_FPV_CHANNELS_H
