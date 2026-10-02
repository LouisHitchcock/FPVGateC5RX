// Tests: turning an RSSI value into a sigma-delta setting and a voltage.
#include "test_util.h"
#include "../firmware/core/rssi_codec.h"

using namespace c5rx;

static void test_endpoints() {
    c5rxtest::suite("codec-ends");
    RssiCodec c; RssiCodecConfig cfg; c.begin(cfg);   // defaults: ratio .5, FS 1.50
    CHECK_NEAR(c.pinVoltageForCounts(0), 0.0, 1e-6);
    CHECK_NEAR(c.pinVoltageForCounts(255), 1.50, 1e-6);
    // RSSI 255 must stay below the host's ADC limit (about 1.65 V).
    CHECK(c.pinVoltageForCounts(255) < 1.65);
    // It can't be more than the divider allows (0.5 x 3.3 V = 1.65 V).
    CHECK(c.pinVoltageForCounts(255) <= cfg.dividerRatio * cfg.vdd);
    // The on-time stays between 0 and 1, and uses most of the range.
    CHECK(c.dutyForCounts(255) <= 1.0);
    CHECK(c.dutyForCounts(255) > 0.9);
}

static void test_monotonic_and_roundtrip() {
    c5rxtest::suite("codec-mono");
    RssiCodec c; RssiCodecConfig cfg; c.begin(cfg);
    float prevV = -1; int prevD = -200;
    for (int n = 0; n <= 255; ++n) {
        int8_t dens = c.densityForCounts((uint8_t)n);
        float v = c.pinVoltageForDensity(dens);
        CHECK(dens >= prevD);        // never goes down
        CHECK(v >= prevV - 1e-6);    // never goes down
        prevD = dens; prevV = v;
        // Each setting gives the wanted voltage to within one step (about 6 mV).
        float target = c.pinVoltageForCounts((uint8_t)n);
        CHECK_NEAR(v, target, 0.008);
    }
}

static void test_no_divider_config() {
    c5rxtest::suite("codec-nodiv");
    // With no divider, vFullScale alone keeps RSSI 255 under the ADC limit.
    RssiCodec c; RssiCodecConfig cfg;
    cfg.dividerRatio = 1.0f; cfg.vFullScale = 1.60f;
    c.begin(cfg);
    CHECK_NEAR(c.pinVoltageForCounts(255), 1.60, 1e-6);
    // Only about 45% of the on-time range gets used: coarser, but about 115 steps.
    CHECK(c.densityForCounts(255) < 0);   // on-time under half gives a negative density
    CHECK(c.densityForCounts(0) == -128);
}

int main() {
    std::printf("== test_codec ==\n");
    test_endpoints();
    test_monotonic_and_roundtrip();
    test_no_divider_config();
    return c5rxtest::report();
}
