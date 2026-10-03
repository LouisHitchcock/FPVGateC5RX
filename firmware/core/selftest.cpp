#include "selftest.h"
#include "rx5808_decode.h"
#include "fpv_channels.h"
#include "rssi_pipeline.h"
#include "rssi_codec.h"
#include "params.h"

namespace c5rx {

static bool checkFreqMaths() {
    // A channel survives the trip to a register value and back.
    const uint16_t fs[] = {5658, 5800, 5880, 5733};
    for (uint16_t f : fs) {
        uint16_t back = synthRegToMhz(mhzToSynthReg(f));
        if (back > f + 1 || back + 1 < f) return false;
        if (snapToChannel(normaliseFrequency(back), 0) == nullptr) return false;
    }
    return true;
}

static bool checkRange() {
    return c5InRange(5180) && c5InRange(5885) &&
           !c5InRange(5917) /*R8*/ && !c5InRange(5945) /*E8*/;
}

static bool checkPipeline() {
    RssiPipeline p; RssiPipelineConfig c;
    c.dbLo = -95; c.dbHi = -35; c.emaAlpha = 1.0f; c.useMedian3 = false;
    c.settleMs = 0; c.windowMaxMs = 0; c.preMin = 1;   // test the mapping on its own
    p.begin(c);
    p.onSample(-95, 1); if (p.counts() != 0) return false;
    p.onSample(-35, 2); if (p.counts() != 255) return false;
    // A rising signal never makes the RSSI go down.
    uint8_t prev = 0;
    for (int i = 0; i <= 60; ++i) {
        float db = -95.0f + i;
        p.onSample(db, 10 + i);
        if (p.counts() < prev) return false;
        prev = p.counts();
    }
    return true;
}

static bool checkCodec() {
    RssiCodec cc; RssiCodecConfig cfg; cc.begin(cfg);
    if (cc.densityForCounts(0) != -128) return false;
    int prev = -200;
    for (int n = 0; n <= 255; ++n) {
        int d = cc.densityForCounts((uint8_t)n);
        if (d < prev) return false;
        prev = d;
    }
    return cc.pinVoltageForCounts(255) < 1.65f;
}

static bool checkParams() {
    PersistConfig c; c.dbLo = -90; c.dbHi = -30;
    uint8_t buf[64];
    size_t n = serializeConfig(c, buf, sizeof(buf));
    if (!n) return false;
    PersistConfig o;
    if (!deserializeConfig(o, buf, n)) return false;
    buf[6] ^= 0xFF;                           // damaged data must be rejected
    return !deserializeConfig(o, buf, n);
}

SelfTestResult runSelfTest() {
    SelfTestResult r;
    if (checkFreqMaths()) r.passedBits |= ST_FREQ_MATHS;
    if (checkRange())     r.passedBits |= ST_RANGE;
    if (checkPipeline())  r.passedBits |= ST_PIPELINE;
    if (checkCodec())     r.passedBits |= ST_CODEC;
    if (checkParams())    r.passedBits |= ST_PARAMS;
    return r;
}

} // namespace c5rx
