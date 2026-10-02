#include "c5_freq_map.h"

namespace c5rx {

// The 5 GHz Wi-Fi channels the ESP32-C5 supports, matching the WIFI_CHANNEL_
// bits in Espressif's esp_wifi_types_generic.h. The top block runs 149, 153,
// ... 177, so those aren't multiples of 4 like the others.
static const uint8_t kChannels5g[] = {
    36, 40, 44, 48, 52, 56, 60, 64,
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
    149, 153, 157, 161, 165, 169, 173, 177,
};
static const int kNumChannels5g = (int)(sizeof(kChannels5g) / sizeof(kChannels5g[0]));

int c5ChannelCount() { return kNumChannels5g; }
uint8_t c5ChannelAt(int i) { return (i >= 0 && i < kNumChannels5g) ? kChannels5g[i] : 0; }

C5Tuning planTuning(uint16_t mhz, int maxOffsetMhz) {
    C5Tuning t;
    int bestCh = kChannels5g[0];
    int bestDiff = 0x7FFF;
    for (int i = 0; i < kNumChannels5g; ++i) {
        int center = 5000 + 5 * kChannels5g[i];
        int d = (int)mhz - center;
        if (d < 0) d = -d;
        if (d < bestDiff) { bestDiff = d; bestCh = kChannels5g[i]; }
    }
    int center = 5000 + 5 * bestCh;
    int offset = (int)mhz - center;

    t.baseChannel = (uint16_t)bestCh;
    t.centerMhz   = (uint16_t)center;
    t.offsetMhz   = (int16_t)offset;
    t.ok = (offset <= maxOffsetMhz) && (offset >= -maxOffsetMhz);
    return t;
}

} // namespace c5rx
