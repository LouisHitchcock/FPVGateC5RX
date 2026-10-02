// Saved settings. They're stored in flash as a block of bytes with a magic
// number, a version and a CRC32, so damaged or older data is ignored and the
// defaults are used instead.
#ifndef C5RX_PARAMS_H
#define C5RX_PARAMS_H

#include <stdint.h>
#include <stddef.h>

namespace c5rx {

static const uint32_t kParamMagic   = 0x43355258; // "C5RX"
static const uint16_t kParamVersion = 4;   // v4 removed the forced-gain setting

struct PersistConfig {
    // RSSI calibration and shaping (see RssiPipelineConfig)
    float    dbLo        = -90.0f;
    float    dbHi        = -20.0f;
    float    emaAlpha    = 1.0f;     // 1 = no smoothing
    uint8_t  useMedian3  = 1;
    uint16_t settleMs    = 35;
    uint16_t stallMs     = 200;
    // Analog output (see RssiCodecConfig)
    float    vdd          = 3.3f;
    float    dividerRatio = 0.5f;    // for the 10k/10k filter in HARDWARE.md
    float    vFullScale   = 1.50f;
    float    vFloor       = 0.0f;
    // Frequency to tune at power-up. 0 = wait for the host or the console.
    uint16_t bootMhz = 0;
    // Soft ceiling. A ratio of 1 turns it off.
    float    kneeDb    = -55.0f;
    float    kneeRatio = 1.0f;
};

uint32_t crc32(const uint8_t* data, size_t len);

// Write the settings into buf. Returns the number of bytes, or 0 if buf is too small.
size_t serializeConfig(const PersistConfig& c, uint8_t* buf, size_t cap);
size_t serializedSize();

// Read settings back. Returns false, leaving `out` alone, if the data is
// damaged, from another version, or out of range.
bool deserializeConfig(PersistConfig& out, const uint8_t* buf, size_t len);

} // namespace c5rx

#endif // C5RX_PARAMS_H
