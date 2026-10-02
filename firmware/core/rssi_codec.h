// Works out the sigma-delta setting that produces a given RSSI voltage.
//
// The C5 has no DAC. Its sigma-delta output switches GPIO10 on and off very
// fast, and an RC filter (docs/HARDWARE.md) smooths that into a steady
// voltage. A resistor divider keeps RSSI 255 just under the host's ADC limit.
//
// The ESP-IDF sigma-delta "density" runs from -128 to 127, and the average
// output is Vdd * (density + 128) / 256.
#ifndef C5RX_RSSI_CODEC_H
#define C5RX_RSSI_CODEC_H

#include <stdint.h>
#include <math.h>

namespace c5rx {

struct RssiCodecConfig {
    float vdd          = 3.3f;    // the C5's 3.3 V supply
    // RSSI pin voltage divided by GPIO voltage. With the recommended filter
    // (10k from GPIO10 to the RSSI pin, then 10k and 100 nF from the RSSI pin
    // to ground) it is 10k / (10k + 10k) = 0.5. Use 1.0 with no divider.
    float dividerRatio = 0.5f;
    // RSSI pin voltage for RSSI 255. It can't exceed dividerRatio * vdd
    // (1.65 V here); 1.5 V stays just under the host's ADC limit of about 1.55 V.
    float vFullScale   = 1.50f;
    float vFloor       = 0.0f;    // RSSI pin voltage for RSSI 0
};

class RssiCodec {
public:
    void begin(const RssiCodecConfig& cfg) { cfg_ = cfg; }
    const RssiCodecConfig& config() const { return cfg_; }

    // The RSSI pin voltage wanted for an RSSI value.
    float pinVoltageForCounts(uint8_t counts) const {
        return cfg_.vFloor + (cfg_.vFullScale - cfg_.vFloor) * (counts / 255.0f);
    }

    // The share of time (0 to 1) the output must be on to give that voltage.
    float dutyForCounts(uint8_t counts) const {
        float vSdm = pinVoltageForCounts(counts) / cfg_.dividerRatio;
        float d = vSdm / cfg_.vdd;
        if (d < 0.0f) d = 0.0f;
        if (d > 1.0f) d = 1.0f;
        return d;
    }

    // The sigma-delta density (-128 to 127) for an RSSI value.
    int8_t densityForCounts(uint8_t counts) const {
        float d = dutyForCounts(counts);
        // Round with floor(x + 0.5). A plain (int) cast rounds negative
        // numbers towards zero, which made low voltages slightly high.
        int v = (int)floorf(d * 256.0f - 128.0f + 0.5f);
        if (v < -128) v = -128;
        if (v > 127) v = 127;
        return (int8_t)v;
    }

    // The RSSI pin voltage a density actually gives (used by the tests).
    float pinVoltageForDensity(int8_t density) const {
        float vSdm = cfg_.vdd * (density + 128) / 256.0f;
        return vSdm * cfg_.dividerRatio;
    }

private:
    RssiCodecConfig cfg_;
};

} // namespace c5rx

#endif // C5RX_RSSI_CODEC_H
