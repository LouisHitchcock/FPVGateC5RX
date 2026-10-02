// The receiving side of the RX5808 3-wire bus, one bit at a time.
//
// The interrupt handlers in main.cpp call start(), pushBit() and popBit() as
// SEL and CLK change. The unit tests drive it the same way with recorded bits.
//
// A frame is 25 bits, least significant first: bits 0 to 3 are the address,
// bit 4 is R/W (1 = write) and bits 5 to 24 are the data.
//   Write: the host sends all 25 bits; we sample DATA on each rising CLK.
//   Read:  the host sends the first 5 bits; we send the other 20 from the
//          addressed register, and the host samples them while CLK is low.
#ifndef C5RX_RX5808_BUS_H
#define C5RX_RX5808_BUS_H

#include <stdint.h>
#include "c5rx_types.h"

namespace c5rx {

class Rx5808Bus {
public:
    // Called on a read to get the 20-bit value of register `addr`.
    typedef uint32_t (*ReadProvider)(void* ctx, uint8_t addr);
    void setReadProvider(ReadProvider fn, void* ctx) { provider_ = fn; pctx_ = ctx; }

    // SEL has gone low: a new frame starts.
    void start() {
        bitIdx_ = 0; acc_ = 0; addr_ = 0; write_ = true;
        outWord_ = 0; haveOut_ = false; done_ = false;
    }

    // Does the host send the next bit? It always sends the first 5, and on a
    // write it sends the data too.
    bool hostDrives() const {
        if (bitIdx_ >= 25) return false;
        if (bitIdx_ < 5) return true;
        return write_;
    }

    // Store a bit sent by the host (sampled on a rising CLK edge).
    void pushBit(bool b) {
        if (bitIdx_ >= 25) return;
        if (b) acc_ |= (1u << bitIdx_);
        ++bitIdx_;
        if (bitIdx_ == 5) {         // address and R/W are now known
            addr_  = (uint8_t)(acc_ & 0x0F);
            write_ = (acc_ >> 4) & 0x1;
            if (!write_) {          // a read: fetch the value to send back
                outWord_ = provider_ ? (provider_(pctx_, addr_) & 0xFFFFF) : 0;
                haveOut_ = true;
            }
        }
        if (bitIdx_ == 25) done_ = true;
    }

    // The next bit to send back during a read.
    bool popBit() {
        if (bitIdx_ < 5 || bitIdx_ >= 25 || write_) return false;
        bool bit = (outWord_ >> (bitIdx_ - 5)) & 0x1;
        ++bitIdx_;
        if (bitIdx_ == 25) done_ = true;
        return bit;
    }

    bool complete() const { return done_; }
    bool isWrite() const { return write_; }
    uint8_t address() const { return addr_; }

    // The received frame, split into fields (the data is only meaningful for writes).
    Rx5808Word word() const {
        Rx5808Word w;
        w.address = addr_;
        w.write   = write_;
        w.data    = (acc_ >> 5) & 0xFFFFF;
        w.valid   = done_;
        return w;
    }

    uint8_t bitIndex() const { return bitIdx_; }

    // The bits the host sent, as one value. For a write this is the whole
    // frame, ready for Controller::onHostWord().
    uint32_t rawBits() const { return acc_ & 0x1FFFFFF; }

private:
    ReadProvider provider_ = nullptr;
    void*    pctx_   = nullptr;
    uint8_t  bitIdx_ = 0;
    uint32_t acc_    = 0;
    uint8_t  addr_   = 0;
    bool     write_  = true;
    uint32_t outWord_ = 0;
    bool     haveOut_ = false;
    bool     done_    = false;
};

} // namespace c5rx

#endif // C5RX_RX5808_BUS_H
