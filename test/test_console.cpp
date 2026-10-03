// Tests: the serial console (tune, channel, scan, stream, cal, knee, boot, save).
#include <string>
#include "test_util.h"
#include "../firmware/core/console.h"

using namespace c5rx;

static std::string g_out;
static int g_saves = 0;

static void writeHook(void*, const char* s) { g_out += s; }
static bool saveHook(void*) { ++g_saves; return true; }

static bool has(const char* s) { return g_out.find(s) != std::string::npos; }

struct Rig {
    SimRfBackend sim;
    Controller ctl;
    PersistConfig cfg;
    Console con;
    uint32_t t = 1000;
    Rig() {
        g_out.clear(); g_saves = 0;
        cfg.emaAlpha = 1.0f;   // instant smoothing: these tests are about commands
        ControllerConfig cc;
        cc.pipeline.dbLo = cfg.dbLo; cc.pipeline.dbHi = cfg.dbHi;
        cc.pipeline.emaAlpha = 1.0f; cc.pipeline.useMedian3 = false;
        cc.pipeline.settleMs = 35;
        ctl.begin(&sim, cc);
        sim.setNoiseFloorDb(-95); sim.setRolloffDbPerMhz(3); sim.setPollIntervalMs(1);
        ConsoleHooks h; h.write = writeHook; h.save = saveHook;
        con.begin(&ctl, &cfg, h);
    }
    void cmd(const char* s) { for (const char* p = s; *p; ++p) con.feedChar(*p, t); con.feedChar('\n', t); }
    void run(uint32_t ms) { for (uint32_t i = 0; i < ms; ++i) { ++t; ctl.tick(t); con.tick(t); } }
};

static void test_tune_and_status() {
    c5rxtest::suite("con-tune");
    Rig r;
    r.sim.setCarrier(5800, -50);
    r.cmd("tune 5800");
    CHECK(has("tuned 5800 MHz (F4)"));
    CHECK_EQ(r.ctl.currentMhz(), 5800);
    r.run(50);
    g_out.clear();
    r.cmd("s");
    CHECK(has("f=5800"));
    CHECK(has("st=TRACKING"));
    CHECK(has("valid=1"));
    CHECK(has("db=-50.0"));
}

static void test_channel_and_errors() {
    c5rxtest::suite("con-ch");
    Rig r;
    r.cmd("ch r1");
    CHECK_EQ(r.ctl.currentMhz(), 5658);
    r.cmd("ch R8");                        // 5917: out of range
    CHECK(has("outside the C5 range"));
    CHECK_EQ(r.ctl.currentMhz(), 5658);   // unchanged
    r.cmd("ch Z9");
    CHECK(has("unknown channel"));
    r.cmd("frobnicate");
    CHECK(has("unknown command"));
}

static void test_scan_finds_carrier() {
    c5rxtest::suite("con-scan");
    Rig r;
    r.sim.setCarrier(5760, -40);           // VTX on F2
    r.cmd("tune 5658");                    // start somewhere else
    r.run(50);
    r.cmd("scan 20");
    CHECK(r.con.scanning());
    r.run(10000);                          // plenty of time for all channels
    CHECK(!r.con.scanning());
    CHECK_EQ(r.con.lastScanPeakMhz(), 5760);
    CHECK_NEAR(r.con.lastScanPeakDb(), -40, 0.5);
    CHECK(has("scan done: peak F2 5760"));
    CHECK(!has("scan: R8"));               // out-of-range channels skipped
    CHECK_EQ(r.ctl.currentMhz(), 5658);    // restored after scan
}

static void test_scan_abort() {
    c5rxtest::suite("con-abort");
    Rig r;
    r.cmd("tune 5800");
    r.cmd("scan");
    r.run(100);
    r.cmd("s");
    CHECK(has("scan aborted"));
    CHECK(!r.con.scanning());
    CHECK_EQ(r.ctl.currentMhz(), 5800);
}

static void test_stream() {
    c5rxtest::suite("con-stream");
    Rig r;
    r.cmd("tune 5800");
    r.cmd("stream on 10");
    g_out.clear();
    r.run(1000);                           // 1 s at 10 Hz is about 10 lines
    int lines = 0;
    for (size_t p = 0; (p = g_out.find("f=5800", p)) != std::string::npos; ++p) ++lines;
    CHECK(lines >= 9 && lines <= 11);
    r.cmd("stream off");
    g_out.clear();
    r.run(1000);
    CHECK(!has("f=5800"));
}

static void test_cal_boot_save() {
    c5rxtest::suite("con-cfg");
    Rig r;
    // Capture calibration from live readings: VTX off, then close.
    r.sim.setCarrier(5800, -95);
    r.cmd("tune 5800");
    r.run(50);
    r.cmd("cal lo");
    CHECK_NEAR(r.cfg.dbLo, -95, 0.5);
    r.sim.setCarrier(5800, -30);
    r.run(50);
    r.cmd("cal hi");
    CHECK_NEAR(r.cfg.dbHi, -30, 0.5);
    r.run(5);                              // counts update on the next reading
    CHECK(r.ctl.rssiCounts() >= 254);      // new top of range now = 255

    r.cmd("cal -10 -20");                   // inverted: rejected
    CHECK(has("hi must be > lo"));
    CHECK_NEAR(r.cfg.dbHi, -30, 0.5);

    r.cmd("knee -50 6");
    CHECK_NEAR(r.cfg.kneeDb, -50, 1e-6);
    CHECK_NEAR(r.cfg.kneeRatio, 6, 1e-6);
    r.cmd("knee -100 6");                    // outside cal range: rejected
    CHECK(has("knee must be between"));
    CHECK_NEAR(r.cfg.kneeDb, -50, 1e-6);
    r.cmd("knee off");
    CHECK_NEAR(r.cfg.kneeRatio, 1, 1e-6);

    r.cmd("boot 5917");
    CHECK(has("out of C5 range"));
    r.cmd("boot 5800");
    CHECK_EQ(r.cfg.bootMhz, 5800);
    r.cmd("save");
    CHECK_EQ(g_saves, 1);
    CHECK(has("saved"));

    r.cmd("defaults");
    CHECK_EQ(r.cfg.bootMhz, 0);
    CHECK_NEAR(r.cfg.dbLo, -90, 1e-6);
}

static void test_sweep() {
    c5rxtest::suite("con-sweep");
    Rig r;
    r.sim.setCarrier(5800, -45);
    r.cmd("tune 5658");
    r.run(50);
    r.cmd("sweep 5780 5820 5 20");
    CHECK(r.con.scanning());
    r.run(5000);
    CHECK(!r.con.scanning());
    CHECK(has("sweep: 5780"));
    CHECK(has("sweep: 5820"));
    CHECK(has("sweep done: peak F4 5800"));
    CHECK_EQ(r.con.lastScanPeakMhz(), 5800);
    CHECK_EQ(r.ctl.currentMhz(), 5658);    // restored after sweep

    // Frequencies the radio can't tune are skipped.
    g_out.clear();
    r.cmd("sweep 5870 5930 10 20");
    r.run(5000);
    CHECK(has("sweep: 5880"));
    CHECK(!has("sweep: 5890"));

    // A scan after a sweep goes back to the channel table.
    g_out.clear();
    r.cmd("scan 20");
    r.run(10000);
    CHECK(has("scan: R1"));

    g_out.clear();
    r.cmd("sweep 5900 5800 5");
    CHECK(has("err:"));
    r.cmd("sweep 5800");
    CHECK(has("usage: sweep"));
}

static void test_range_follows_backend() {
    c5rxtest::suite("con-range");
    Rig r;
    r.sim.setRange(4900, 6000);            // a radio that reaches R8
    r.sim.setCarrier(5917, -42);
    r.cmd("ch R8");
    CHECK(has("tuned 5917 MHz (R8)"));
    CHECK(r.ctl.freqSupported());
    r.run(50);
    CHECK(r.ctl.rssiValid());
    CHECK_NEAR(r.ctl.rssiDb(), -42, 0.5);
    g_out.clear();
    r.cmd("scan 20");
    r.run(10000);
    CHECK(has("scan: R8"));
    CHECK(has("scan done: peak R8 5917"));
}

static void test_rf_without_settings() {
    c5rxtest::suite("con-rf");
    Rig r;
    r.cmd("rf");
    CHECK(has("no rf settings"));
}

int main() {
    std::printf("== test_console ==\n");
    test_sweep();
    test_range_follows_backend();
    test_rf_without_settings();
    test_tune_and_status();
    test_channel_and_errors();
    test_scan_finds_carrier();
    test_scan_abort();
    test_stream();
    test_cal_boot_save();
    return c5rxtest::report();
}
