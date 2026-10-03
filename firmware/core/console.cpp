#include "console.h"
#include "fpv_channels.h"
#include "selftest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace c5rx {

namespace {

const char* stateName(State s) {
    switch (s) {
        case State::BOOT:      return "BOOT";
        case State::RF_INIT:   return "RF_INIT";
        case State::IDLE:      return "IDLE";
        case State::TUNING:    return "TUNING";
        case State::TRACKING:  return "TRACKING";
        case State::RF_FAULT:  return "RF_FAULT";
        case State::POWERDOWN: return "POWERDOWN";
    }
    return "?";
}

// Split `s` (modified in place) on spaces into up to `max` tokens.
int tokenize(char* s, char* tok[], int max) {
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t') ++s;
        if (!*s) break;
        tok[n++] = s;
        while (*s && *s != ' ' && *s != '\t') ++s;
        if (*s) *s++ = '\0';
    }
    return n;
}

void lower(char* s) { for (; *s; ++s) *s = (char)tolower((unsigned char)*s); }

// Parse "R1", "f4", "A8" etc. into a channel's MHz. 0 if not a channel.
uint16_t parseBandChannel(const char* t) {
    if (!t[0] || !t[1] || t[2]) return 0;
    char band = (char)toupper((unsigned char)t[0]);
    int idx = t[1] - '0';
    if (idx < 1 || idx > 8) return 0;
    int n; const FpvChannel* tbl = fpvChannelTable(n);
    for (int i = 0; i < n; ++i)
        if (tbl[i].band == band && tbl[i].index == idx) return tbl[i].mhz;
    return 0;
}

// Label for a frequency, e.g. "R1" (first matching channel), or "--".
void channelLabel(uint16_t mhz, char out[4]) {
    const FpvChannel* c = snapToChannel(mhz, 0);
    if (c) { out[0] = c->band; out[1] = (char)('0' + c->index); out[2] = 0; }
    else   { out[0] = '-'; out[1] = '-'; out[2] = 0; }
}

} // namespace

bool Console::tunable(uint16_t mhz) const {
    const RfBackend* rf = ctl_->backend();
    return rf ? rf->canTune(mhz) : c5InRange(mhz);
}

// Step `i` of a scan or sweep, in MHz, or 0 once past the end. A scan is
// every channel in the table, a sweep every `step` MHz from lo to hi.
uint16_t Console::scanMhzAt(int i) const {
    if (sweepStep_) {
        uint32_t mhz = (uint32_t)sweepLo_ + (uint32_t)i * sweepStep_;
        return mhz <= sweepHi_ ? (uint16_t)mhz : 0;
    }
    int n; const FpvChannel* tbl = fpvChannelTable(n);
    return (i >= 0 && i < n) ? tbl[i].mhz : 0;
}

// The next step from `from` on that can be tuned, skipping channels that
// repeat an earlier frequency. -1 at the end.
int Console::nextScanIndex(int from) const {
    for (int i = from; ; ++i) {
        uint16_t mhz = scanMhzAt(i);
        if (!mhz) return -1;
        if (!tunable(mhz)) continue;
        bool repeat = false;
        for (int j = 0; j < i && !sweepStep_; ++j) repeat = repeat || scanMhzAt(j) == mhz;
        if (!repeat) return i;
    }
}

void Console::begin(Controller* ctl, PersistConfig* cfg, const ConsoleHooks& hooks) {
    ctl_ = ctl;
    cfg_ = cfg;
    hooks_ = hooks;
    lineLen_ = 0;
    streamOn_ = false;
    scanActive_ = false;
}

void Console::out(const char* s) {
    if (hooks_.write) hooks_.write(hooks_.ctx, s);
}

void Console::outf(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    out(buf);
}

void Console::feedChar(char c, uint32_t nowMs) {
    if (c == '\r' || c == '\n') {
        if (lineLen_ > 0) {
            line_[lineLen_] = '\0';
            lineLen_ = 0;
            execLine(line_, nowMs);
        }
        return;
    }
    if (c == 0x08 || c == 0x7F) {           // backspace
        if (lineLen_ > 0) --lineLen_;
        return;
    }
    if (lineLen_ < (int)sizeof(line_) - 1) line_[lineLen_++] = c;
}

void Console::printHelp() {
    out("commands:\n"
        "  status | s            one status line\n"
        "  tune <mhz> | f <mhz>  tune to a frequency (5180-5885, wider with rf method phy)\n"
        "  ch <band><n>          tune to a channel, e.g. ch R1, ch F4\n"
        "  scan [dwell_ms]       measure every channel and report the strongest\n"
        "  sweep <lo> <hi> <step> [dwell_ms]  measure every <step> MHz from lo to hi\n"
        "  rf [...]              radio settings (rf help)\n"
        "  stream on|off [hz]    print status continuously (default 5 Hz)\n"
        "  cal                   show calibration\n"
        "  cal <lo_db> <hi_db>   set the dB levels that read as RSSI 0 and 255\n"
        "  cal lo | cal hi       use the current reading as the low / high end\n"
        "  ema <alpha>           smoothing, 0.01 to 1 (1 = off)\n"
        "  knee <db> <ratio>|off soft ceiling: squeeze signals above <db>\n"
        "  boot <mhz>|off        frequency to tune at power-up\n"
        "  save                  store settings to flash\n"
        "  defaults              restore default settings (not saved)\n"
        "  diag                  radio status\n"
        "  out <0-255>|off       fix the RSSI output level (wiring test)\n"
        "  bus                   what has arrived from FPVGate\n"
        "  selftest              run the software self-test\n");
}

void Console::printStatus() {
    char lab[4];
    channelLabel(ctl_->currentMhz(), lab);
    outf("f=%u %s st=%s db=%.1f rssi=%u valid=%d\n",
         ctl_->currentMhz(), lab, stateName(ctl_->state()),
         ctl_->rssiDb(), ctl_->rssiCounts(), ctl_->rssiValid() ? 1 : 0);
}

void Console::applyCalibration() {
    ctl_->pipeline().setCalibration(cfg_->dbLo, cfg_->dbHi);
    ctl_->pipeline().setEmaAlpha(cfg_->emaAlpha);
    ctl_->pipeline().setKnee(cfg_->kneeDb, cfg_->kneeRatio);
}

void Console::tune(uint16_t mhz, uint32_t nowMs) {
    if (!tunable(mhz)) {
        outf("err: %u MHz is outside the C5 range for the current radio settings\n", mhz);
        return;
    }
    ctl_->tuneMhz(mhz, nowMs);
    char lab[4]; channelLabel(mhz, lab);
    outf("tuned %u MHz (%s)\n", mhz, lab);
}

void Console::startScan(uint32_t dwellMs, uint32_t nowMs) {
    scanIdx_ = nextScanIndex(0);
    if (scanIdx_ < 0) { out("err: no channels to scan\n"); return; }
    scanDwellMs_ = dwellMs;
    scanRestoreMhz_ = ctl_->currentMhz();
    peakMhz_ = 0;
    peakDb_ = -200.0f;
    scanActive_ = true;
    out(sweepStep_ ? "sweep: MHz   peak dB\n" : "scan: channel  MHz   peak dB\n");
    ctl_->tuneMhz(scanMhzAt(scanIdx_), nowMs);
    scanPhaseStartMs_ = nowMs;
    scanSawValid_ = false;
    scanMaxDb_ = -200.0f;
}

void Console::scanStep(uint32_t nowMs) {
    // Only count readings once the radio has settled on the channel.
    if (ctl_->rssiValid()) {
        scanSawValid_ = true;
        if (ctl_->rssiDb() > scanMaxDb_) scanMaxDb_ = ctl_->rssiDb();
    }
    uint32_t settle = ctl_->pipeline().config().settleMs;
    if (nowMs - scanPhaseStartMs_ < settle + scanDwellMs_) return;

    uint16_t mhz = scanMhzAt(scanIdx_);
    if (sweepStep_) {
        if (scanSawValid_) outf("sweep: %u  %6.1f\n", mhz, scanMaxDb_);
        else outf("sweep: %u  no reading\n", mhz);
    } else {
        int n; const FpvChannel& ch = fpvChannelTable(n)[scanIdx_];
        if (scanSawValid_) outf("scan: %c%u       %u  %6.1f\n", ch.band, ch.index, mhz, scanMaxDb_);
        else outf("scan: %c%u       %u  no reading\n", ch.band, ch.index, mhz);
    }
    if (scanSawValid_ && scanMaxDb_ > peakDb_) { peakDb_ = scanMaxDb_; peakMhz_ = mhz; }

    scanIdx_ = nextScanIndex(scanIdx_ + 1);
    if (scanIdx_ < 0) {
        scanActive_ = false;
        const char* what = sweepStep_ ? "sweep" : "scan";
        char lab[4]; channelLabel(peakMhz_, lab);
        if (peakMhz_) outf("%s done: peak %s %u MHz at %.1f dB\n", what, lab, peakMhz_, peakDb_);
        else outf("%s done: no readings\n", what);
        if (scanRestoreMhz_) ctl_->tuneMhz(scanRestoreMhz_, nowMs);
        return;
    }
    ctl_->tuneMhz(scanMhzAt(scanIdx_), nowMs);
    scanPhaseStartMs_ = nowMs;
    scanSawValid_ = false;
    scanMaxDb_ = -200.0f;
}

void Console::tick(uint32_t nowMs) {
    if (scanActive_) scanStep(nowMs);
    if (streamOn_ && !scanActive_ && (nowMs - lastStreamMs_) >= streamPeriodMs_) {
        lastStreamMs_ = nowMs;
        printStatus();
    }
}

void Console::execLine(const char* lineIn, uint32_t nowMs) {
    char buf[80];
    strncpy(buf, lineIn, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* tok[6];
    int n = tokenize(buf, tok, 6);
    if (n == 0) return;
    lower(tok[0]);
    const char* cmd = tok[0];

    if (scanActive_ && strcmp(cmd, "scan") != 0 && strcmp(cmd, "sweep") != 0) {
        scanActive_ = false;                     // any other command stops a scan
        out("scan aborted\n");
        if (scanRestoreMhz_) ctl_->tuneMhz(scanRestoreMhz_, nowMs);
    }

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        printHelp();
    } else if (!strcmp(cmd, "status") || !strcmp(cmd, "s")) {
        printStatus();
    } else if (!strcmp(cmd, "tune") || !strcmp(cmd, "f")) {
        if (n < 2) { out("usage: tune <mhz>\n"); return; }
        tune((uint16_t)atoi(tok[1]), nowMs);
    } else if (!strcmp(cmd, "ch")) {
        if (n < 2) { out("usage: ch <band><n>, e.g. ch R1\n"); return; }
        uint16_t mhz = parseBandChannel(tok[1]);
        if (!mhz) { outf("err: unknown channel '%s'\n", tok[1]); return; }
        tune(mhz, nowMs);
    } else if (!strcmp(cmd, "scan")) {
        uint32_t dwell = (n >= 2) ? (uint32_t)atoi(tok[1]) : 60;
        if (dwell < 5) dwell = 5;
        if (dwell > 2000) dwell = 2000;
        sweepStep_ = 0;
        startScan(dwell, nowMs);
    } else if (!strcmp(cmd, "sweep")) {
        if (n < 4) { out("usage: sweep <lo_mhz> <hi_mhz> <step_mhz> [dwell_ms]\n"); return; }
        int lo = atoi(tok[1]), hi = atoi(tok[2]), step = atoi(tok[3]);
        if (lo < 1 || hi < lo || hi > 65535 || step < 1 || step > 1000) {
            out("err: need 0 < lo <= hi and a step of 1 to 1000 MHz\n");
            return;
        }
        uint32_t dwell = (n >= 5) ? (uint32_t)atoi(tok[4]) : 60;
        if (dwell < 5) dwell = 5;
        if (dwell > 2000) dwell = 2000;
        sweepLo_ = (uint16_t)lo; sweepHi_ = (uint16_t)hi; sweepStep_ = (uint16_t)step;
        startScan(dwell, nowMs);
    } else if (!strcmp(cmd, "rf")) {
        char reply[400];
        reply[0] = '\0';
        RfBackend* rf = ctl_->backend();
        if (!rf || !rf->command(tok + 1, n - 1, reply, sizeof(reply))) {
            out("err: this radio has no rf settings\n");
            return;
        }
        out(reply);
        // A setting may change what the radio can tune, so tune again.
        if (ctl_->currentMhz()) ctl_->tuneMhz(ctl_->currentMhz(), nowMs);
    } else if (!strcmp(cmd, "stream")) {
        if (n < 2) { out("usage: stream on|off [hz]\n"); return; }
        lower(tok[1]);
        if (!strcmp(tok[1], "on")) {
            int hz = (n >= 3) ? atoi(tok[2]) : 5;
            if (hz < 1) hz = 1;
            if (hz > 50) hz = 50;
            streamPeriodMs_ = 1000u / (uint32_t)hz;
            streamOn_ = true;
            lastStreamMs_ = nowMs - streamPeriodMs_;
            outf("stream on at %d Hz\n", hz);
        } else {
            streamOn_ = false;
            out("stream off\n");
        }
    } else if (!strcmp(cmd, "cal")) {
        if (n == 1) {
            if (cfg_->kneeRatio > 1.0f)
                outf("cal lo=%.1f hi=%.1f ema=%.2f knee=%.1f ratio=%.1f\n", cfg_->dbLo,
                     cfg_->dbHi, cfg_->emaAlpha, cfg_->kneeDb, cfg_->kneeRatio);
            else
                outf("cal lo=%.1f hi=%.1f ema=%.2f knee=off\n", cfg_->dbLo, cfg_->dbHi,
                     cfg_->emaAlpha);
            return;
        }
        lower(tok[1]);
        if (!strcmp(tok[1], "lo") || !strcmp(tok[1], "hi")) {
            if (!ctl_->rssiValid()) { out("err: no valid reading to capture (tune first)\n"); return; }
            float db = ctl_->rssiDb();
            float lo = cfg_->dbLo, hi = cfg_->dbHi;
            if (tok[1][0] == 'l') lo = db; else hi = db;
            if (!(hi > lo + 1.0f)) { outf("err: hi must be > lo (lo=%.1f hi=%.1f)\n", lo, hi); return; }
            cfg_->dbLo = lo; cfg_->dbHi = hi;
            applyCalibration();
            outf("cal lo=%.1f hi=%.1f\n", cfg_->dbLo, cfg_->dbHi);
        } else if (n >= 3) {
            float lo = (float)atof(tok[1]);
            float hi = (float)atof(tok[2]);
            if (!(hi > lo + 1.0f)) { out("err: hi must be > lo\n"); return; }
            cfg_->dbLo = lo; cfg_->dbHi = hi;
            applyCalibration();
            outf("cal lo=%.1f hi=%.1f\n", cfg_->dbLo, cfg_->dbHi);
        } else {
            out("usage: cal | cal <lo> <hi> | cal lo | cal hi\n");
        }
    } else if (!strcmp(cmd, "knee")) {
        if (n < 2) { out("usage: knee <db> <ratio> | knee off\n"); return; }
        lower(tok[1]);
        if (!strcmp(tok[1], "off")) {
            cfg_->kneeRatio = 1.0f;
        } else {
            if (n < 3) { out("usage: knee <db> <ratio>, e.g. knee -55 6\n"); return; }
            float db = (float)atof(tok[1]);
            float ratio = (float)atof(tok[2]);
            if (ratio < 1.0f || ratio > 50.0f) { out("err: ratio must be 1..50\n"); return; }
            if (!(db > cfg_->dbLo && db < cfg_->dbHi)) {
                outf("err: knee must be between cal lo (%.1f) and hi (%.1f)\n", cfg_->dbLo, cfg_->dbHi);
                return;
            }
            cfg_->kneeDb = db;
            cfg_->kneeRatio = ratio;
        }
        applyCalibration();
        if (cfg_->kneeRatio > 1.0f) outf("knee %.1f dB ratio %.1f (save to keep)\n", cfg_->kneeDb, cfg_->kneeRatio);
        else out("knee off (save to keep)\n");
    } else if (!strcmp(cmd, "ema")) {
        if (n < 2) { out("usage: ema <alpha 0.01..1>\n"); return; }
        float a = (float)atof(tok[1]);
        if (a < 0.01f || a > 1.0f) { out("err: alpha must be 0.01..1\n"); return; }
        cfg_->emaAlpha = a;
        applyCalibration();
        outf("ema %.2f\n", a);
    } else if (!strcmp(cmd, "boot")) {
        if (n < 2) { outf("boot %u\n", cfg_->bootMhz); return; }
        lower(tok[1]);
        if (!strcmp(tok[1], "off")) {
            cfg_->bootMhz = 0;
            out("boot off (save to keep)\n");
        } else {
            uint16_t mhz = (uint16_t)atoi(tok[1]);
            if (!c5InRange(mhz)) { out("err: out of C5 range\n"); return; }
            cfg_->bootMhz = mhz;
            outf("boot %u (save to keep)\n", mhz);
        }
    } else if (!strcmp(cmd, "save")) {
        bool ok = hooks_.save ? hooks_.save(hooks_.ctx) : false;
        out(ok ? "saved\n" : "err: save failed\n");
    } else if (!strcmp(cmd, "defaults")) {
        *cfg_ = PersistConfig();
        applyCalibration();
        out("defaults restored (save to keep)\n");
    } else if (!strcmp(cmd, "bus")) {
        if (!hooks_.busInfo) { out("err: not available\n"); return; }
        char buf[400];
        hooks_.busInfo(hooks_.ctx, buf, sizeof(buf));
        out(buf);
    } else if (!strcmp(cmd, "out")) {
        if (n < 2) { out("usage: out <0-255> | out off\n"); return; }
        if (!hooks_.forceOutput) { out("err: not available\n"); return; }
        lower(tok[1]);
        int c = !strcmp(tok[1], "off") ? -1 : atoi(tok[1]);
        if (c > 255) c = 255;
        if (c < -1) c = -1;
        int err = hooks_.forceOutput(hooks_.ctx, c);
        if (err) outf("err: analog output not running (error %d)\n", err);
        else if (c < 0) out("out off (the output follows the RSSI again)\n");
        else outf("out %d (fixed until 'out off')\n", c);
    } else if (!strcmp(cmd, "diag")) {
        printStatus();
        outf("channel supported=%d\n", ctl_->freqSupported() ? 1 : 0);
        if (ctl_->backend()) {
            char buf[400];
            ctl_->backend()->diag(buf, sizeof(buf));
            if (buf[0]) out(buf);
        }
    } else if (!strcmp(cmd, "selftest")) {
        SelfTestResult r = runSelfTest();
        outf("selftest: 0x%02X %s\n", r.passedBits, r.pass() ? "PASS" : "FAIL");
    } else {
        outf("unknown command '%s' (try help)\n", cmd);
    }
}

} // namespace c5rx
