// The interface between the firmware and a radio, plus a simulated radio.
//
// Everything above this interface (bus, controller, RSSI pipeline, outputs)
// only talks to RfBackend, so the unit tests can run it all against
// SimRfBackend on a PC.
#ifndef C5RX_RF_BACKEND_H
#define C5RX_RF_BACKEND_H

#include <stdint.h>

namespace c5rx {

class RfBackend {
public:
    virtual ~RfBackend() {}
    // Start the radio. Returns false on failure.
    virtual bool begin() = 0;
    // Tune to `mhz`. Returns false if the frequency can't be tuned.
    virtual bool tune(uint16_t mhz) = 0;
    // Signal strength in dBm. Returns true if `db` holds a new reading.
    virtual bool readPowerDb(float& db, uint32_t nowMs) = 0;
    virtual void powerDown() = 0;
    virtual void wake() = 0;
    virtual const char* name() const = 0;
    // False if `mhz` can never be tuned. The controller then reports "no
    // signal" for that channel instead of treating it as a fault.
    virtual bool canTune(uint16_t mhz) const { (void)mhz; return true; }
    // Status text for the console `diag` command.
    virtual void diag(char* out, int len) const { if (len > 0) out[0] = '\0'; }
};

// A simulated radio for the unit tests: a noise floor plus one transmitter
// whose signal falls off as the tuned frequency moves away from it.
class SimRfBackend : public RfBackend {
public:
    bool begin() override { begun_ = true; tuned_ = 0; return true; }
    bool tune(uint16_t mhz) override;
    bool readPowerDb(float& db, uint32_t nowMs) override;
    void powerDown() override { powered_ = false; }
    void wake() override { powered_ = true; }
    const char* name() const override { return "sim"; }
    bool canTune(uint16_t mhz) const override { return mhz >= lo_ && mhz <= hi_; }

    // Test controls.
    void setNoiseFloorDb(float db) { noiseDb_ = db; }
    void setPollIntervalMs(uint32_t ms) { pollMs_ = ms ? ms : 1; }
    // The simulated transmitter: its frequency and its signal level at that frequency.
    void setCarrier(uint16_t mhz, float powerDb) { carrierMhz_ = mhz; carrierDb_ = powerDb; }
    void clearCarrier() { carrierMhz_ = 0; }
    // Signal lost per MHz of mistuning.
    void setRolloffDbPerMhz(float r) { rolloff_ = r; }
    // Frequencies the simulated radio accepts (the C5's range by default).
    void setRange(uint16_t lo, uint16_t hi) { lo_ = lo; hi_ = hi; }

    uint16_t tunedMhz() const { return tuned_; }

private:
    bool     begun_   = false;
    bool     powered_ = true;
    uint16_t tuned_   = 0;
    uint16_t lo_      = 5180;
    uint16_t hi_      = 5885;
    float    noiseDb_ = -95.0f;
    uint16_t carrierMhz_ = 0;
    float    carrierDb_  = -40.0f;
    float    rolloff_    = 3.0f;    // dB per MHz
    uint32_t pollMs_  = 1;          // a new reading every this many ms
    uint32_t lastPollMs_ = 0;
    bool     firstPoll_ = true;
};

} // namespace c5rx

#endif // C5RX_RF_BACKEND_H
