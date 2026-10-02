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

// Espressif libphy function (Apache-2.0). It is exported by the library but
// not declared in a public header, so it's declared here. Returns the signal
// strength in dBm: the noise floor plus the gain the AGC settled on.
extern "C" int phy_get_rssi(void);

namespace c5rx {

static const char* TAG = "c5rx";

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

    inited_  = true;
    powered_ = true;
    return true;
}

bool C5RadioBackend::canTune(uint16_t mhz) const {
    return c5InRange(mhz) && planTuning(mhz).ok;
}

bool C5RadioBackend::tune(uint16_t mhz) {
    if (!inited_ || !canTune(mhz)) return false;

    // Tune to the nearest 5 GHz Wi-Fi channel. The FPV carrier only has to be
    // inside the receiver's 20 MHz bandwidth, and planTuning() keeps it within
    // 12 MHz of the channel centre.
    C5Tuning plan = planTuning(mhz);
    lastChannel_ = plan.baseChannel;
    errChannel_ = esp_wifi_set_channel((uint8_t)plan.baseChannel, WIFI_SECOND_CHAN_NONE);
    if (errChannel_ != ESP_OK) {
        ESP_LOGE(TAG, "set_channel(%u) failed: %s", plan.baseChannel, esp_err_to_name(errChannel_));
        return false;
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

    // With the AGC on automatic, this follows an analog FPV carrier. Bench
    // figures (C5-Zero, 25 mW VTX on R4): about -45 dBm at 1 m, -12 to -24 dBm
    // at 50 cm, -76 dBm with the VTX off. It refreshes about every 25 ms with
    // a short dip, which the RSSI pipeline's peak-hold removes.
    db = (float)phy_get_rssi();
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
             "  live rssi=%d dBm\n",
             inited_, powered_, tuned_,
             esp_err_to_name(errInit_), esp_err_to_name(errCountry_),
             esp_err_to_name(errStart_), esp_err_to_name(errBand_),
             lastChannel_, esp_err_to_name(errChannel_), prim,
             inited_ ? phy_get_rssi() : 0);
}

} // namespace c5rx
