// Tests: the register values a host reads back.
#include "test_util.h"
#include "../firmware/core/host_regs.h"
#include "../firmware/core/rx5808_decode.h"

using namespace c5rx;

static ControllerConfig cfg() {
    ControllerConfig c;
    c.pipeline.dbLo = -95; c.pipeline.dbHi = -35;
    c.pipeline.emaAlpha = 1.0f; c.pipeline.useMedian3 = false;
    c.pipeline.settleMs = 0; c.pipeline.stallMs = 200;
    return c;
}

static void test_synth_readback_compat() {
    c5rxtest::suite("hr-compat");
    SimRfBackend sim; Controller ctl; ctl.begin(&sim, cfg());
    uint16_t reg = mhzToSynthReg(5800);
    ctl.onHostWord(buildWord(REG_SYNTH_RF, true, reg), 10);
    CHECK_EQ(buildReadData(ctl, REG_SYNTH_RF), reg);
}

static void test_status_register() {
    c5rxtest::suite("hr-status");
    SimRfBackend sim; Controller ctl; ctl.begin(&sim, cfg());
    sim.setCarrier(5800, -45); sim.setPollIntervalMs(1);
    ctl.onHostWord(buildWord(REG_SYNTH_RF, true, mhzToSynthReg(5800)), 100);
    for (uint32_t t = 101; t <= 110; ++t) ctl.tick(t);

    uint32_t s = buildReadData(ctl, REG_EXT_STATUS);
    CHECK_EQ(s & 0xFF, ctl.rssiCounts());      // counts in D0-7
    CHECK((s >> 8) & 1);                        // valid
    CHECK((s >> 9) & 1);                        // supported
    CHECK((s >> 15) & 1);                       // alive marker
}

static void test_info_and_detection() {
    c5rxtest::suite("hr-info");
    SimRfBackend sim; Controller ctl; ctl.begin(&sim, cfg());
    sim.setCarrier(5800, -50); sim.setPollIntervalMs(1);
    ctl.onHostWord(buildWord(REG_SYNTH_RF, true, mhzToSynthReg(5800)), 100);
    for (uint32_t t = 101; t <= 110; ++t) ctl.tick(t);

    uint32_t info = buildReadData(ctl, REG_EXT_INFO);
    int8_t db = (int8_t)(info & 0xFF);
    CHECK_NEAR(db, -50, 2);                      // signed dBm
    CHECK_EQ((info >> 8) & 0xFF, kC5rxSignature);

    uint32_t status = buildReadData(ctl, REG_EXT_STATUS);
    CHECK(looksLikeC5rx(info, status));          // recognised

    // A real RX5808 wouldn't return the signature.
    CHECK(!looksLikeC5rx(0x00000, 0x08000));     // D15 set but no signature
    CHECK(!looksLikeC5rx(0xC500, 0x00000));      // signature but D15 clear
}

int main() {
    std::printf("== test_hostregs ==\n");
    test_synth_readback_compat();
    test_status_register();
    test_info_and_detection();
    return c5rxtest::report();
}
