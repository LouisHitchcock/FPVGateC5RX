// The whole chain together, as an FPVGate session: tune over the bus, a gate
// pass, reading the RSSI back digitally, and the analog output following it.
#include "test_util.h"
#include "../firmware/core/rx5808_bus.h"
#include "../firmware/core/controller.h"
#include "../firmware/core/host_regs.h"
#include "../firmware/core/rssi_codec.h"
#include "../firmware/core/rx5808_decode.h"

using namespace c5rx;

static Controller* g_ctl = nullptr;
static uint32_t readProvider(void* ctx, uint8_t addr) {
    (void)ctx;
    return g_ctl ? buildReadData(*g_ctl, addr) : 0;
}

// Do a host read of `addr` over the bus and return the 20-bit value.
static uint32_t busRead(Rx5808Bus& bus, uint8_t addr) {
    bus.start();
    uint32_t hdr = buildWord(addr, false, 0);
    for (int i = 0; i < 5; ++i) bus.pushBit((hdr >> i) & 1);
    uint32_t got = 0;
    for (int i = 0; i < 20; ++i) if (bus.popBit()) got |= (1u << i);
    return got;
}

int main() {
    std::printf("== test_integration ==\n");
    c5rxtest::suite("session");

    SimRfBackend sim;
    Controller ctl;
    ControllerConfig cfg;
    cfg.pipeline.dbLo = -95; cfg.pipeline.dbHi = -35;
    cfg.pipeline.emaAlpha = 0.5f; cfg.pipeline.useMedian3 = true;
    cfg.pipeline.settleMs = 35; cfg.pipeline.stallMs = 200;
    ctl.begin(&sim, cfg);
    g_ctl = &ctl;

    Rx5808Bus bus; bus.setReadProvider(readProvider, nullptr);
    RssiCodec codec; RssiCodecConfig ccfg; codec.begin(ccfg);

    sim.setNoiseFloorDb(-95);
    sim.setRolloffDbPerMhz(3);
    sim.setPollIntervalMs(1);

    // 1) The host tunes to R1 (5658), just as FPVGate does.
    uint16_t reg = mhzToSynthReg(5658);
    ctl.onHostWord(buildWord(REG_SYNTH_RF, true, reg), 1000);
    CHECK_EQ(ctl.currentMhz(), 5658);

    // 2) Reading register 0x1 back matches what was written.
    CHECK_EQ(busRead(bus, REG_SYNTH_RF), reg);

    // 3) Settle, then a gate pass: -95 dB up to -30 dB and back down.
    sim.setCarrier(5658, -95);
    for (uint32_t t = 1001; t <= 1040; ++t) ctl.tick(t);
    CHECK(ctl.state() == State::TRACKING);

    int peakCounts = 0; uint32_t peakT = 0;
    int peakDensity = -128;
    bool detected = false;
    for (uint32_t t = 1041; t <= 1241; ++t) {
        float d = (t < 1141) ? (float)(t - 1041) : (float)(1241 - t);
        sim.setCarrier(5658, -95.0f + d * 0.65f);
        ctl.tick(t);

        uint8_t counts = ctl.rssiCounts();
        int8_t dens = codec.densityForCounts(counts);     // the analog output follows the RSSI
        if (counts > peakCounts) { peakCounts = counts; peakT = t; peakDensity = dens; }

        // Halfway through, read the RSSI digitally and check the signature.
        if (t == 1141) {
            uint32_t st = busRead(bus, REG_EXT_STATUS);
            uint32_t info = busRead(bus, REG_EXT_INFO);
            CHECK_EQ(st & 0xFF, ctl.rssiCounts());
            CHECK(looksLikeC5rx(info, st));
            detected = true;
        }
    }

    CHECK(detected);
    CHECK(peakCounts > 200);                 // a strong, clean peak
    CHECK(peakT > 1120 && peakT < 1165);     // near the middle of the pass
    CHECK(peakDensity > 0);                  // the analog output is high at the peak
    // No signal gives the lowest output setting.
    CHECK_EQ(codec.densityForCounts(0), -128);

    // 4) Retune to another channel; the read-back follows.
    uint16_t reg2 = mhzToSynthReg(5800);
    ctl.onHostWord(buildWord(REG_SYNTH_RF, true, reg2), 1300);
    CHECK_EQ(busRead(bus, REG_SYNTH_RF), reg2);
    CHECK(!ctl.rssiValid());                 // held invalid straight after retuning

    return c5rxtest::report();
}
