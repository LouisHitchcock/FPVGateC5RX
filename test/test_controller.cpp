// Tests: the controller, using the simulated radio.
#include <initializer_list>
#include "test_util.h"
#include "../firmware/core/controller.h"
#include "../firmware/core/rx5808_decode.h"
#include "../firmware/core/fpv_channels.h"

using namespace c5rx;

// The word FPVGate sends to set a frequency.
static uint32_t synthWriteWord(uint16_t mhz) {
    return buildWord(REG_SYNTH_RF, true, mhzToSynthReg(mhz));
}

static ControllerConfig cfg() {
    ControllerConfig c;
    c.pipeline.dbLo = -95; c.pipeline.dbHi = -35;
    c.pipeline.emaAlpha = 0.5f; c.pipeline.useMedian3 = false;
    c.pipeline.settleMs = 35; c.pipeline.stallMs = 200;
    return c;
}

static void test_tune_and_track() {
    c5rxtest::suite("ctl-track");
    SimRfBackend sim;
    Controller ctl;
    ctl.begin(&sim, cfg());
    CHECK(ctl.state() == State::IDLE);

    sim.setNoiseFloorDb(-90);
    sim.setCarrier(5658, -45);
    sim.setPollIntervalMs(1);

    ctl.onHostWord(synthWriteWord(5658), 1000);
    CHECK(ctl.state() == State::TUNING);
    CHECK_EQ(ctl.currentMhz(), 5658);
    CHECK(!ctl.rssiValid());             // blanking

    // Run past the settling time.
    for (uint32_t t = 1001; t <= 1040; ++t) ctl.tick(t);
    CHECK(ctl.state() == State::TRACKING);
    CHECK(ctl.rssiValid());
    CHECK(ctl.rssiCounts() > 128);       // -45 dB is in upper half of -95..-35
}

static void test_readback_matches() {
    c5rxtest::suite("ctl-readback");
    SimRfBackend sim; Controller ctl; ctl.begin(&sim, cfg());
    // Reading register 0x1 must return exactly what the host wrote, so
    // FPVGate's verifyFrequency() passes.
    for (uint16_t f : {5658u, 5800u, 5880u, 5733u}) {
        uint16_t reg = mhzToSynthReg(f);
        ctl.onHostWord(buildWord(REG_SYNTH_RF, true, reg), 10);
        CHECK_EQ(ctl.synthReadback(), reg);
    }
}

static void test_out_of_range() {
    c5rxtest::suite("ctl-oor");
    SimRfBackend sim; Controller ctl; ctl.begin(&sim, cfg());
    // R8 (5917) is outside the C5's range. The command is accepted, so the
    // read-back matches, but there's no RSSI and the radio doesn't tune.
    uint16_t reg = mhzToSynthReg(5917);
    ctl.onHostWord(buildWord(REG_SYNTH_RF, true, reg), 10);
    CHECK_EQ(ctl.synthReadback(), reg);
    CHECK(!ctl.freqSupported());
    for (uint32_t t = 11; t < 100; ++t) ctl.tick(t);
    CHECK(!ctl.rssiValid());
}

static void test_powerdown_wake() {
    c5rxtest::suite("ctl-power");
    SimRfBackend sim; Controller ctl; ctl.begin(&sim, cfg());
    sim.setCarrier(5800, -40); sim.setPollIntervalMs(1);
    ctl.onHostWord(synthWriteWord(5800), 100);
    for (uint32_t t = 101; t <= 140; ++t) ctl.tick(t);
    CHECK(ctl.rssiValid());

    // Power down: all ones written to register 0xA.
    ctl.onHostWord(buildWord(REG_POWER, true, 0xFFFFF), 200);
    CHECK(ctl.state() == State::POWERDOWN);
    ctl.tick(210);
    CHECK(!ctl.rssiValid());

    // Tuning wakes it up again.
    ctl.onHostWord(synthWriteWord(5800), 300);
    for (uint32_t t = 301; t <= 340; ++t) ctl.tick(t);
    CHECK(ctl.rssiValid());
}

static void test_gate_pass_profile() {
    c5rxtest::suite("ctl-pass");
    // A quad flies past: the signal rises, then falls. The RSSI should make
    // one clean peak, which is what lap timing relies on.
    SimRfBackend sim; Controller ctl; ctl.begin(&sim, cfg());
    sim.setNoiseFloorDb(-95); sim.setRolloffDbPerMhz(3); sim.setPollIntervalMs(1);
    sim.setCarrier(5800, -95);
    ctl.onHostWord(synthWriteWord(5800), 0);
    for (uint32_t t = 1; t <= 40; ++t) ctl.tick(t);   // settle

    int peak = 0; uint32_t peakT = 0;
    for (uint32_t t = 41; t <= 241; ++t) {
        // Signal rises from -95 dB to -30 dB at t = 141, then falls back.
        float d = (t < 141) ? (float)(t - 41) : (float)(241 - t);
        float power = -95.0f + d * (65.0f / 100.0f);
        sim.setCarrier(5800, power);
        ctl.tick(t);
        if (ctl.rssiCounts() > peak) { peak = ctl.rssiCounts(); peakT = t; }
    }
    CHECK(peak > 200);                       // strong peak reached
    CHECK(peakT > 120 && peakT < 165);       // peak near the middle (allow EMA lag)
}

int main() {
    std::printf("== test_controller ==\n");
    test_tune_and_track();
    test_readback_matches();
    test_out_of_range();
    test_powerdown_wake();
    test_gate_pass_profile();
    return c5rxtest::report();
}
