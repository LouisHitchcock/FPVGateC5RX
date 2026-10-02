// Tests: receiving RX5808 bus frames, for writes and reads.
#include "test_util.h"
#include "../firmware/core/rx5808_bus.h"
#include "../firmware/core/rx5808_decode.h"

using namespace c5rx;

// Returns a fixed value for each register, for the read tests.
static uint32_t provider(void* ctx, uint8_t addr) {
    (void)ctx;
    if (addr == REG_SYNTH_RF) return 0x0ABCD;      // a frequency register value
    if (addr == 0x7)          return 0x8ABCD;      // a full 20-bit value
    return 0;
}

// Feed in a 25-bit word as if the host sent it (a write).
static Rx5808Word replayWrite(uint32_t word25) {
    Rx5808Bus bus; bus.setReadProvider(provider, nullptr);
    bus.start();
    for (int i = 0; i < 25; ++i) {
        CHECK(bus.hostDrives());                   // on a write the host sends all 25 bits
        bus.pushBit((word25 >> i) & 1);
    }
    CHECK(bus.complete());
    return bus.word();
}

static void test_write_assembly() {
    c5rxtest::suite("bus-write");
    uint32_t w = buildWord(REG_SYNTH_RF, true, mhzToSynthReg(5800));
    Rx5808Word r = replayWrite(w);
    CHECK_EQ(r.address, REG_SYNTH_RF);
    CHECK(r.write);
    CHECK_EQ(r.data & 0xFFFF, mhzToSynthReg(5800));

    Rx5808Word p = replayWrite(buildWord(REG_POWER, true, 0xFFFFF));
    CHECK_EQ(p.address, REG_POWER);
    CHECK_EQ(p.data, 0xFFFFF);
}

static void test_read_drives_register() {
    c5rxtest::suite("bus-read");
    Rx5808Bus bus; bus.setReadProvider(provider, nullptr);
    bus.start();
    // The host sends address 0x1 with R/W = 0 (a read): the first 5 bits.
    uint32_t hdr = buildWord(REG_SYNTH_RF, false, 0);
    for (int i = 0; i < 5; ++i) {
        CHECK(bus.hostDrives());
        bus.pushBit((hdr >> i) & 1);
    }
    // Then we send back the 20 data bits of register 0x1 (0x0ABCD).
    uint32_t got = 0;
    for (int i = 0; i < 20; ++i) {
        CHECK(!bus.hostDrives());
        if (bus.popBit()) got |= (1u << i);
    }
    CHECK(bus.complete());
    CHECK_EQ(got, 0x0ABCD);
    CHECK(!bus.isWrite());
    CHECK_EQ(bus.address(), REG_SYNTH_RF);
}

static void test_read_ext_status() {
    c5rxtest::suite("bus-read-ext");
    Rx5808Bus bus; bus.setReadProvider(provider, nullptr);
    bus.start();
    uint32_t hdr = buildWord(0x7, false, 0);
    for (int i = 0; i < 5; ++i) bus.pushBit((hdr >> i) & 1);
    uint32_t got = 0;
    for (int i = 0; i < 20; ++i) if (bus.popBit()) got |= (1u << i);
    CHECK_EQ(got, 0x8ABCD);       // full 20-bit value preserved
}

int main() {
    std::printf("== test_bus ==\n");
    test_write_assembly();
    test_read_drives_register();
    test_read_ext_status();
    return c5rxtest::report();
}
