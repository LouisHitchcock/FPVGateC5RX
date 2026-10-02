#include "rf_backend.h"

namespace c5rx {

bool SimRfBackend::tune(uint16_t mhz) {
    if (!begun_) return false;
    if (mhz < lo_ || mhz > hi_) return false;
    tuned_ = mhz;
    return true;
}

bool SimRfBackend::readPowerDb(float& db, uint32_t nowMs) {
    if (!powered_ || tuned_ == 0) { db = noiseDb_; return false; }

    // Only give a new reading every pollMs_ milliseconds.
    if (!firstPoll_ && (nowMs - lastPollMs_) < pollMs_) return false;
    firstPoll_ = false;
    lastPollMs_ = nowMs;

    float power = noiseDb_;
    if (carrierMhz_ != 0) {
        int detune = (int)tuned_ - (int)carrierMhz_;
        if (detune < 0) detune = -detune;
        float recv = carrierDb_ - rolloff_ * (float)detune;
        // Use whichever is stronger, the transmitter or the noise floor.
        // Simpler than adding the powers properly, and enough for the tests.
        if (recv > power) power = recv;
    }
    db = power;
    return true;
}

} // namespace c5rx
