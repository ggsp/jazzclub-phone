#pragma once

// === PIN DEFINITIONS ===
#define PULSE_PIN     4   // W48 terminal 1 (nsi, yellow). Terminal 2 (green) to GND.
                          // Closed at rest (LOW), opens once per pulse (HIGH).
                          // External 1 kOhm pull-up to 3V3 (needed: off-hook, the
                          // W48 puts 100 Ohm + 1 uF across nsi as spark quench).
#define HOOK_PIN      5   // W48 terminal b (brown line wire). Internal pull-up.
                          // Off-hook: hook switch closes the DC speech loop
                          // from terminal 2 (GND) to b -> pin reads LOW.
#define HOOK_ACTIVE_LOW 1 // 1: LOW = handset lifted. 0: HIGH = lifted.
#define NEOPIXEL_PIN  38  // Onboard WS2812 RGB LED (ESP32-S3 DevKitC-1)

// MAX98357A I2S amplifier -> W48 handset earpiece (terminals F / F').
// Amp VIN -> 3V3, GND -> GND, GAIN unconnected (9 dB; GND = 6 dB, VIN = 12 dB).
// Speaker "+" -> RV1 (2 kOhm trimmer as rheostat, 0 Ohm = full volume) -> F,
// speaker "-" -> F' (220 Ohm capsule). Volume is set once on the perfboard.
// Bridge-tied output: never connect either speaker terminal to GND.
#define I2S_BCLK_PIN  13  // amp BCLK
#define I2S_LRC_PIN   14  // amp LRC (word select)
#define I2S_DIN_PIN   12  // amp DIN (data)
#define I2S_SD_PIN    15  // amp SD (shutdown/mode): driven HIGH = amp on, left channel.
                          // Clone boards may lack a pull-up, leaving the amp muted
                          // if this pin floats. Set to -1 if not wired.

// microSD breakout (3.3 V, SPI). Sound files on the card: /<number>.wav plays
// once, /<number>.loop.wav then loops. Falls back to internal flash if no card.
#define SD_ENABLED    1
#define SD_CS_PIN     10
#define SD_MOSI_PIN   17  // card DI
#define SD_MISO_PIN   16  // card DO
#define SD_SCK_PIN    18  // card CLK
#define SD_SPI_HZ     20000000

// === PULSE DECODER TIMING (ms) ===
#define PULSE_DEBOUNCE_MS   15    // contact bounce filter
#define DIGIT_GAP_MS        250   // no edges for this long -> digit complete
#define NUMBER_GAP_MS       0     // decoder-level number grouping; 0 = off,
                                  // the call controller / dialplan groups digits

// === HOOK / CALL FLOW (ms) ===
#define HOOK_DEBOUNCE_MS          50
#define INTER_DIGIT_TIMEOUT_MS    10000  // stop dialling for this long -> unobtainable

// === AUDIO ===
#define AUDIO_DEFAULT_VOLUME     10   // percent at boot; physical level is set by RV1
#define AUDIO_MAX_VOLUME         100  // hard cap for 'vol'
#define AUDIO_BUSY_AFTER_SAMPLE  1    // play busy tone once the sample has ended
#define TONE_HZ                  425  // German call-progress tones are 425 Hz
#define DIAL_TONE_ON_MS          0    // 0 = continuous
#define BUSY_TONE_ON_MS          480
#define BUSY_TONE_OFF_MS         480
#define UNOBTAINABLE_TONE_ON_MS  240  // fast busy ("Gassenbesetzt")
#define UNOBTAINABLE_TONE_OFF_MS 240

// === CALL SETUP (heard after the number is recognised, before the sample) ===
// touch tones for the digits -> ringback -> pickup click -> /<number>.wav
#define CONNECT_SEQUENCE         1     // 0: play the sample immediately
#define DTMF_LEAD_MS             300   // silence before the first touch tone
#define DTMF_ON_MS               120
#define DTMF_OFF_MS              80
#define RINGBACK_DELAY_MS        800   // silence between last tone and first ring
#define RINGBACK_RINGS           2
#define RINGBACK_ON_MS           1000  // German Freizeichen: 1 s on, 4 s off
#define RINGBACK_OFF_MS          3000  // (shortened a little)
#define PICKUP_DELAY_MS          600   // silence after the last ring before the click
#define PICKUP_CLICK_MS          30
#define PICKUP_SETTLE_MS         300   // silence after the click before the sample

// === LED ===
#define LED_PULSE_FLASH_MS   30   // blue, per pulse
#define LED_DIGIT_FLASH_MS   150  // green, per digit
#define LED_BRIGHTNESS       40   // 0..255
