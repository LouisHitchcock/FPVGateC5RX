// ESP32-C5 radio backend.
//
// Brings the C5's 5 GHz receiver up, tunes it to the FPV channel and reads the
// signal strength. The tuning maths lives in core/c5_freq_map.* so it can be
// unit-tested on a PC.
#ifndef C5RX_C5_RADIO_H
#define C5RX_C5_RADIO_H

#include "../../core/rf_backend.h"

namespace c5rx {

class C5RadioBackend : public RfBackend {
public:
    bool begin() override;
    bool tune(uint16_t mhz) override;
    bool readPowerDb(float& db, uint32_t nowMs) override;
    void powerDown() override;
    void wake() override;
    const char* name() const override { return "c5-radio"; }
    bool canTune(uint16_t mhz) const override;
    void diag(char* out, int len) const override;

private:
    bool     inited_  = false;
    bool     powered_ = false;
    uint16_t tuned_   = 0;
    uint32_t pollMs_  = 1;        // read the RSSI at most once per millisecond
    uint32_t lastPollMs_ = 0;
    bool     firstPoll_ = true;

    // Driver return codes, shown by the console `diag` command.
    int      errInit_    = 0;
    int      errCountry_ = 0;
    int      errBand_    = 0;
    int      errStart_   = 0;
    int      errChannel_ = 0;
    uint16_t lastChannel_ = 0;
};

} // namespace c5rx

#endif // C5RX_C5_RADIO_H
