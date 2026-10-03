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
    bool command(char* const* tok, int n, char* out, int len) override;

private:
    // How tune() reaches a frequency: the nearest Wi-Fi channel, or that
    // channel and then the exact frequency set straight into the PHY. Auto
    // uses the Wi-Fi channel wherever it works, and the PHY elsewhere.
    enum class Method { AUTO, WIFI, PHY };
    enum class P11 { AUTO, ON, OFF };   // 802.11p mode: auto = on from 5750 MHz
    void apply11p(uint16_t mhz);
    bool usePhy(uint16_t mhz) const;
    const char* methodName() const;

    Method   method_  = Method::AUTO;
    bool     phyTuned_ = false;       // the current channel was tuned by the PHY
    P11      p11_     = P11::AUTO;
    int      p11Mode_ = 0;
    bool     hold_    = false;    // re-tune if something moves the radio
    // What to run straight after RFChannelSel (experiment, see EXTENDED_TUNING.md).
    enum class Post { NONE, TWICE, TRACK };
    Post     post_    = Post::TRACK;  // fastest settle above 5885 MHz on the bench
    uint8_t  anchor_  = 0;        // phy method: driver channel, 0 = nearest
    // What readPowerDb() reports (experiment): phy_get_rssi, the sig-RSSI
    // register, or the noise-floor estimate.
    enum class Read { RSSI, SIG, NF };
    Read     read_    = Read::SIG;
    int      sigen_   = -1;       // last phy_check_sigrssi_en argument, -1 = never called
    uint32_t drifts_  = 0;        // times the PHY was found off our frequency
    uint32_t reasserts_ = 0;
    uint16_t lastDriftMhz_ = 0;
    uint32_t lastDriftMs_  = 0;
    bool     inited_  = false;
    bool     powered_ = false;
    uint16_t tuned_   = 0;
    uint32_t pollMs_  = 1;        // read the RSSI at most once per millisecond
    uint32_t lastPollMs_ = 0;
    bool     firstPoll_ = true;
    bool     mode11p_   = false;  // 802.11p mode on for the current channel

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
