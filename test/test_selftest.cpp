// Tests: the boot self-test passes and sets every bit.
#include "test_util.h"
#include "../firmware/core/selftest.h"

using namespace c5rx;

int main() {
    std::printf("== test_selftest ==\n");
    c5rxtest::suite("bist");
    SelfTestResult r = runSelfTest();
    CHECK_EQ(r.passedBits, (uint16_t)ST_ALL);
    CHECK(r.pass());
    // Each bit on its own.
    CHECK(r.passedBits & ST_FREQ_MATHS);
    CHECK(r.passedBits & ST_RANGE);
    CHECK(r.passedBits & ST_PIPELINE);
    CHECK(r.passedBits & ST_CODEC);
    CHECK(r.passedBits & ST_PARAMS);
    return c5rxtest::report();
}
