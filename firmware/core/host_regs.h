// The values the host gets back when it reads a register over the bus.
//
// Register 0x1 returns the last frequency value written, so FPVGate's
// verifyFrequency() check passes.
//
// Two extra registers let a host that knows about this firmware read the RSSI
// digitally. A real RX5808 doesn't have them.
//   0x7 status: D0-7 RSSI (0 to 255), D8 valid, D9 channel supported,
//               D11-14 state, D15 always 1
//   0x6 info:   D0-7 signal strength in dBm (signed), D8-15 signature 0xC5
#ifndef C5RX_HOST_REGS_H
#define C5RX_HOST_REGS_H

#include <stdint.h>
#include "controller.h"

namespace c5rx {

static const uint8_t REG_EXT_STATUS = 0x7;
static const uint8_t REG_EXT_INFO   = 0x6;
static const uint8_t kC5rxSignature = 0xC5;

// The 20-bit value to send back for a read of register `addr`.
uint32_t buildReadData(const Controller& c, uint8_t addr);

// True if registers 0x6 and 0x7 identify this firmware (for a host driver).
bool looksLikeC5rx(uint32_t infoRegData, uint32_t statusRegData);

} // namespace c5rx

#endif // C5RX_HOST_REGS_H
