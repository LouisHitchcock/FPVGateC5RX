// Tests: the simulated radio used by the other tests.
#include "test_util.h"
#include "../firmware/core/rf_backend.h"

using namespace c5rx;

static void test_range() {
    c5rxtest::suite("sim-range");
    SimRfBackend s; CHECK(s.begin());
    CHECK(s.tune(5658));
    CHECK_EQ(s.tunedMhz(), 5658);
    CHECK(!s.tune(5917));   // R8 out of C5 range
    CHECK(!s.tune(5179));
    CHECK_EQ(s.tunedMhz(), 5658);   // unchanged after failed tune
}

static void test_freshness() {
    c5rxtest::suite("sim-fresh");
    SimRfBackend s; s.begin(); s.tune(5800);
    s.setPollIntervalMs(5);
    float db;
    CHECK(s.readPowerDb(db, 100));   // first poll always fresh
    CHECK(!s.readPowerDb(db, 102));  // too soon
    CHECK(s.readPowerDb(db, 106));   // >=5 ms later
}

static void test_carrier_detune() {
    c5rxtest::suite("sim-carrier");
    SimRfBackend s; s.begin();
    s.setNoiseFloorDb(-95);
    s.setRolloffDbPerMhz(3.0f);
    s.setPollIntervalMs(1);
    s.setCarrier(5800, -40);

    float db;
    s.tune(5800); s.readPowerDb(db, 10);
    CHECK_NEAR(db, -40, 0.01);        // on the channel: full signal

    s.tune(5805); s.readPowerDb(db, 20);
    CHECK_NEAR(db, -55, 0.01);        // 5 MHz off: 15 dB down

    s.tune(5820); s.readPowerDb(db, 30);
    CHECK_NEAR(db, -95, 0.01);        // far off: just the noise floor
}

static void test_powerdown() {
    c5rxtest::suite("sim-power");
    SimRfBackend s; s.begin(); s.tune(5800); s.setCarrier(5800,-40);
    s.powerDown();
    float db;
    CHECK(!s.readPowerDb(db, 10));    // no fresh samples while powered down
    s.wake();
    CHECK(s.readPowerDb(db, 20));
}

int main() {
    std::printf("== test_backend ==\n");
    test_range();
    test_freshness();
    test_carrier_detune();
    test_powerdown();
    return c5rxtest::report();
}
