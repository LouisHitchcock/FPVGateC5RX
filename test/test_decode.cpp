// Tests: RX5808 bus words, frequency maths and channel lookup.
#include "test_util.h"
#include "../firmware/core/rx5808_decode.h"
#include "../firmware/core/fpv_channels.h"

using namespace c5rx;

static void test_word_roundtrip() {
    c5rxtest::suite("word");
    // A write to the frequency register.
    uint32_t w = buildWord(REG_SYNTH_RF, true, 0x0A5A5);
    Rx5808Word p = parseWord(w);
    CHECK_EQ(p.address, REG_SYNTH_RF);
    CHECK(p.write);
    CHECK_EQ(p.data & 0xFFFF, 0x0A5A5);

    // A read (write bit clear).
    Rx5808Word r = parseWord(buildWord(REG_SYNTH_RF, false, 0));
    CHECK(!r.write);
    CHECK_EQ(r.address, REG_SYNTH_RF);

    // The reset register, 0xF.
    Rx5808Word s = parseWord(buildWord(REG_STATE, true, 0xFFFFF));
    CHECK_EQ(s.address, REG_STATE);
    CHECK_EQ(s.data, 0xFFFFF);
}

static void test_freq_maths() {
    c5rxtest::suite("freq");
    // MHz to register value and back lands within 1 MHz (the RX5808 steps in 2 MHz).
    const uint16_t chans[] = {5658,5695,5732,5769,5806,5843,5880,  // R1-R7
                              5740,5800,5880,                       // F
                              5725,5865,5733,5866,5645,5705,5885};  // A,B,E
    for (uint16_t f : chans) {
        uint16_t reg = mhzToSynthReg(f);
        uint16_t back = synthRegToMhz(reg);
        // Within 1 MHz of the original.
        CHECK(std::abs((int)back - (int)f) <= 1);
        // It also snaps to a real channel within 1 MHz. A few channels share a
        // register value: R5 (5806) and A4 (5805), for example, are 1 MHz apart.
        uint16_t norm = normaliseFrequency(back);
        CHECK(snapToChannel(norm, 0) != nullptr);
        CHECK(std::abs((int)norm - (int)f) <= 1);
    }
}

static void test_range_and_snap() {
    c5rxtest::suite("range");
    CHECK(c5InRange(5180));
    CHECK(c5InRange(5885));
    CHECK(!c5InRange(5179));
    CHECK(!c5InRange(5917));   // R8 out of range
    CHECK(!c5InRange(5945));   // E8 out of range

    // R8 and E6 to E8 must stay out of range after snapping.
    CHECK(!c5InRange(normaliseFrequency(synthRegToMhz(mhzToSynthReg(5917)))));
    const FpvChannel* r1 = snapToChannel(5657, 2);  // host programs 5657 for R1
    CHECK(r1 != nullptr);
    CHECK_EQ(r1->mhz, 5658);
    CHECK(r1->band == 'R' && r1->index == 1);

    // A frequency nowhere near a channel doesn't snap.
    CHECK(snapToChannel(5500, 2) == nullptr);
}

int main() {
    std::printf("== test_decode ==\n");
    test_word_roundtrip();
    test_freq_maths();
    test_range_and_snap();
    return c5rxtest::report();
}
