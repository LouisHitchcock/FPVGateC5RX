// Pin assignments. Each can be changed per board with a -D flag in
// platformio.ini, e.g. -DPIN_RX5808_SEL=6.
//
// The defaults match the GPIO numbers FPVGate uses on a Seeed XIAO ESP32-S3
// (CLK 4, DATA 5, SEL 6), so the wiring is like for like.
//
// Pins to avoid on an ESP32-C5:
//   GPIO2, 3, 7, 8, 9, 25, 26, 27, 28  strapping pins (they set the boot mode)
//   GPIO11, 12                          UART0
//   GPIO13, 14                          USB
// On the Waveshare C5-Zero, GPIO9 is also the BOOT button, GPIO26 the antenna
// switch and GPIO27 the RGB LED.
#ifndef C5RX_BOARD_PINS_H
#define C5RX_BOARD_PINS_H

// RX5808 bus from FPVGate. DATA goes both ways.
#ifndef PIN_RX5808_SEL
#define PIN_RX5808_SEL   6
#endif
#ifndef PIN_RX5808_CLK
#define PIN_RX5808_CLK   4
#endif
#ifndef PIN_RX5808_DATA
#define PIN_RX5808_DATA  5
#endif

// RSSI output, into the RC filter (see docs/HARDWARE.md).
#ifndef PIN_RSSI_SDM
#define PIN_RSSI_SDM     10
#endif

#endif // C5RX_BOARD_PINS_H
