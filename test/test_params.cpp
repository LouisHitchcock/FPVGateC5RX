// Tests: saving and loading settings, and rejecting bad data.
#include "test_util.h"
#include "../firmware/core/params.h"
#include <cstring>

using namespace c5rx;

static void test_roundtrip() {
    c5rxtest::suite("params-rt");
    PersistConfig c;
    c.dbLo = -92; c.dbHi = -30; c.emaAlpha = 0.5f; c.useMedian3 = 0;
    c.settleMs = 40; c.stallMs = 150;
    c.vdd = 3.3f; c.dividerRatio = 0.5f; c.vFullScale = 1.55f; c.vFloor = 0.05f;
    c.bootMhz = 5800;
    c.kneeDb = -55; c.kneeRatio = 6;

    uint8_t buf[64];
    size_t n = serializeConfig(c, buf, sizeof(buf));
    CHECK(n == serializedSize());
    CHECK(n > 0);

    PersistConfig o;
    CHECK(deserializeConfig(o, buf, n));
    CHECK_NEAR(o.dbLo, -92, 1e-6);
    CHECK_NEAR(o.dbHi, -30, 1e-6);
    CHECK_NEAR(o.vFullScale, 1.55, 1e-6);
    CHECK_EQ(o.settleMs, 40);
    CHECK_EQ(o.useMedian3, 0);
    CHECK_EQ(o.bootMhz, 5800);
    CHECK_NEAR(o.kneeDb, -55, 1e-6);
    CHECK_NEAR(o.kneeRatio, 6, 1e-6);

    // An invalid knee ratio is rejected.
    PersistConfig badK; badK.kneeRatio = 0.5f;
    n = serializeConfig(badK, buf, sizeof(buf));
    CHECK(!deserializeConfig(o, buf, n));

    // An out-of-range boot frequency is rejected.
    PersistConfig bad; bad.bootMhz = 5917;
    n = serializeConfig(bad, buf, sizeof(buf));
    CHECK(!deserializeConfig(o, buf, n));
}

static void test_crc_rejects_corruption() {
    c5rxtest::suite("params-crc");
    PersistConfig c; uint8_t buf[64];
    size_t n = serializeConfig(c, buf, sizeof(buf));
    buf[8] ^= 0xFF;                 // flip a payload byte
    PersistConfig o;
    CHECK(!deserializeConfig(o, buf, n));   // CRC mismatch -> reject
}

static void test_bad_magic_version() {
    c5rxtest::suite("params-magic");
    PersistConfig c; uint8_t buf[64];
    size_t n = serializeConfig(c, buf, sizeof(buf));
    uint8_t b2[64]; memcpy(b2, buf, n);
    b2[0] ^= 0x01;                  // corrupt magic
    PersistConfig o;
    CHECK(!deserializeConfig(o, b2, n));
    // Too short.
    CHECK(!deserializeConfig(o, buf, n - 1));
}

static void test_sanity_clamp() {
    c5rxtest::suite("params-sane");
    // A good CRC but impossible values (top below bottom) is still rejected.
    PersistConfig c; c.dbLo = -10; c.dbHi = -50;   // inverted
    uint8_t buf[64];
    size_t n = serializeConfig(c, buf, sizeof(buf));
    PersistConfig o;
    CHECK(!deserializeConfig(o, buf, n));

    PersistConfig c2; c2.emaAlpha = 2.0f;          // out of (0,1]
    n = serializeConfig(c2, buf, sizeof(buf));
    CHECK(!deserializeConfig(o, buf, n));
}

int main() {
    std::printf("== test_params ==\n");
    test_roundtrip();
    test_crc_rejects_corruption();
    test_bad_magic_version();
    test_sanity_clamp();
    return c5rxtest::report();
}
