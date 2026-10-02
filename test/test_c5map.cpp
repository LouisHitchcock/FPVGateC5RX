// Tests: choosing the 5 GHz channel for an FPV frequency.
#include "test_util.h"
#include "../firmware/core/c5_freq_map.h"
#include "../firmware/core/fpv_channels.h"

using namespace c5rx;

static bool isRealChannel(uint16_t ch) {
    for (int i = 0; i < c5ChannelCount(); ++i)
        if (c5ChannelAt(i) == ch) return true;
    return false;
}

static void test_plan_reachable_channels() {
    c5rxtest::suite("c5map-plan");
    // Every in-range channel in bands A, B, E, F and R must map to a real
    // Wi-Fi channel, close enough to be inside the receive bandwidth.
    int n; const FpvChannel* tbl = fpvChannelTable(n);
    for (int i = 0; i < n; ++i) {
        uint16_t f = tbl[i].mhz;
        if (!c5InRange(f) || tbl[i].band == 'L') continue;
        C5Tuning t = planTuning(f);
        CHECK(t.ok);
        CHECK_EQ(t.centerMhz + t.offsetMhz, f);
        CHECK(t.offsetMhz <= 12 && t.offsetMhz >= -12);
        CHECK(isRealChannel(t.baseChannel));   // never an invented channel
        CHECK_EQ(t.centerMhz, 5000 + 5 * t.baseChannel);
    }
}

static void test_channel_list() {
    c5rxtest::suite("c5map-list");
    CHECK_EQ(c5ChannelCount(), 28);
    // The top block is 149, 153 and so on: not multiples of 4.
    CHECK(isRealChannel(149));
    CHECK(isRealChannel(177));
    CHECK(!isRealChannel(160));   // the bug the first hardware run exposed
    CHECK(!isRealChannel(148));
}

static void test_specific() {
    c5rxtest::suite("c5map-spec");
    C5Tuning r1 = planTuning(5658);
    CHECK_EQ(r1.baseChannel, 132);     // center 5660
    CHECK_EQ(r1.offsetMhz, -2);
    CHECK(r1.ok);

    C5Tuning f4 = planTuning(5800);
    CHECK_EQ(f4.baseChannel, 161);     // center 5805
    CHECK_EQ(f4.offsetMhz, -5);
    CHECK(f4.ok);

    C5Tuning e5 = planTuning(5885);
    CHECK_EQ(e5.baseChannel, 177);     // center 5885 exactly
    CHECK_EQ(e5.offsetMhz, 0);

    // L1 to L4 fall in the gap between 5320 and 5500 MHz, so they can't be tuned.
    CHECK(!planTuning(5373).ok);
    CHECK(!planTuning(5453).ok);
}

int main() {
    std::printf("== test_c5map ==\n");
    test_plan_reachable_channels();
    test_channel_list();
    test_specific();
    return c5rxtest::report();
}
