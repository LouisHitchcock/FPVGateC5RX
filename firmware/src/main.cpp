// FPVGateC5RX: makes an ESP32-C5 behave like an RX5808 receiver module.
//
// FPVGate tunes it over the RX5808's 3-wire bus (SEL, CLK, DATA), which is
// handled here with GPIO interrupts. The signal strength comes back as an
// analog voltage on the RSSI pin, made by the sigma-delta output and an RC
// filter. A serial console allows testing and calibration over USB.
//
// The logic lives in core/ and is unit-tested on a PC. This file connects it
// to the hardware.

#include <Arduino.h>
#include <HWCDC.h>
#include <Preferences.h>
#include <stdarg.h>
#include "driver/sdm.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

#include "board_pins.h"
#include "core/rx5808_bus.h"
#include "core/rx5808_decode.h"
#include "core/controller.h"
#include "core/host_regs.h"
#include "core/rssi_codec.h"
#include "core/params.h"
#include "core/selftest.h"
#include "core/console.h"
#include "rf/c5_radio.h"

using namespace c5rx;

// The C5's built-in USB serial port. The Arduino core only creates HWCDCSerial
// when the board is built with USB-CDC-on-boot, so otherwise make our own.
#if defined(HWCDC_SERIAL_IS_DEFINED)
  #define USB_CON HWCDCSerial
#else
  static HWCDC g_usbCdc;
  #define USB_CON g_usbCdc
#endif

static Rx5808Bus      g_bus;
static Controller     g_ctl;
static RssiCodec      g_codec;
static Preferences    g_prefs;
static Console        g_console;
static C5RadioBackend g_radio;
static PersistConfig  g_cfg;          // the live settings, changed by the console
static sdm_channel_handle_t g_sdm = nullptr;
static esp_err_t      g_sdmErr = ESP_FAIL;
static int            g_outOverride = -1;   // `out` command: fixed RSSI, or -1

// Register values for the host to read. loop() keeps them up to date, so the
// interrupt handler never has to touch the controller.
static volatile uint32_t g_readRegs[16] = {0};

// A complete write from the host, handed from the interrupt to loop().
static volatile uint32_t g_pendingWord  = 0;
static volatile bool     g_pendingValid = false;

// Bus statistics for the `bus` command.
static volatile uint32_t g_busFrames = 0;
static volatile uint32_t g_busWrites = 0;
static volatile uint32_t g_busReads = 0;
static volatile uint32_t g_busShort = 0;       // frames that ended early
static volatile uint8_t  g_busShortBits = 0;
static volatile uint32_t g_busLastWord = 0;
static volatile uint8_t  g_busLastReadAddr = 0;

static uint32_t IRAM_ATTR readProvider(void* /*ctx*/, uint8_t addr) {
    return g_readRegs[addr & 0x0F];
}

// Pin access for the interrupt handlers. Arduino's pinMode() and digitalRead()
// aren't safe in an interrupt on this core, and are too slow for FPVGate's
// read-back, which clocks a bit every 10 us. Espressif's gpio_ll functions are
// direct register accesses.
static inline int IRAM_ATTR pinLevel(int pin) { return gpio_ll_get_level(&GPIO, pin); }
static inline void IRAM_ATTR dataRelease() { gpio_ll_output_disable(&GPIO, PIN_RX5808_DATA); }
static inline void IRAM_ATTR dataDrive(int level) {
    gpio_ll_set_level(&GPIO, PIN_RX5808_DATA, level);   // set the level before enabling
    gpio_ll_output_enable(&GPIO, PIN_RX5808_DATA);
}

static void IRAM_ATTR onSelChange() {
    if (!pinLevel(PIN_RX5808_SEL)) {
        // SEL low: a frame starts, and the host sends the first bits.
        g_bus.start();
        dataRelease();
    } else {
        // SEL high: the frame has ended.
        g_busFrames = g_busFrames + 1;
        if (g_bus.complete()) {
            if (g_bus.isWrite()) {
                g_pendingWord  = g_bus.rawBits();
                g_pendingValid = true;
                g_busLastWord  = g_bus.rawBits();
                g_busWrites = g_busWrites + 1;
            } else {
                g_busLastReadAddr = g_bus.address();
                g_busReads = g_busReads + 1;
            }
        } else if (g_bus.bitIndex() > 0) {
            g_busShort = g_busShort + 1;
            g_busShortBits = g_bus.bitIndex();
        }
        dataRelease();   // always hand DATA back to the host
    }
}

static void IRAM_ATTR onClkChange() {
    if (pinLevel(PIN_RX5808_CLK)) {
        // Rising edge: read the next bit if the host is sending it.
        if (g_bus.hostDrives()) g_bus.pushBit(pinLevel(PIN_RX5808_DATA));
    } else {
        // Falling edge: during a read, put the next bit on DATA so it's ready
        // when the host samples it.
        if (!g_bus.hostDrives() && !g_bus.isWrite() && !g_bus.complete()) {
            dataDrive(g_bus.popBit() ? 1 : 0);
        }
    }
}

// Write to the built-in USB in pieces that fit its send buffer. It only waits
// for room while a PC is connected, and never for more than 50 ms, so an
// unplugged cable can't hold the firmware up.
static void usbWrite(const char* s) {
    size_t n = strlen(s);
    uint32_t start = millis();
    while (n > 0) {
        if (!HWCDC::isConnected()) return;
        int room = USB_CON.availableForWrite();
        if (room <= 0) {
            if (millis() - start > 50) return;
            delay(1);
            continue;
        }
        size_t k = n < (size_t)room ? n : (size_t)room;
        USB_CON.write((const uint8_t*)s, k);
        s += k;
        n -= k;
    }
}

// Serial output goes to both the C5's built-in USB and UART0, because boards
// differ in which one their USB-C connector uses.
static void say(const char* s) {
    Serial0.print(s);
    usbWrite(s);
}

static void sayf(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    say(buf);
}

static void applyConfig(const PersistConfig& pc) {
    ControllerConfig cc;
    cc.pipeline.dbLo = pc.dbLo; cc.pipeline.dbHi = pc.dbHi;
    cc.pipeline.emaAlpha = pc.emaAlpha; cc.pipeline.useMedian3 = pc.useMedian3;
    cc.pipeline.settleMs = pc.settleMs; cc.pipeline.stallMs = pc.stallMs;
    cc.pipeline.kneeDb = pc.kneeDb; cc.pipeline.kneeRatio = pc.kneeRatio;
    g_ctl.begin(&g_radio, cc);

    RssiCodecConfig rc;
    rc.vdd = pc.vdd; rc.dividerRatio = pc.dividerRatio;
    rc.vFullScale = pc.vFullScale; rc.vFloor = pc.vFloor;
    g_codec.begin(rc);
}

static PersistConfig loadConfig() {
    PersistConfig pc;   // defaults, used if nothing valid is stored
    g_prefs.begin("c5rx", true);
    size_t len = g_prefs.getBytesLength("cfg");
    if (len && len <= 64) {
        uint8_t buf[64];
        g_prefs.getBytes("cfg", buf, len);
        PersistConfig loaded;
        if (deserializeConfig(loaded, buf, len)) pc = loaded;
    }
    g_prefs.end();
    return pc;
}

// ---- console hooks ----
static void consoleWrite(void* /*ctx*/, const char* s) { say(s); }

static bool consoleSave(void* /*ctx*/) {
    uint8_t buf[64];
    size_t n = serializeConfig(g_cfg, buf, sizeof(buf));
    if (!n) return false;
    if (!g_prefs.begin("c5rx", false)) return false;
    size_t written = g_prefs.putBytes("cfg", buf, n);
    g_prefs.end();
    return written == n;
}

static int consoleForceOutput(void* /*ctx*/, int counts) {
    g_outOverride = counts;
    return (g_sdm && g_sdmErr == ESP_OK) ? 0 : (int)g_sdmErr;
}

static void consoleBusInfo(void* /*ctx*/, char* out, int len) {
    Rx5808Word pw = parseWord(g_busLastWord);
    uint16_t mhz = (pw.address == REG_SYNTH_RF) ? synthRegToMhz((uint16_t)(pw.data & 0xFFFF)) : 0;
    snprintf(out, len,
             "bus: frames=%lu writes=%lu reads=%lu short=%lu (last short: %u bits)\n"
             "     last write: addr=0x%X data=0x%05lX%s%u%s  last read addr=0x%X\n"
             "     pins SEL=GPIO%d(%d) CLK=GPIO%d(%d) DATA=GPIO%d(%d)\n",
             (unsigned long)g_busFrames, (unsigned long)g_busWrites,
             (unsigned long)g_busReads, (unsigned long)g_busShort, g_busShortBits,
             pw.address, (unsigned long)pw.data, mhz ? " = " : "", mhz, mhz ? " MHz" : "",
             g_busLastReadAddr,
             PIN_RX5808_SEL, pinLevel(PIN_RX5808_SEL), PIN_RX5808_CLK, pinLevel(PIN_RX5808_CLK),
             PIN_RX5808_DATA, pinLevel(PIN_RX5808_DATA));
}

// ---- analog RSSI output ----
static void setupSdm() {
    sdm_config_t cfg = {};
    cfg.gpio_num = (gpio_num_t)PIN_RSSI_SDM;
    cfg.clk_src  = SDM_CLK_SRC_DEFAULT;
    cfg.sample_rate_hz = 4 * 1000 * 1000;   // 4 MHz is easy for the RC filter to smooth
    g_sdmErr = sdm_new_channel(&cfg, &g_sdm);
    if (g_sdmErr == ESP_OK) g_sdmErr = sdm_channel_enable(g_sdm);
    if (g_sdmErr == ESP_OK) sdm_channel_set_pulse_density(g_sdm, -128);  // start at 0 V
}

static void updateAnalogOut(uint8_t counts) {
    if (g_outOverride >= 0) counts = (uint8_t)g_outOverride;
    if (g_sdm && g_sdmErr == ESP_OK)
        sdm_channel_set_pulse_density(g_sdm, g_codec.densityForCounts(counts));
}

static void refreshReadRegs() {
    g_readRegs[REG_SYNTH_RF]   = buildReadData(g_ctl, REG_SYNTH_RF);
    g_readRegs[REG_EXT_STATUS] = buildReadData(g_ctl, REG_EXT_STATUS);
    g_readRegs[REG_EXT_INFO]   = buildReadData(g_ctl, REG_EXT_INFO);
}

void setup() {
    Serial0.begin(115200);
    USB_CON.setTxTimeoutMs(0);
    USB_CON.begin();
    delay(50);
    say("FPVGateC5RX boot\n");

    pinMode(PIN_RX5808_SEL, INPUT_PULLUP);
    pinMode(PIN_RX5808_CLK, INPUT);
    // DATA goes both ways. Set it up once as an output so it's routed as a
    // plain GPIO, then switch the driver off. The interrupt handlers only
    // turn the driver on and off after that.
    pinMode(PIN_RX5808_DATA, OUTPUT);
    digitalWrite(PIN_RX5808_DATA, LOW);
    gpio_ll_input_enable(&GPIO, PIN_RX5808_DATA);
    gpio_ll_output_disable(&GPIO, PIN_RX5808_DATA);

    SelfTestResult st = runSelfTest();
    sayf("selftest: 0x%02X %s\n", st.passedBits, st.pass() ? "PASS" : "FAIL");
    if (!st.pass()) say("WARNING: self-test failed\n");

    setupSdm();
    sayf("analog out: GPIO%d %s\n", PIN_RSSI_SDM,
         g_sdmErr == ESP_OK ? "running" : esp_err_to_name(g_sdmErr));

    g_bus.setReadProvider(readProvider, nullptr);
    g_cfg = loadConfig();
    applyConfig(g_cfg);

    ConsoleHooks hooks;
    hooks.write       = consoleWrite;
    hooks.save        = consoleSave;
    hooks.forceOutput = consoleForceOutput;
    hooks.busInfo     = consoleBusInfo;
    g_console.begin(&g_ctl, &g_cfg, hooks);

    // Tune the saved boot frequency, if there is one. FPVGate's commands
    // override it as soon as they arrive.
    if (g_cfg.bootMhz) {
        g_ctl.tuneMhz(g_cfg.bootMhz, millis());
        sayf("boot frequency %u MHz\n", g_cfg.bootMhz);
    }
    refreshReadRegs();
    say(g_ctl.state() == State::RF_FAULT ? "radio: failed to start (try 'diag')\n"
                                         : "radio: ready\n");

    attachInterrupt(digitalPinToInterrupt(PIN_RX5808_SEL), onSelChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_RX5808_CLK), onClkChange, CHANGE);

    say("type 'help' for commands\n");
}

void loop() {
    uint32_t now = millis();

    // A complete write from FPVGate.
    if (g_pendingValid) {
        noInterrupts();
        uint32_t w = g_pendingWord;
        g_pendingValid = false;
        interrupts();
        g_ctl.onHostWord(w, now);
        Rx5808Word pw = parseWord(w);
        if (pw.write && pw.address == REG_SYNTH_RF) {
            sayf("host: tune %u MHz (reg 0x%04lX)%s\n", g_ctl.currentMhz(),
                 (unsigned long)(pw.data & 0xFFFF), g_ctl.freqSupported() ? "" : ", outside the C5's range");
        } else if (pw.write) {
            sayf("host: write reg 0x%X = 0x%05lX\n", pw.address, (unsigned long)pw.data);
        }
    }

    // Console input, from either serial port.
    while (Serial0.available() > 0) g_console.feedChar((char)Serial0.read(), now);
    while (USB_CON.available() > 0) g_console.feedChar((char)USB_CON.read(), now);

    g_ctl.tick(now);
    g_console.tick(now);
    updateAnalogOut(g_ctl.rssiCounts());
    refreshReadRegs();

    delayMicroseconds(500);   // run the loop at roughly 1 kHz
}
