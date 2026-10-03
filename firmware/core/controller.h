// Connects everything: takes the host's bus commands, tunes the radio, feeds
// readings through the RSSI pipeline and keeps track of the receiver's state.
// It has no clock of its own; callers pass the time in milliseconds.
#ifndef C5RX_CONTROLLER_H
#define C5RX_CONTROLLER_H

#include <stdint.h>
#include "c5rx_types.h"
#include "rf_backend.h"
#include "rssi_pipeline.h"

namespace c5rx {

enum class State : uint8_t {
    BOOT, RF_INIT, IDLE, TUNING, TRACKING, RF_FAULT, POWERDOWN
};

struct ControllerConfig {
    RssiPipelineConfig pipeline;
    uint32_t faultRetryMs = 250;   // wait this long before restarting a failed radio
};

class Controller {
public:
    void begin(RfBackend* rf, const ControllerConfig& cfg);

    // Handle a complete 25-bit word from the host.
    void onHostWord(uint32_t bits25, uint32_t nowMs);

    // Call often (about every millisecond): reads the radio and updates the RSSI.
    void tick(uint32_t nowMs);

    // Tune to a frequency directly (console and boot frequency). Also updates
    // what a host reading register 0x1 gets back.
    void tuneMhz(uint16_t mhz, uint32_t nowMs);

    uint8_t  rssiCounts() const { return pipe_.counts(); }
    bool     rssiValid()  const { return pipe_.valid() && freqSupported_ && powered_; }
    float    rssiDb()     const { return pipe_.smoothedDb(); }
    uint16_t currentMhz() const { return tunedMhz_; }
    bool     freqSupported() const { return freqSupported_; }
    State    state()      const { return state_; }

    // What the host gets back when it reads register 0x1: the last value it
    // wrote, so FPVGate's verifyFrequency() matches.
    uint16_t synthReadback() const { return lastSynthReg_; }

    // For calibration and saved settings.
    RssiPipeline& pipeline() { return pipe_; }
    // For the console's diag and rf commands.
    const RfBackend* backend() const { return rf_; }
    RfBackend*       backend()       { return rf_; }

private:
    void retune(uint16_t mhz, uint32_t nowMs);
    void enterFault(uint32_t nowMs);

    RfBackend*       rf_ = nullptr;
    ControllerConfig cfg_;
    RssiPipeline     pipe_;
    State            state_ = State::BOOT;

    bool     powered_       = true;
    bool     freqSupported_ = false;
    uint16_t tunedMhz_      = 0;
    uint16_t lastSynthReg_  = 0;
    uint32_t faultUntilMs_  = 0;
};

} // namespace c5rx

#endif // C5RX_CONTROLLER_H
