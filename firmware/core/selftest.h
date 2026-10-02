// A quick software self-test, run at boot. Each check sets one bit in the
// result; 0x1F means everything passed.
#ifndef C5RX_SELFTEST_H
#define C5RX_SELFTEST_H

#include <stdint.h>

namespace c5rx {

enum SelfTestBit : uint16_t {
    ST_FREQ_MATHS   = 1 << 0,   // frequency maths and channel lookup
    ST_RANGE        = 1 << 1,   // R8 and E6-E8 are outside the C5's range
    ST_PIPELINE     = 1 << 2,   // RSSI rises with signal and reaches 0 and 255
    ST_CODEC        = 1 << 3,   // analog output rises with RSSI
    ST_PARAMS       = 1 << 4,   // settings save, load and corruption check
    ST_ALL          = ST_FREQ_MATHS | ST_RANGE | ST_PIPELINE | ST_CODEC | ST_PARAMS,
};

struct SelfTestResult {
    uint16_t passedBits = 0;
    bool pass() const { return passedBits == ST_ALL; }
};

// Run every check. Touches no hardware.
SelfTestResult runSelfTest();

} // namespace c5rx

#endif // C5RX_SELFTEST_H
