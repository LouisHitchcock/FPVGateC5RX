#include "controller.h"
#include "rx5808_decode.h"
#include "fpv_channels.h"

namespace c5rx {

void Controller::begin(RfBackend* rf, const ControllerConfig& cfg) {
    rf_ = rf;
    cfg_ = cfg;
    pipe_.begin(cfg_.pipeline);
    state_ = State::RF_INIT;
    powered_ = true;
    freqSupported_ = false;
    tunedMhz_ = 0;
    lastSynthReg_ = 0;
    faultUntilMs_ = 0;
    if (!rf_ || !rf_->begin()) {
        state_ = State::RF_FAULT;
        faultUntilMs_ = cfg_.faultRetryMs;
    } else {
        state_ = State::IDLE;
    }
}

void Controller::retune(uint16_t mhz, uint32_t nowMs) {
    tunedMhz_ = mhz;
    freqSupported_ = rf_ && rf_->canTune(mhz);
    if (!powered_) { rf_->wake(); powered_ = true; }

    if (!freqSupported_) {
        // A channel the C5 can't receive: accept the command, so the read-back
        // still matches, but report no signal.
        pipe_.onTune(nowMs);
        state_ = State::TRACKING;
        return;
    }
    if (rf_ && rf_->tune(mhz)) {
        pipe_.onTune(nowMs);
        state_ = State::TUNING;
    } else {
        enterFault(nowMs);
    }
}

void Controller::tuneMhz(uint16_t mhz, uint32_t nowMs) {
    lastSynthReg_ = mhzToSynthReg(mhz);
    if (state_ == State::RF_FAULT) {
        tunedMhz_ = mhz;               // tick() applies it when the radio recovers
        freqSupported_ = rf_ ? rf_->canTune(mhz) : c5InRange(mhz);
        return;
    }
    retune(mhz, nowMs);
}

void Controller::enterFault(uint32_t nowMs) {
    state_ = State::RF_FAULT;
    faultUntilMs_ = nowMs + cfg_.faultRetryMs;
}

void Controller::onHostWord(uint32_t bits25, uint32_t nowMs) {
    Rx5808Word w = parseWord(bits25);
    if (!w.valid) return;

    switch (w.address) {
        case REG_SYNTH_RF: {
            if (w.write) {
                lastSynthReg_ = (uint16_t)(w.data & 0xFFFF);
                uint16_t rawMhz = synthRegToMhz(lastSynthReg_);
                uint16_t mhz = normaliseFrequency(rawMhz);
                retune(mhz, nowMs);
            }
            // Reads are answered by the bus code, using synthReadback().
            break;
        }
        case REG_POWER: {
            if (w.write) {
                // FPVGate powers the RX5808 down by writing all ones.
                if ((w.data & 0xFFFFF) == 0xFFFFF) {
                    powered_ = false;
                    if (rf_) rf_->powerDown();
                    state_ = State::POWERDOWN;
                } else {
                    if (!powered_) { if (rf_) rf_->wake(); powered_ = true; }
                }
            }
            break;
        }
        case REG_STATE: {
            if (w.write) {
                // Reset: restart the radio and retune.
                if (rf_ && rf_->begin()) {
                    if (tunedMhz_) retune(tunedMhz_, nowMs);
                    else state_ = State::IDLE;
                } else {
                    enterFault(nowMs);
                }
            }
            break;
        }
        default:
            // Other registers need no action.
            break;
    }
}

void Controller::tick(uint32_t nowMs) {
    if (state_ == State::RF_FAULT) {
        if (nowMs >= faultUntilMs_) {
            if (rf_ && rf_->begin()) {
                state_ = State::IDLE;
                if (tunedMhz_) retune(tunedMhz_, nowMs);
            } else {
                faultUntilMs_ = nowMs + cfg_.faultRetryMs;
            }
        }
        pipe_.tick(nowMs);
        return;
    }

    if (!powered_ || !freqSupported_ || tunedMhz_ == 0) {
        pipe_.tick(nowMs);
        return;
    }

    float db;
    if (rf_ && rf_->readPowerDb(db, nowMs)) {
        pipe_.onSample(db, nowMs);
        if (state_ == State::TUNING && pipe_.valid()) {
            state_ = State::TRACKING;
        }
    }
    pipe_.tick(nowMs);
}

} // namespace c5rx
