#include "rssi_pipeline.h"

namespace c5rx {

static inline float med3(float a, float b, float c) {
    // Median of three values.
    float mx = a > b ? a : b;
    float mn = a < b ? a : b;
    float m  = c > mx ? mx : (c < mn ? mn : c);
    return m;
}

void RssiPipeline::begin(const RssiPipelineConfig& cfg) {
    cfg_ = cfg;
    reset();
}

void RssiPipeline::reset() {
    medCount_ = 0; medIdx_ = 0;
    haveEma_ = false; emaDb_ = cfg_.dbLo;
    counts_ = 0; heldCounts_ = 0;
    valid_ = false; stalled_ = false;
    haveSample_ = false;
    lastSampleMs_ = 0; blankUntilMs_ = 0;
    winHead_ = 0; winCount_ = 0;
}

float RssiPipeline::windowMax(float db, uint32_t nowMs) {
    if (cfg_.windowMaxMs == 0) return db;
    // Append the new sample (overwrite the oldest if the buffer is full).
    int idx = (winHead_ + winCount_) % (int)kWinCap;
    if (winCount_ == (int)kWinCap) { winHead_ = (winHead_ + 1) % (int)kWinCap; --winCount_; }
    winDb_[idx] = db;
    winT_[idx] = nowMs;
    ++winCount_;
    // Drop samples that have aged out of the window.
    while (winCount_ > 1 && (nowMs - winT_[winHead_]) >= cfg_.windowMaxMs) {
        winHead_ = (winHead_ + 1) % (int)kWinCap;
        --winCount_;
    }
    float mx = winDb_[winHead_];
    for (int i = 1; i < winCount_; ++i) {
        float v = winDb_[(winHead_ + i) % (int)kWinCap];
        if (v > mx) mx = v;
    }
    return mx;
}

void RssiPipeline::setCalibration(float dbLo, float dbHi) {
    cfg_.dbLo = dbLo;
    cfg_.dbHi = dbHi;
}

float RssiPipeline::compress(float db) const {
    if (cfg_.kneeRatio <= 1.0f || db <= cfg_.kneeDb) return db;
    return cfg_.kneeDb + (db - cfg_.kneeDb) / cfg_.kneeRatio;
}

uint8_t RssiPipeline::mapCounts(float db) const {
    // Both the reading and the top of the scale go through the soft ceiling,
    // so dbHi still reads exactly 255 whatever the knee setting.
    float lo = compress(cfg_.dbLo);
    float span = compress(cfg_.dbHi) - lo;
    if (span <= 0.0f) return 0;
    float x = (compress(db) - lo) / span * 255.0f;
    if (x < 0.0f) x = 0.0f;
    if (x > 255.0f) x = 255.0f;
    return (uint8_t)(x + 0.5f);
}

void RssiPipeline::onTune(uint32_t nowMs) {
    // Hold the current output while the radio settles, and mark it invalid.
    heldCounts_ = counts_;
    blankUntilMs_ = nowMs + cfg_.settleMs;
    valid_ = false;
    // Keep the smoothed value, so the output doesn't restart from zero, but
    // forget the old channel's readings.
    medCount_ = 0; medIdx_ = 0;
    winHead_ = 0; winCount_ = 0;
}

void RssiPipeline::onSample(float db, uint32_t nowMs) {
    lastSampleMs_ = nowMs;
    haveSample_ = true;
    stalled_ = false;

    float held = windowMax(db, nowMs);   // hides the AGC-refresh dips
    float filtered = held;
    if (cfg_.useMedian3) {
        med_[medIdx_] = held;
        medIdx_ = (medIdx_ + 1) % 3;
        if (medCount_ < 3) ++medCount_;
        if (medCount_ == 3) filtered = med3(med_[0], med_[1], med_[2]);
    }

    if (!haveEma_) { emaDb_ = filtered; haveEma_ = true; }
    else emaDb_ += cfg_.emaAlpha * (filtered - emaDb_);

    uint8_t mapped = mapCounts(emaDb_);

    if (nowMs < blankUntilMs_) {
        // Still settling: keep showing the last good value.
        counts_ = heldCounts_;
        valid_ = false;
    } else {
        counts_ = mapped;
        valid_ = true;
    }
}

void RssiPipeline::tick(uint32_t nowMs) {
    if (haveSample_ && (nowMs - lastSampleMs_) > cfg_.stallMs) {
        stalled_ = true;
        valid_ = false;
    }
}

} // namespace c5rx
