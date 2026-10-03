// Tests: turning readings into RSSI (calibration, smoothing, peak-hold, soft ceiling).
#include "test_util.h"
#include "../firmware/core/rssi_pipeline.h"

using namespace c5rx;

static void test_calibration_map() {
    c5rxtest::suite("cal");
    RssiPipeline p;
    RssiPipelineConfig c;
    c.dbLo = -95; c.dbHi = -35; c.emaAlpha = 1.0f; c.useMedian3 = false;
    c.settleMs = 0; c.windowMaxMs = 0; c.preMin = 1;   // isolate the calibration map
    p.begin(c);
    // With no smoothing, the output follows the input straight away.
    p.onSample(-95, 10); CHECK_EQ(p.counts(), 0);
    p.onSample(-35, 20); CHECK_EQ(p.counts(), 255);
    p.onSample(-65, 30); CHECK_NEAR(p.counts(), 127, 2);   // midpoint
    // Values outside the range are clamped.
    p.onSample(-120, 40); CHECK_EQ(p.counts(), 0);
    p.onSample(0, 50);    CHECK_EQ(p.counts(), 255);
}

static void test_smoothing_monotone() {
    c5rxtest::suite("ema");
    RssiPipeline p;
    RssiPipelineConfig c;
    c.dbLo = -95; c.dbHi = -35; c.emaAlpha = 0.3f; c.useMedian3 = true;
    c.settleMs = 0;
    p.begin(c);
    // A step up: the smoothed value climbs steadily and never overshoots.
    uint8_t prev = 0;
    for (uint32_t t = 0; t < 50; ++t) {
        p.onSample(-40, t);   // high signal
        CHECK(p.counts() >= prev);
        prev = p.counts();
    }
    CHECK(p.counts() > 200);          // converged near top
    CHECK(p.counts() <= 255);
}

static void test_median_rejects_glitch() {
    c5rxtest::suite("median");
    RssiPipeline p;
    RssiPipelineConfig c;
    c.dbLo = -95; c.dbHi = -35; c.emaAlpha = 1.0f; c.useMedian3 = true;
    c.settleMs = 0;
    p.begin(c);
    p.onSample(-60, 1);
    p.onSample(-60, 2);
    uint8_t before = p.counts();
    p.onSample(-10, 3);   // single wild glitch
    // The median of -60, -60 and -10 is -60, so the glitch is ignored.
    CHECK_NEAR(p.counts(), before, 3);
}

static void test_window_removes_dips() {
    c5rxtest::suite("window");
    RssiPipeline p;
    RssiPipelineConfig c;
    c.dbLo = -90; c.dbHi = -20; c.emaAlpha = 1.0f; c.useMedian3 = false;
    c.settleMs = 0; c.windowMaxMs = 30; c.preMin = 1;   // the peak-hold on its own
    p.begin(c);
    // What the C5 really does: steady at -70, with a 3 ms dip to -80 every 25 ms.
    float minOut = 0, maxOut = -200;
    for (uint32_t t = 0; t < 200; ++t) {
        float db = ((t % 25) < 3 && t > 25) ? -80.0f : -70.0f;
        p.onSample(db, t);
        if (t > 30) {
            if (p.smoothedDb() < minOut) minOut = p.smoothedDb();
            if (p.smoothedDb() > maxOut) maxOut = p.smoothedDb();
        }
    }
    CHECK_NEAR(minOut, -70, 0.01);       // dips never reach the output
    CHECK_NEAR(maxOut, -70, 0.01);

    // A rise shows up on the very next reading.
    p.onSample(-30, 200);
    CHECK_NEAR(p.smoothedDb(), -30, 0.01);

    // A fall is held for the window length, then let through.
    for (uint32_t t = 201; t <= 229; ++t) p.onSample(-70, t);
    CHECK_NEAR(p.smoothedDb(), -30, 0.01);   // still within 30 ms of the peak
    p.onSample(-70, 230);
    CHECK_NEAR(p.smoothedDb(), -70, 0.01);   // peak aged out after 30 ms

    // With the window off, readings pass straight through.
    p.setWindowMaxMs(0);
    p.onSample(-80, 240);
    CHECK_NEAR(p.smoothedDb(), -80, 0.01);
}

static void test_knee_soft_ceiling() {
    c5rxtest::suite("knee");
    RssiPipelineConfig c;
    c.dbLo = -80; c.dbHi = -15; c.emaAlpha = 1.0f; c.useMedian3 = false;
    c.settleMs = 0; c.windowMaxMs = 0;
    auto countsAt = [&](float db, bool knee) {
        RssiPipeline p; RssiPipelineConfig cc = c;
        if (knee) { cc.kneeDb = -55; cc.kneeRatio = 6; }
        p.begin(cc);
        p.onSample(db, 1);
        return (int)p.counts();
    };
    // Knee off: a straight mapping (levels from the VTX power-cycle test).
    CHECK_NEAR(countsAt(-54, false), 102, 1);
    CHECK_NEAR(countsAt(-26, false), 212, 1);
    // Knee at -55 dB, ratio 6: the VTX start-up step shrinks from about 110 to 37.
    // (-54 is just above the knee: -55 + 1/6 = -54.83; top = -55 + 40/6.)
    int lowK = countsAt(-54, true), highK = countsAt(-26, true);
    CHECK_NEAR(lowK, 203, 1);
    CHECK_NEAR(highK, 240, 1);
    CHECK(highK - lowK < 40);
    // It still rises with signal (a close pass still peaks), and dbHi still reads 255.
    CHECK(countsAt(-20, true) > highK);
    CHECK_EQ(countsAt(-15, true), 255);
    // Below the knee nothing changes: the background stays low.
    CHECK(countsAt(-76, true) < 40);
    CHECK(countsAt(-80, true) == 0);
}

static void test_blanking_after_tune() {
    c5rxtest::suite("blank");
    RssiPipeline p;
    RssiPipelineConfig c;
    c.dbLo = -95; c.dbHi = -35; c.emaAlpha = 1.0f; c.useMedian3 = false;
    c.settleMs = 35;
    p.begin(c);
    p.onSample(-50, 100);
    CHECK(p.valid());
    uint8_t held = p.counts();
    p.onTune(200);                 // retune at t=200, blank until 235
    p.onSample(-40, 205);          // sample during settle
    CHECK(!p.valid());             // not valid yet
    CHECK_EQ(p.counts(), held);    // holds last stable value
    p.onSample(-40, 240);          // after settle
    CHECK(p.valid());
    CHECK(p.counts() > held);      // new (higher) value now exposed
}

static void test_stall_detect() {
    c5rxtest::suite("stall");
    RssiPipeline p;
    RssiPipelineConfig c; c.stallMs = 200; c.settleMs = 0;
    p.begin(c);
    p.onSample(-50, 1000);
    CHECK(p.valid());
    p.tick(1100); CHECK(!p.stalled());
    p.tick(1300); CHECK(p.stalled());   // >200 ms since last sample
    CHECK(!p.valid());
}

static void test_premin_drops_bursts() {
    c5rxtest::suite("premin");
    RssiPipeline p;
    RssiPipelineConfig c;
    c.dbLo = -95; c.dbHi = -35; c.emaAlpha = 1.0f; c.useMedian3 = false;
    c.settleMs = 0; c.windowMaxMs = 30;
    p.begin(c);
    CHECK_EQ(p.config().preMin, 8);   // the default
    uint32_t t = 0;
    for (int i = 0; i < 20; ++i) p.onSample(-95, ++t);
    // Wi-Fi-like bursts: 1 to 3 ms at -60, every 24 ms (as measured on R5).
    // Without the minimum, the 30 ms peak-hold would hold -60 throughout.
    uint8_t worst = 0;
    for (int cycle = 0; cycle < 12; ++cycle) {
        for (int i = 0; i < 24; ++i) {
            p.onSample(i < 1 + cycle % 3 ? -60.0f : -95.0f, ++t);
            if (p.counts() > worst) worst = p.counts();
        }
    }
    CHECK_EQ(worst, 0);
    // A steady signal (a VTX) comes through 7 ms late...
    int lag = -1;
    for (int i = 0; i < 40; ++i) {
        p.onSample(-50, ++t);
        if (lag < 0 && p.counts() > 150) lag = i;
    }
    CHECK_EQ(lag, 7);
    // ...and its dips of up to 6 ms (10 dB) are still bridged by the peak-hold.
    uint8_t lowest = 255;
    for (int cycle = 0; cycle < 6; ++cycle) {
        for (int i = 0; i < 40; ++i) {
            p.onSample(i < 6 ? -60.0f : -50.0f, ++t);
            if (p.counts() < lowest) lowest = p.counts();
        }
    }
    CHECK_EQ(lowest, p.counts());
    // The setting is clamped.
    p.setPreMin(200); CHECK_EQ(p.config().preMin, RssiPipeline::kPreMinCap);
    p.setPreMin(0);   CHECK_EQ(p.config().preMin, 1);
}

int main() {
    std::printf("== test_pipeline ==\n");
    test_premin_drops_bursts();
    test_calibration_map();
    test_smoothing_monotone();
    test_median_rejects_glitch();
    test_window_removes_dips();
    test_knee_soft_ceiling();
    test_blanking_after_tune();
    test_stall_detect();
    return c5rxtest::report();
}
