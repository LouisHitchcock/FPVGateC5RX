#include "c5_radio.h"

#include "../../core/c5_freq_map.h"
#include "../../core/fpv_channels.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32-hal.h"   // temperatureRead()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Espressif libphy function (Apache-2.0). It is exported by the library but
// not declared in a public header, so it's declared here. Returns the signal
// strength in dBm: the noise floor plus the gain the AGC settled on.
extern "C" int phy_get_rssi(void);

// Also from libphy, also undeclared. Switches the PHY's 802.11p mode on or
// off. While it's on, libphy re-applies it on every channel change; turning
// it off needs its own call. Reported to help from 5750 to 5990 MHz, and on
// the bench (R8) about 14% less noise with it on.
extern "C" void phy_11p_set(int enable, int mode);

// Also from libphy, also undeclared. Tunes the radio. Above channel 14 the
// PHY takes `chan` as the frequency in MHz, any value, not a Wi-Fi channel
// number, so this reaches frequencies the Wi-Fi driver won't (R8, E6-E8, the
// L band). `bw` 0 is 20 MHz.
extern "C" void RFChannelSel(int chan, int bw);

// libphy's state block. phy_chip_set_chan stores the frequency it tuned, in
// MHz, as a uint16 at this offset. Read only, to see who last tuned the radio.
extern "C" uint8_t phy_param[];
static const int kPhyParamFreqOffset = 288;

// Also from libphy. ESP-IDF's PHY timer calls this about once a second
// (wifi=1, bt=0): I2C and temperature calibration tracking.
extern "C" void phy_param_track_tot(int wifi, int bt);

// Also from libphy (experiment). phy_get_rssi() is the noise-floor estimate
// / 4 plus an AGC gain byte that only changes when the receiver detects
// something, so with nothing to detect it holds its last value.
// phy_get_sigrssi() reads that same byte, signed. phy_check_sigrssi_en()
// rewrites a block of AGC registers ("signal RSSI" mode). Which argument
// turns it on is untested.
extern "C" int  phy_get_sigrssi(void);
extern "C" void phy_check_sigrssi_en(int en);
extern "C" int  phy_get_noise_floor(void);

namespace c5rx {

static uint16_t phyFreqMhz() {
    uint16_t f;
    memcpy(&f, phy_param + kPhyParamFreqOffset, sizeof(f));
    return f;
}

static const char* TAG = "c5rx";

// 11p "auto" switches it on from here up.
static const uint16_t k11pMinMhz = 5750;

// What `rf method phy` accepts. Experimental: see docs/EXTENDED_TUNING.md.
static const uint16_t kPhyMinMhz = 4900;
static const uint16_t kPhyMaxMhz = 6000;

bool C5RadioBackend::begin() {
    if (inited_) return true;

    // Start Wi-Fi in station mode but never connect or scan. That powers the
    // receiver, which is all we need.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    errInit_ = esp_wifi_init(&cfg);
    if (errInit_ != ESP_OK) { ESP_LOGE(TAG, "wifi_init failed"); return false; }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;

    // Allow every 5 GHz channel the C5 supports (36 to 177). The default
    // "world safe" country setting blocks some of them. The firmware only
    // receives, so nothing is ever transmitted on these channels.
    wifi_country_t country = {};
    country.cc[0] = '0'; country.cc[1] = '1'; country.cc[2] = ' ';
    country.schan = 1;
    country.nchan = 13;
    country.max_tx_power = 20;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    country.wifi_5g_channel_mask = 0x1FFFFFFE;   // bits 1 to 28 = channels 36 to 177
    errCountry_ = esp_wifi_set_country(&country);

    errStart_ = esp_wifi_start();
    if (errStart_ != ESP_OK) { ESP_LOGE(TAG, "wifi_start failed"); return false; }

    // 5 GHz only, so a channel number is never taken as a 2.4 GHz channel.
    errBand_ = esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY);

    // Promiscuous mode keeps the receiver running all the time.
    esp_wifi_set_promiscuous(true);

    // Signal-RSSI mode: the RSSI register then measures all the time, instead
    // of only updating when the receiver detects a packet. Without it the
    // reading freezes whenever there's nothing to detect (R8, quiet channels).
    // Bench, R8 at 1 m: about -48 dBm with the VTX on, -94 off.
    phy_check_sigrssi_en(1);
    sigen_ = 1;

    inited_  = true;
    powered_ = true;
    return true;
}

static bool wifiCanTune(uint16_t mhz) { return c5InRange(mhz) && planTuning(mhz).ok; }
static bool phyCanTune(uint16_t mhz)  { return mhz >= kPhyMinMhz && mhz <= kPhyMaxMhz; }

bool C5RadioBackend::canTune(uint16_t mhz) const {
    if (method_ == Method::WIFI) return wifiCanTune(mhz);
    return phyCanTune(mhz);
}

// Auto keeps the Wi-Fi method wherever it works, and only uses the phy
// method for frequencies the driver can't reach (R8, E6-E8, the L band).
bool C5RadioBackend::usePhy(uint16_t mhz) const {
    if (method_ == Method::AUTO) return !wifiCanTune(mhz);
    return method_ == Method::PHY;
}

void C5RadioBackend::apply11p(uint16_t mhz) {
    bool want = p11_ == P11::ON || (p11_ == P11::AUTO && mhz >= k11pMinMhz);
    if (want) phy_11p_set(1, p11Mode_);
    else if (mode11p_) phy_11p_set(0, 0);   // only undo it if it was on
    mode11p_ = want;
}

bool C5RadioBackend::tune(uint16_t mhz) {
    if (!inited_ || !canTune(mhz)) return false;

    // Tune to the nearest 5 GHz Wi-Fi channel. In the wifi method that's
    // all: the FPV carrier only has to be inside the receiver's 20 MHz
    // bandwidth, and planTuning() keeps it within 12 MHz of the centre. The
    // phy method then moves the radio onto the exact frequency, with the
    // driver still on a nearby channel.
    bool phy = usePhy(mhz);
    C5Tuning plan = planTuning(mhz, phy ? 0x7FFF : 12);
    if (phy && anchor_) plan.baseChannel = anchor_;
    lastChannel_ = plan.baseChannel;
    apply11p(mhz);
    errChannel_ = esp_wifi_set_channel((uint8_t)plan.baseChannel, WIFI_SECOND_CHAN_NONE);
    if (errChannel_ != ESP_OK) {
        ESP_LOGE(TAG, "set_channel(%u) failed: %s", plan.baseChannel, esp_err_to_name(errChannel_));
        return false;
    }
    phyTuned_ = phy;
    if (phy) {
        RFChannelSel(mhz, 0);
        if (post_ == Post::TWICE) RFChannelSel(mhz, 0);
        else if (post_ == Post::TRACK) phy_param_track_tot(1, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(2));   // let the PLL settle

    tuned_ = mhz;
    firstPoll_ = true;
    if (!powered_) wake();
    return true;
}

bool C5RadioBackend::readPowerDb(float& db, uint32_t nowMs) {
    if (!inited_ || !powered_ || tuned_ == 0) return false;
    if (!firstPoll_ && (nowMs - lastPollMs_) < pollMs_) return false;
    firstPoll_ = false;
    lastPollMs_ = nowMs;

    // In the phy method, the Wi-Fi driver doesn't know the radio moved. Watch
    // for anything tuning it back, and with `rf hold on`, undo it.
    if (phyTuned_) {
        uint16_t f = phyFreqMhz();
        if (f != tuned_) {
            if (f != lastDriftMhz_ || nowMs - lastDriftMs_ > 1000) ++drifts_;
            lastDriftMhz_ = f;
            lastDriftMs_ = nowMs;
            if (hold_) { RFChannelSel(tuned_, 0); ++reasserts_; }
            return false;   // this reading is from the wrong frequency
        }
    }

    // With the AGC on automatic, this follows an analog FPV carrier. Bench
    // figures (C5-Zero, 25 mW VTX on R4): about -45 dBm at 1 m, -12 to -24 dBm
    // at 50 cm, -76 dBm with the VTX off. It refreshes about every 25 ms with
    // a short dip, which the RSSI pipeline's peak-hold removes.
    switch (read_) {
        case Read::SIG: db = (float)phy_get_sigrssi(); break;
        case Read::NF:  db = (float)(phy_get_noise_floor() >> 2); break;
        default:        db = (float)phy_get_rssi(); break;
    }
    return true;
}

void C5RadioBackend::powerDown() {
    if (inited_ && powered_) {
        esp_wifi_set_promiscuous(false);
        powered_ = false;
    }
}

void C5RadioBackend::wake() {
    if (inited_ && !powered_) {
        esp_wifi_set_promiscuous(true);
        powered_ = true;
    }
}

void C5RadioBackend::diag(char* out, int len) const {
    uint8_t prim = 0;
    wifi_second_chan_t sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&prim, &sec);
    snprintf(out, len,
             "radio: started=%d powered=%d tuned=%u MHz\n"
             "  wifi init=%s country=%s start=%s band=%s\n"
             "  last channel set: %u (%s), driver reports channel %u\n"
             "  method=%s, this channel tuned by %s, 11p=%s hold=%d\n"
             "  phy freq=%u MHz, retuned behind us %lu times (last to %u MHz, %lu ms ago), reasserted %lu\n"
             "  live rssi=%d dBm, sigrssi=%d, noise floor/4=%d, chip %.1f C\n",
             inited_, powered_, tuned_,
             esp_err_to_name(errInit_), esp_err_to_name(errCountry_),
             esp_err_to_name(errStart_), esp_err_to_name(errBand_),
             lastChannel_, esp_err_to_name(errChannel_), prim,
             methodName(), phyTuned_ ? "phy" : "wifi", mode11p_ ? "on" : "off", hold_ ? 1 : 0,
             inited_ ? phyFreqMhz() : 0, (unsigned long)drifts_, lastDriftMhz_,
             (unsigned long)(lastDriftMs_ ? lastPollMs_ - lastDriftMs_ : 0),
             (unsigned long)reasserts_,
             inited_ ? phy_get_rssi() : 0, inited_ ? phy_get_sigrssi() : 0,
             inited_ ? (phy_get_noise_floor() >> 2) : 0, temperatureRead());
}

const char* C5RadioBackend::methodName() const {
    return method_ == Method::AUTO ? "auto" : method_ == Method::PHY ? "phy" : "wifi";
}

// rf                         show the settings
// rf method auto|wifi|phy    how to tune (see tune() and usePhy())
// rf 11p auto|on|off [0|1]   802.11p mode, and the mode argument
bool C5RadioBackend::command(char* const* tok, int n, char* out, int len) {
    const char* err = nullptr;
    if (n >= 1 && !strcasecmp(tok[0], "method")) {
        if (n >= 2 && !strcasecmp(tok[1], "auto"))      method_ = Method::AUTO;
        else if (n >= 2 && !strcasecmp(tok[1], "wifi")) method_ = Method::WIFI;
        else if (n >= 2 && !strcasecmp(tok[1], "phy"))  method_ = Method::PHY;
        else err = "usage: rf method auto|wifi|phy\n";
    } else if (n >= 1 && !strcasecmp(tok[0], "11p")) {
        if (n >= 2 && !strcasecmp(tok[1], "auto"))     p11_ = P11::AUTO;
        else if (n >= 2 && !strcasecmp(tok[1], "on"))  p11_ = P11::ON;
        else if (n >= 2 && !strcasecmp(tok[1], "off")) p11_ = P11::OFF;
        else err = "usage: rf 11p auto|on|off [0|1]\n";
        if (!err && n >= 3) p11Mode_ = atoi(tok[2]) ? 1 : 0;
    } else if (n >= 1 && !strcasecmp(tok[0], "hold")) {
        if (n >= 2 && !strcasecmp(tok[1], "on"))       hold_ = true;
        else if (n >= 2 && !strcasecmp(tok[1], "off")) hold_ = false;
        else err = "usage: rf hold on|off\n";
    } else if (n >= 1 && !strcasecmp(tok[0], "post")) {
        if (n >= 2 && !strcasecmp(tok[1], "none"))       post_ = Post::NONE;
        else if (n >= 2 && !strcasecmp(tok[1], "twice")) post_ = Post::TWICE;
        else if (n >= 2 && !strcasecmp(tok[1], "track")) post_ = Post::TRACK;
        else err = "usage: rf post none|twice|track\n";
    } else if (n >= 1 && !strcasecmp(tok[0], "anchor")) {
        // Phy method only: the Wi-Fi channel to park the driver on, instead
        // of the nearest one. "auto" = nearest.
        int ch = (n >= 2 && strcasecmp(tok[1], "auto")) ? atoi(tok[1]) : 0;
        bool known = ch == 0;
        for (int i = 0; i < c5ChannelCount() && !known; ++i) known = c5ChannelAt(i) == ch;
        if (n >= 2 && known) anchor_ = (uint8_t)ch;
        else err = "usage: rf anchor auto|<5 GHz Wi-Fi channel>\n";
    } else if (n >= 1 && !strcasecmp(tok[0], "read")) {
        if (n >= 2 && !strcasecmp(tok[1], "rssi"))     read_ = Read::RSSI;
        else if (n >= 2 && !strcasecmp(tok[1], "sig")) read_ = Read::SIG;
        else if (n >= 2 && !strcasecmp(tok[1], "nf"))  read_ = Read::NF;
        else err = "usage: rf read rssi|sig|nf\n";
    } else if (n >= 1 && !strcasecmp(tok[0], "sigen")) {
        if (n >= 2) { sigen_ = atoi(tok[1]); phy_check_sigrssi_en(sigen_); }
        else err = "usage: rf sigen <0|1>\n";
    } else if (n >= 1 && strcasecmp(tok[0], "help")) {
        err = "usage: rf | rf method auto|wifi|phy | rf 11p auto|on|off [0|1] | rf hold on|off\n"
              "       rf post none|twice|track | rf anchor auto|<channel>\n"
              "       rf read rssi|sig|nf | rf sigen <0|1>\n";
    }
    if (err) { snprintf(out, len, "%s", err); return true; }

    static const char* const k11pNames[] = {"auto", "on", "off"};
    bool wide = method_ != Method::WIFI;
    static const char* const kPostNames[] = {"none", "twice", "track"};
    static const char* const kReadNames[] = {"rssi", "sig", "nf"};
    snprintf(out, len,
             "rf method=%s (tunes %u-%u MHz) 11p=%s mode=%d hold=%s post=%s anchor=%u\n"
             "  read=%s sigen=%d\n"
             "  (not saved: the radio starts with method auto, 11p auto, hold off, post track, anchor auto,\n"
             "   read sig, sigen 1)\n",
             methodName(), wide ? kPhyMinMhz : kC5MinMhz,
             wide ? kPhyMaxMhz : kC5MaxMhz, k11pNames[(int)p11_], p11Mode_,
             hold_ ? "on" : "off", kPostNames[(int)post_], anchor_,
             kReadNames[(int)read_], sigen_);
    return true;
}

} // namespace c5rx
