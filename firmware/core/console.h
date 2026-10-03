// The serial console: tune, scan, watch the signal, calibrate and save,
// over USB with no FPVGate needed. Output goes through a callback, so the
// unit tests can run it too.
#ifndef C5RX_CONSOLE_H
#define C5RX_CONSOLE_H

#include <stdint.h>
#include "controller.h"
#include "params.h"

namespace c5rx {

struct ConsoleHooks {
    void* ctx = nullptr;
    void (*write)(void* ctx, const char* s) = nullptr;     // required
    bool (*save)(void* ctx) = nullptr;                     // store the settings
    // Fix the analog output at an RSSI value (0 to 255), or -1 to release it.
    // Returns 0 if the analog output is working, otherwise an error code.
    int  (*forceOutput)(void* ctx, int counts) = nullptr;  // optional
    // Text for the `bus` command: what has arrived from the host.
    void (*busInfo)(void* ctx, char* out, int len) = nullptr;  // optional
};

class Console {
public:
    // `cfg` is the firmware's live settings; the console changes them directly.
    void begin(Controller* ctl, PersistConfig* cfg, const ConsoleHooks& hooks);

    // Pass in each received character. A line runs on newline or CR.
    void feedChar(char c, uint32_t nowMs);
    // Run one command line.
    void execLine(const char* line, uint32_t nowMs);
    // Call every loop: keeps streaming and scanning going.
    void tick(uint32_t nowMs);

    bool     scanning() const { return scanActive_; }
    bool     streaming() const { return streamOn_; }
    uint16_t lastScanPeakMhz() const { return peakMhz_; }
    float    lastScanPeakDb() const { return peakDb_; }

private:
    void out(const char* s);
    void outf(const char* fmt, ...);
    void printStatus();
    void printHelp();
    void tune(uint16_t mhz, uint32_t nowMs);
    void startScan(uint32_t dwellMs, uint32_t nowMs);
    void scanStep(uint32_t nowMs);
    void applyCalibration();
    bool     tunable(uint16_t mhz) const;
    uint16_t scanMhzAt(int i) const;
    int      nextScanIndex(int from) const;

    Controller*    ctl_ = nullptr;
    PersistConfig* cfg_ = nullptr;
    ConsoleHooks   hooks_;

    char line_[80];
    int  lineLen_ = 0;

    // streaming
    bool     streamOn_ = false;
    uint32_t streamPeriodMs_ = 200;
    uint32_t lastStreamMs_ = 0;

    // scan state
    bool     scanActive_ = false;
    int      scanIdx_ = 0;
    uint32_t scanDwellMs_ = 60;
    uint32_t scanPhaseStartMs_ = 0;
    bool     scanSawValid_ = false;
    float    scanMaxDb_ = -200.0f;
    uint16_t scanRestoreMhz_ = 0;
    uint16_t peakMhz_ = 0;
    float    peakDb_ = -200.0f;
    // sweep: a scan over a frequency range instead of the channel table
    uint16_t sweepLo_ = 0;
    uint16_t sweepHi_ = 0;
    uint16_t sweepStep_ = 0;          // 0 = channel-table scan
    bool     rawPending_ = false;     // `raw`: print the capture when it's done
};

} // namespace c5rx

#endif // C5RX_CONSOLE_H
