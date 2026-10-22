// W48 rotary phone — ESP32-S3 DevKitC-1.
// Thin glue: GPIO -> PulseDecoder + hook Debouncer -> CallController
//            -> Serial + WS2812 LED.
//
// Serial commands (115200 baud, newline-terminated, "USB" socket):
//   raw       toggle raw per-pulse timing output
//   lift      simulate lifting the handset (until 'hangup')
//   hangup    end simulated off-hook
//   status    print hook / call state
//   play <id> play /<id>.wav from LittleFS (then /<id>.loop.wav looped, if present)
//   call <n>  as if <n> had been dialled: touch tones, ringback, pickup, /<n>.wav
//   tone      dial tone      busy: busy tone
//   vol <n>   volume 0..100
//   ls        list audio files
//   sd        (re)mount the SD card and rescan its numbers
//   test      start dial self-test (dial 0..9 in order)
//   stop      stop audio / abort self-test
//   help      list commands

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

#include "AudioPlayer.h"
#include "CallController.h"
#include "CallProgress.h"
#include "Debouncer.h"
#include "Dialplan.h"
#include "PulseDecoder.h"
#include "PulseStats.h"
#include "config.h"
#include "directory.h"

// ---------------------------------------------------------------------------
// Output to both USB connectors: Serial = native USB CDC ("USB" port),
// Serial0 = UART0 via CP2102 ("UART" port). Commands are accepted on either.
// ---------------------------------------------------------------------------
class DualOut : public Print {
 public:
  size_t write(uint8_t c) override {
    Serial0.write(c);
    Serial.write(c);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t n) override {
    Serial0.write(buf, n);
    Serial.write(buf, n);
    return n;
  }
};
static DualOut out;

#define STR_(x) #x
#define STR(x) STR_(x)

// ---------------------------------------------------------------------------
// Non-blocking single-pixel LED: a steady base colour plus timed flashes.
// ---------------------------------------------------------------------------
class StatusLed {
 public:
  explicit StatusLed(uint8_t pin) : px_(1, pin, NEO_GRB + NEO_KHZ800) {}

  void begin() {
    px_.begin();
    px_.setBrightness(LED_BRIGHTNESS);
    show(base_);
  }
  void setBase(uint8_t r, uint8_t g, uint8_t b) {
    base_ = px_.Color(r, g, b);
    if (!flashing_) show(base_);
  }
  void flash(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) {
    show(px_.Color(r, g, b));
    flashOffMs_ = millis() + durationMs;
    flashing_ = true;
  }
  void tick() {
    if (flashing_ && (int32_t)(millis() - flashOffMs_) >= 0) {
      flashing_ = false;
      show(base_);
    }
  }

 private:
  void show(uint32_t c) {
    px_.setPixelColor(0, c);
    px_.show();
  }
  Adafruit_NeoPixel px_;
  uint32_t base_ = 0;
  uint32_t flashOffMs_ = 0;
  bool flashing_ = false;
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static PulseDecoder::Config makeDecoderConfig() {
  PulseDecoder::Config c;
  c.debounceMs = PULSE_DEBOUNCE_MS;
  c.digitGapMs = DIGIT_GAP_MS;
  c.numberGapMs = NUMBER_GAP_MS;
  return c;
}

static PulseDecoder decoder(makeDecoderConfig());
static Debouncer hook(HOOK_DEBOUNCE_MS);
// Reachable numbers = directory.h entries + numbers found as files on the card.
static constexpr size_t kMaxCardNumbers = 200;
static char cardNumbers[kMaxCardNumbers][CallController::kMaxDigits + 1];
static Dialplan::Entry allEntries[kDirectorySize + kMaxCardNumbers];
static size_t allEntryCount = 0;
static Dialplan dialplan(allEntries, 0, DIAL_MAX_DIGITS, DIAL_ACCEPT_ANY);
static fs::FS* media = &LittleFS;  // where sound files are read from
static bool sdMounted = false;
static CallController call(dialplan, INTER_DIGIT_TIMEOUT_MS);
static StatusLed led(NEOPIXEL_PIN);
static AudioPlayer audio;

static bool rawTiming = false;
static bool simulatedOffHook = false;

// Self-test state
static bool selfTest = false;
static uint8_t selfTestIndex = 0;
static uint8_t selfTestPassed = 0;
static PulseStats selfTestStats;

static char lineBuf[32];
static size_t lineLen = 0;

// ---------------------------------------------------------------------------
// Destination hook: this is where audio playback will go.
// ---------------------------------------------------------------------------
// Per destination: <name>.wav plays once; if <name>.loop.wav exists it then
// loops until hang-up, otherwise the busy tone follows. <name> is the dialled
// number itself if such a file exists, else the directory.h id.
static char currentName[CallController::kMaxDigits + 1] = "0";

static bool mediaHas(const char* name, const char* suffix) {
  char path[40];
  snprintf(path, sizeof(path), "/%s%s", name, suffix);
  return media->exists(path);
}

// Pick the file base name for a call: dialled number first, then id.
static void chooseName(const char* number, uint16_t id) {
  if (number && *number && (mediaHas(number, ".wav") || mediaHas(number, ".loop.wav")))
    snprintf(currentName, sizeof(currentName), "%s", number);
  else
    snprintf(currentName, sizeof(currentName), "%u", id);
}

static bool startIntro() {
  char path[40];
  snprintf(path, sizeof(path), "/%s.wav", currentName);
  if (!media->exists(path)) return false;
  out.printf("playing %s\n", path);
  return audio.playFile(path);
}

// Start the loop part for the current destination. Returns false if none.
static bool startLoop() {
  char path[40];
  snprintf(path, sizeof(path), "/%s.loop.wav", currentName);
  if (!media->exists(path)) return false;
  out.printf("looping %s\n", path);
  return audio.playFile(path, true);
}

// --- Media: SD card with internal flash as fallback -------------------------
static bool isDigits(const char* s, size_t n) {
  if (n == 0 || n > CallController::kMaxDigits) return false;
  for (size_t i = 0; i < n; ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

// Rebuild the dialplan: directory.h entries, then one entry per numeric
// file name on the media (/<digits>.wav or /<digits>.loop.wav).
static void rebuildDialplan() {
  allEntryCount = 0;
  for (size_t i = 0; i < kDirectorySize; ++i) allEntries[allEntryCount++] = kDirectory[i];

  size_t found = 0;
  File dir = media->open("/");
  for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    const char* name = f.name();
    const char* slash = strrchr(name, '/');
    if (slash) name = slash + 1;
    size_t len = strlen(name);
    size_t stem = 0;
    if (len > 9 && strcasecmp(name + len - 9, ".loop.wav") == 0) stem = len - 9;
    else if (len > 4 && strcasecmp(name + len - 4, ".wav") == 0) stem = len - 4;
    else continue;
    if (!isDigits(name, stem)) continue;
    bool dup = false;
    for (size_t i = 0; i < found && !dup; ++i)
      dup = strlen(cardNumbers[i]) == stem && strncmp(cardNumbers[i], name, stem) == 0;
    for (size_t i = 0; i < kDirectorySize && !dup; ++i)
      dup = strlen(kDirectory[i].number) == stem && strncmp(kDirectory[i].number, name, stem) == 0;
    if (dup || found >= kMaxCardNumbers) continue;
    memcpy(cardNumbers[found], name, stem);
    cardNumbers[found][stem] = '\0';
    allEntries[allEntryCount++] = Dialplan::Entry{cardNumbers[found], 0};
    found++;
  }
  dialplan.setEntries(allEntries, allEntryCount);
  out.printf("dialplan: %u from directory.h + %u from files = %u numbers\n",
             (unsigned)kDirectorySize, (unsigned)found, (unsigned)allEntryCount);
}

static void mountMedia() {
  audio.stop();
  sdMounted = false;
#if SD_ENABLED
  SD.end();
  SPI.end();
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (SD.begin(SD_CS_PIN, SPI, SD_SPI_HZ)) {
    sdMounted = true;
    media = &SD;
    out.printf("media: SD card, %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));
  } else {
    media = &LittleFS;
    out.println("media: no SD card, using internal flash");
  }
#else
  media = &LittleFS;
#endif
  audio.setFS(*media);
  rebuildDialplan();
}

static void playUnobtainableTone() {
  audio.playTone(TONE_HZ, UNOBTAINABLE_TONE_ON_MS, UNOBTAINABLE_TONE_OFF_MS);
}

// The "other side answered": play the sample for the current destination.
static void startSample() {
  if (startIntro()) return;
  if (startLoop()) return;
  out.printf("no audio file /%s.wav on %s\n", currentName, sdMounted ? "SD card" : "internal flash");
  playUnobtainableTone();
}

static callprogress::Timing connectTiming() {
  callprogress::Timing t{};
  t.leadMs = DTMF_LEAD_MS;
  t.dtmfOnMs = DTMF_ON_MS;
  t.dtmfOffMs = DTMF_OFF_MS;
  t.ringDelayMs = RINGBACK_DELAY_MS;
  t.ringToneHz = TONE_HZ;
  t.ringOnMs = RINGBACK_ON_MS;
  t.ringOffMs = RINGBACK_OFF_MS;
  t.rings = RINGBACK_RINGS;
  t.pickupDelayMs = PICKUP_DELAY_MS;
  t.clickMs = PICKUP_CLICK_MS;
  t.settleMs = PICKUP_SETTLE_MS;
  return t;
}

static void onConnected(uint16_t id, const char* number) {
  chooseName(number, id);
  out.printf("connected: %s -> /%s.wav\n", number, currentName);
#if CONNECT_SEQUENCE
  if (number && *number) {
    out.printf("calling %s: touch tones, %u ring(s)...\n", number, (unsigned)RINGBACK_RINGS);
    audio.playConnect(number, connectTiming());
    return;  // startSample() follows once takeConnectFinished() fires
  }
#endif
  startSample();
}

static void listFiles() {
  File dir = media->open("/");
  size_t count = 0;
  for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) continue;
    out.printf("  %s  %lu bytes\n", f.name(), (unsigned long)f.size());
    count++;
  }
  if (sdMounted)
    out.printf("%u file(s) on SD card, %llu of %llu MB used\n", (unsigned)count,
               SD.usedBytes() / (1024ULL * 1024ULL), SD.totalBytes() / (1024ULL * 1024ULL));
  else
    out.printf("%u file(s) on internal flash, %lu of %lu KB used\n", (unsigned)count,
               (unsigned long)(LittleFS.usedBytes() / 1024), (unsigned long)(LittleFS.totalBytes() / 1024));
}

// ---------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------
static void selfTestPrompt() { out.printf("[test] dial %u now\n", selfTestIndex); }

static void selfTestStart() {
  selfTest = true;
  selfTestIndex = 0;
  selfTestPassed = 0;
  selfTestStats.clear();
  decoder.reset();
  out.println("[test] self-test started: dial 0 through 9 in order ('stop' to abort)");
  selfTestPrompt();
}

static void selfTestFinish(bool aborted) {
  selfTest = false;
  out.println(aborted ? "[test] aborted" : "[test] done");
  out.printf("[test] result: %u/10 PASS\n", selfTestPassed);
  out.printf("[test] pulses: %lu, avg open %.1f ms, avg closed %.1f ms\n",
             (unsigned long)selfTestStats.pulses(), selfTestStats.avgOpenMs(),
             selfTestStats.avgClosedMs());
  out.printf("[test] avg pulse period %.1f ms (%.1f pps), open/closed ratio %.2f (nominal 1.50)\n",
             selfTestStats.avgPeriodMs(),
             selfTestStats.avgPeriodMs() > 0 ? 1000.f / selfTestStats.avgPeriodMs() : 0.f,
             selfTestStats.openClosedRatio());
}

static void selfTestOnDigit(const PulseDecoder::Event& e) {
  uint8_t expected = selfTestIndex;
  bool pass = (e.digit == (int8_t)expected);
  if (pass) selfTestPassed++;
  out.printf("[test] expected %u, got %d (%u pulses): %s\n", expected, e.digit, e.pulses,
             pass ? "PASS" : "FAIL");
  selfTestIndex++;
  if (selfTestIndex >= 10) selfTestFinish(false);
  else selfTestPrompt();
}

// ---------------------------------------------------------------------------
// Serial commands
// ---------------------------------------------------------------------------
static void printHelp() {
  out.println("commands: raw, lift, hangup, status, play <number>, call <number>, tone, busy, vol <0-100>, ls, sd, test, stop, help");
}

static void printStatus() {
  out.printf("hook pin: %s%s | call: %s | dialed: \"%s\"\n",
             hook.state() ? "off-hook" : "on-hook",
             simulatedOffHook ? " (+simulated lift)" : "",
             CallController::stateName(call.state()), call.dialed());
  out.printf("audio: %s, %s, volume %u%%, media: %s, %u numbers, underruns %lu\n",
             audio.ok() ? "driver ok" : "driver FAILED", audio.isPlaying() ? "playing" : "idle",
             audio.volume(), sdMounted ? "SD card" : "internal flash", (unsigned)allEntryCount,
             (unsigned long)audio.underruns());
}

static void handleCommand(const char* cmd) {
  const char* arg = strchr(cmd, ' ');
  size_t nameLen = arg ? (size_t)(arg - cmd) : strlen(cmd);
  if (arg) while (*arg == ' ') arg++;
  auto is = [&](const char* name) { return strlen(name) == nameLen && strncmp(cmd, name, nameLen) == 0; };

  if (is("play")) {
    if (!arg || !*arg) { out.println("usage: play <number or id>"); return; }
    chooseName(arg, (uint16_t)atoi(arg));
    if (!startIntro() && !startLoop()) out.printf("no such file: /%s.wav\n", currentName);
  } else if (is("call")) {
    if (!arg || !*arg || !isDigits(arg, strlen(arg))) { out.println("usage: call <number>"); return; }
    onConnected((uint16_t)atoi(arg), arg);
  } else if (is("sd")) {
    mountMedia();
  } else if (is("tone")) {
    audio.playTone(TONE_HZ, DIAL_TONE_ON_MS, 0);
    out.println("dial tone");
  } else if (is("busy")) {
    audio.playTone(TONE_HZ, BUSY_TONE_ON_MS, BUSY_TONE_OFF_MS);
    out.println("busy tone");
  } else if (is("vol")) {
    if (arg && *arg) {
      bool digits = true;
      for (const char* p = arg; *p; ++p) digits = digits && (*p >= '0' && *p <= '9');
      long v = digits ? atol(arg) : -1;
      if (v < 0) out.println("usage: vol <0-" STR(AUDIO_MAX_VOLUME) ">");
      else if (v > AUDIO_MAX_VOLUME) out.printf("capped at %u%% (AUDIO_MAX_VOLUME in config.h)\n", AUDIO_MAX_VOLUME);
      audio.setVolume((uint8_t)(v < 0 ? audio.volume() : (v > AUDIO_MAX_VOLUME ? AUDIO_MAX_VOLUME : v)));
    }
    out.printf("volume: %u%% (max %u%%)\n", audio.volume(), AUDIO_MAX_VOLUME);
  } else if (is("ls")) {
    listFiles();
  } else if (strcmp(cmd, "raw") == 0) {
    rawTiming = !rawTiming;
    out.printf("raw timing: %s\n", rawTiming ? "on" : "off");
  } else if (strcmp(cmd, "lift") == 0) {
    simulatedOffHook = true;
    out.println("simulated: handset lifted");
  } else if (strcmp(cmd, "hangup") == 0) {
    simulatedOffHook = false;
    out.println("simulated: handset replaced");
  } else if (strcmp(cmd, "status") == 0) {
    printStatus();
  } else if (strcmp(cmd, "test") == 0) {
    selfTestStart();
  } else if (strcmp(cmd, "stop") == 0) {
    audio.stop();
    if (selfTest) selfTestFinish(true);
    else out.println("audio stopped");
  } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (cmd[0] != '\0') {
    out.printf("unknown command: '%s'\n", cmd);
    printHelp();
  }
}

static void pollSerialFrom(Stream& s) {
  while (s.available()) {
    char c = (char)s.read();
    if (c == '\n' || c == '\r') {
      lineBuf[lineLen] = '\0';
      if (lineLen > 0) handleCommand(lineBuf);
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1 && c >= ' ') {
      lineBuf[lineLen++] = c;
    }
  }
}

// ---------------------------------------------------------------------------
// Event dispatch
// ---------------------------------------------------------------------------
static void handleDecoderEvent(uint32_t now, const PulseDecoder::Event& e) {
  switch (e.type) {
    case PulseDecoder::EventType::Pulse:
      led.flash(0, 0, 255, LED_PULSE_FLASH_MS);
      if (call.state() == CallController::State::OffHook) audio.stop();  // end dial tone
      if (rawTiming) {
        if (e.closedMs > 0)
          out.printf("pulse %u: open %lu ms, closed %lu ms\n", e.pulseIndex,
                     (unsigned long)e.openMs, (unsigned long)e.closedMs);
        else
          out.printf("pulse %u: open %lu ms\n", e.pulseIndex, (unsigned long)e.openMs);
      }
      if (selfTest) selfTestStats.add(e.openMs, e.closedMs);
      break;

    case PulseDecoder::EventType::Digit:
      led.flash(0, 255, 0, LED_DIGIT_FLASH_MS);
      if (e.digit >= 0) out.printf("digit: %d\n", e.digit);
      else out.printf("digit: ? (%u pulses)\n", e.pulses);
      if (selfTest) selfTestOnDigit(e);
      else if (!call.offHook()) out.println("  (ignored: handset on hook)");
      else call.onDigit(now, e.digit);
      break;

    case PulseDecoder::EventType::Number:
      out.printf("number: %s\n", e.number);
      break;
  }
}

static void handleCallEvent(const CallController::Event& e) {
  switch (e.type) {
    case CallController::EventType::Lifted:
      out.println("handset lifted");
      led.setBase(120, 60, 0);  // amber: dial tone
      audio.playTone(TONE_HZ, DIAL_TONE_ON_MS, 0);
      break;
    case CallController::EventType::HungUp:
      out.println("handset replaced");
      led.setBase(0, 0, 0);
      audio.stop();
      break;
    case CallController::EventType::DigitAccepted:
      out.printf("dialing: %s\n", e.number);
      break;
    case CallController::EventType::Connected:
      led.setBase(0, 255, 0);
      onConnected(e.id, e.number);
      break;
    case CallController::EventType::Unobtainable: {
      const char* why = "unknown";
      switch (e.reason) {
        case CallController::Reason::NoSuchNumber: why = "no such number"; break;
        case CallController::Reason::InvalidDigit: why = "invalid digit"; break;
        case CallController::Reason::Timeout: why = "timeout"; break;
        default: break;
      }
      out.printf("unobtainable: \"%s\" (%s)\n", e.number, why);
      led.setBase(255, 0, 0);
      playUnobtainableTone();
      break;
    }
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial0.begin(115200);      // UART port
  Serial.begin(115200);       // native USB port
  Serial.setTxTimeoutMs(0);   // never stall the pulse loop if USB isn't attached
  pinMode(PULSE_PIN, INPUT_PULLUP);
  pinMode(HOOK_PIN, INPUT_PULLUP);
  led.begin();
  delay(200);

  bool fsOk = LittleFS.begin(true);
  bool audioOk = audio.begin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN, I2S_SD_PIN, LittleFS);
  audio.setVolume(AUDIO_DEFAULT_VOLUME);
  if (!fsOk) out.println("LittleFS mount FAILED");

  out.println();
  out.println("Phone: W48 rotary phone");
  out.printf("pulse GPIO%d, hook GPIO%d (%s = lifted), LED GPIO%d\n", PULSE_PIN, HOOK_PIN,
             HOOK_ACTIVE_LOW ? "LOW" : "HIGH", NEOPIXEL_PIN);
  out.printf("dialling: max %u digits%s\n", (unsigned)DIAL_MAX_DIGITS,
             DIAL_ACCEPT_ANY ? ", any full-length number accepted" : "");
  if (digitalRead(PULSE_PIN) == HIGH) {
    out.println("WARNING: pulse contact reads OPEN at boot - check wiring");
  }
  out.printf("audio: I2S BCLK %d LRC %d DIN %d SD %d, %s, volume %u%%\n", I2S_BCLK_PIN,
             I2S_LRC_PIN, I2S_DIN_PIN, I2S_SD_PIN, audioOk ? "ok" : "FAILED", audio.volume());
#if SD_ENABLED
  out.printf("sd: CS %d MOSI %d MISO %d SCK %d\n", SD_CS_PIN, SD_MOSI_PIN, SD_MISO_PIN, SD_SCK_PIN);
#endif
  mountMedia();
  listFiles();
  printHelp();
}

void loop() {
  uint32_t now = millis();

  bool pulseOpen = digitalRead(PULSE_PIN) == HIGH;
  decoder.update(now, pulseOpen);

  bool hookRaw = digitalRead(HOOK_PIN) == (HOOK_ACTIVE_LOW ? LOW : HIGH);
  if (hook.update(now, hookRaw)) {
    out.printf("hook pin: %s\n", hook.state() ? "off-hook" : "on-hook");
  }
  call.setHook(now, hook.state() || simulatedOffHook);
  call.tick(now);

  PulseDecoder::Event de;
  while (decoder.pollEvent(de)) handleDecoderEvent(now, de);
  CallController::Event ce;
  while (call.pollEvent(ce)) handleCallEvent(ce);

  if (audio.takeConnectFinished()) {
    out.println("answered");
    startSample();
  }
  if (audio.takeFinished()) {
    out.println("audio: finished");
    if (!startLoop() && AUDIO_BUSY_AFTER_SAMPLE && call.state() == CallController::State::Connected)
      audio.playTone(TONE_HZ, BUSY_TONE_ON_MS, BUSY_TONE_OFF_MS);
  }

  led.tick();
  pollSerialFrom(Serial);
  pollSerialFrom(Serial0);
}
