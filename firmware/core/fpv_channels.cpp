#include "fpv_channels.h"

namespace c5rx {

// Standard 5.8 GHz FPV channels, as used by FPV video gear.
static const FpvChannel kChannels[] = {
    // Band A (Boscam A)
    {'A',1,5865},{'A',2,5845},{'A',3,5825},{'A',4,5805},{'A',5,5785},{'A',6,5765},{'A',7,5745},{'A',8,5725},
    // Band B (Boscam B)
    {'B',1,5733},{'B',2,5752},{'B',3,5771},{'B',4,5790},{'B',5,5809},{'B',6,5828},{'B',7,5847},{'B',8,5866},
    // Band E (Boscam E)
    {'E',1,5705},{'E',2,5685},{'E',3,5665},{'E',4,5645},{'E',5,5885},{'E',6,5905},{'E',7,5925},{'E',8,5945},
    // Band F (FatShark / IRC)
    {'F',1,5740},{'F',2,5760},{'F',3,5780},{'F',4,5800},{'F',5,5820},{'F',6,5840},{'F',7,5860},{'F',8,5880},
    // Band R (Raceband)
    {'R',1,5658},{'R',2,5695},{'R',3,5732},{'R',4,5769},{'R',5,5806},{'R',6,5843},{'R',7,5880},{'R',8,5917},
    // Band L (Low race). Below the main band. L5 to L8 are reachable but untested.
    {'L',1,5333},{'L',2,5373},{'L',3,5413},{'L',4,5453},{'L',5,5493},{'L',6,5533},{'L',7,5573},{'L',8,5613},
};
static const int kChannelCount = (int)(sizeof(kChannels) / sizeof(kChannels[0]));

const FpvChannel* fpvChannelTable(int& countOut) {
    countOut = kChannelCount;
    return kChannels;
}

const FpvChannel* snapToChannel(uint16_t mhz, uint16_t tolMhz) {
    const FpvChannel* best = nullptr;
    uint16_t bestDiff = 0xFFFF;
    for (int i = 0; i < kChannelCount; ++i) {
        uint16_t d = (kChannels[i].mhz > mhz) ? (kChannels[i].mhz - mhz)
                                              : (mhz - kChannels[i].mhz);
        if (d <= tolMhz && d < bestDiff) {
            bestDiff = d;
            best = &kChannels[i];
        }
    }
    return best;
}

uint16_t normaliseFrequency(uint16_t rawMhz) {
    const FpvChannel* c = snapToChannel(rawMhz, 2);
    return c ? c->mhz : rawMhz;
}

} // namespace c5rx
