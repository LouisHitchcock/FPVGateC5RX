// Turns signal strength readings (dBm) into the 0 to 255 RSSI value FPVGate
// expects from an RX5808.
#ifndef C5RX_RSSI_PIPELINE_H
#define C5RX_RSSI_PIPELINE_H

#include <stdint.h>

namespace c5rx {

struct RssiPipelineConfig {
    // Calibration: dbLo reads as 0, dbHi reads as 255.
    // On the bench, the VTX off read -75 to -97 dBm, and a 25 mW VTX at
    // 0.3 to 3 m read -16 to -59 dBm.
    float    dbLo        = -90.0f;
    float    dbHi        = -20.0f;
    // Smoothing: out += alpha * (in - out). 1 means no smoothing, which is the
    // default because any smoothing visibly rounded the edges on the bench.
    float    emaAlpha    = 1.0f;
    bool     useMedian3  = true;     // drop single-sample glitches
    // Minimum of the last preMin raw readings, before the peak-hold. Any
    // upward burst shorter than this many readings is removed entirely. On
    // in-band channels, background Wi-Fi shows as spikes of mostly 1 to 3 ms
    // (occasionally longer) about every 24 ms, which the 30 ms peak-hold
    // would otherwise hold continuously. On the bench (R5, VTX off) 4 left
    // FPVGate at 0 but the C5 output above 0 3.5% of the time; 8 removed it
    // completely. A VTX's own dips (1 to 6 ms) come out 7 ms wider, still
    // inside the peak-hold, and it reads about 1.5 dB lower. Costs 7 ms on a
    // rise. 1 = off, up to kPreMinCap.
    uint8_t  preMin      = 8;
    // Peak-hold: output the highest reading of the last windowMaxMs ms. The
    // C5's reading dips about 10 dB for 2 to 3 ms every 25 ms or so, when the
    // AGC refreshes. A window longer than that cycle hides the dips. Rises come
    // through straight away; falls are delayed by up to this long. 0 = off.
    uint32_t windowMaxMs = 30;
    // Soft ceiling: above kneeDb, every kneeRatio dB of extra signal only
    // counts as 1 dB. Strong signals get squeezed together but still peak,
    // a bit like an RX5808 that saturates. kneeRatio of 1 turns it off.
    // dbHi still reads as 255 when it's on.
    float    kneeDb      = -55.0f;
    float    kneeRatio   = 1.0f;
    uint32_t settleMs    = 35;       // hold the output this long after retuning
    uint32_t stallMs     = 200;      // no reading for this long counts as stalled
};

class RssiPipeline {
public:
    void begin(const RssiPipelineConfig& cfg);
    void reset();

    // Add a new reading (dBm), timestamped in milliseconds.
    void onSample(float db, uint32_t nowMs);
    // Call after retuning: holds the output while the radio settles.
    void onTune(uint32_t nowMs);
    // Call regularly, even with no readings, so stalls get noticed.
    void tick(uint32_t nowMs);

    uint8_t counts() const { return counts_; }     // 0 to 255
    float   smoothedDb() const { return emaDb_; }
    bool    valid() const { return valid_; }        // false while settling or stalled
    bool    stalled() const { return stalled_; }

    const RssiPipelineConfig& config() const { return cfg_; }
    void setCalibration(float dbLo, float dbHi);
    void setEmaAlpha(float a) { if (a > 0.0f && a <= 1.0f) cfg_.emaAlpha = a; }
    void setWindowMaxMs(uint32_t ms) { cfg_.windowMaxMs = ms > kWinCap ? kWinCap : ms; winCount_ = 0; }
    void setKnee(float db, float ratio) { cfg_.kneeDb = db; cfg_.kneeRatio = ratio < 1.0f ? 1.0f : ratio; }
    void setPreMin(uint8_t n);

    static const uint32_t kWinCap = 200;   // longest peak-hold window, ms
    static const uint8_t  kPreMinCap = 31; // longest pre-minimum

private:
    uint8_t mapCounts(float db) const;
    float   compress(float db) const;
    float   windowMax(float db, uint32_t nowMs);
    float   preMin(float db);

    float    pm_[kPreMinCap];
    int      pmIdx_ = 0;
    int      pmCount_ = 0;

    // Recent readings for the peak-hold, with their timestamps.
    float    winDb_[kWinCap];
    uint32_t winT_[kWinCap];
    int      winHead_ = 0;
    int      winCount_ = 0;

    RssiPipelineConfig cfg_;
    float    med_[3]   = {0,0,0};
    int      medCount_ = 0;
    int      medIdx_   = 0;
    float    emaDb_    = -100.0f;
    bool     haveEma_  = false;
    uint8_t  counts_   = 0;
    uint8_t  heldCounts_ = 0;      // value held during blanking
    bool     valid_    = false;
    bool     stalled_  = false;
    uint32_t lastSampleMs_ = 0;
    uint32_t blankUntilMs_ = 0;
    bool     haveSample_   = false;
};

} // namespace c5rx

#endif // C5RX_RSSI_PIPELINE_H
