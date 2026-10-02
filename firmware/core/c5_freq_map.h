// Picks the 5 GHz Wi-Fi channel to tune for an FPV frequency.
#ifndef C5RX_C5_FREQ_MAP_H
#define C5RX_C5_FREQ_MAP_H

#include <stdint.h>

namespace c5rx {

struct C5Tuning {
    uint16_t baseChannel;  // 5 GHz Wi-Fi channel number
    uint16_t centerMhz;    // 5000 + 5 * baseChannel
    int16_t  offsetMhz;    // how far the FPV frequency is from the centre
    bool     ok;           // false if no channel is close enough
};

// The nearest 5 GHz Wi-Fi channel to `mhz` (channels 36-64, 100-144 and
// 149-177). `ok` is false if the FPV signal would be more than maxOffsetMhz
// from the channel centre, which puts it outside the 20 MHz receive
// bandwidth. L1 to L4 fall in the gap between 5320 and 5500 MHz, for example.
C5Tuning planTuning(uint16_t mhz, int maxOffsetMhz = 12);

// The list of supported 5 GHz channels.
int     c5ChannelCount();
uint8_t c5ChannelAt(int i);

} // namespace c5rx

#endif // C5RX_C5_FREQ_MAP_H
