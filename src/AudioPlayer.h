#pragma once
// I2S audio output for the MAX98357A: plays 16-bit PCM WAV files from LittleFS
// and generates call-progress tones and the "dial, ring, pick up" sequence. Playback runs in its own FreeRTOS task on
// core 0 so the main loop (pulse sampling) is never blocked.

#include <Arduino.h>
#include <FS.h>

#include "CallProgress.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class AudioPlayer {
 public:
  // sdPin: amp shutdown/mode pin driven by us (HIGH = on). -1 if not wired.
  bool begin(int bclkPin, int lrcPin, int dinPin, int sdPin, fs::FS& fs);

  // Returns false if the file is missing or not 8/16-bit PCM.
  // loop = true restarts the file at its end until stop() / another play.
  bool playFile(const char* path, bool loop = false);
  // offMs == 0 -> continuous tone.
  void playTone(uint16_t hz, uint16_t onMs, uint16_t offMs);
  // Touch tones for `digits`, then ringback and a pickup click. Ends on its
  // own; poll takeConnectFinished() to start the sample.
  void playConnect(const char* digits, const callprogress::Timing& timing);
  void stop();

  void setVolume(uint8_t percent);
  uint8_t volume() const { return volumePercent_; }
  bool isPlaying() const { return playing_; }
  bool ok() const { return ok_; }
  // Switch the filesystem files are read from. Call stop() first.
  void setFS(fs::FS& fs) { fs_ = &fs; }
  // True once after a file reaches its end on its own.
  bool takeFinished();
  // True once after a playConnect() sequence has ended (someone "answered").
  bool takeConnectFinished();
  // Number of times the output buffer ran dry (audible gap/click).
  uint32_t underruns() const { return underruns_; }

 private:
  enum class CmdType : uint8_t { Stop, File, Tone, Connect };
  struct Cmd {
    CmdType type;
    char path[48];  // file path, or the digits for Connect
    bool loop;
    uint16_t hz, onMs, offMs;
    callprogress::Timing timing;
  };
  enum class Mode : uint8_t { Idle, File, Tone, Connect };

  static void taskEntry(void* arg);
  void task();
  void apply(const Cmd& c);
  void setRate(uint32_t rate);
  void writeFrames(const int16_t* mono, size_t n);
  void fillFromFile();
  void fillTone();
  void fillConnect();
  void goIdle(bool finished);

  fs::FS* fs_ = nullptr;
  int sdPin_ = -1;
  bool ok_ = false;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;

  // Task-owned state
  Mode mode_ = Mode::Idle;
  File file_;
  uint32_t currentRate_ = 0;
  uint16_t fileChannels_ = 1;
  uint16_t fileBits_ = 16;
  uint32_t bytesLeft_ = 0;
  uint32_t dataOffset_ = 0;
  uint32_t dataSize_ = 0;
  bool loop_ = false;
  float phase_ = 0.f;
  float phaseStep_ = 0.f;
  uint32_t toneOnSamples_ = 0, toneOffSamples_ = 0, tonePos_ = 0;

  // Connect sequence: digits (<=16) x 2 + rings (<=8) x 2 + 5 fixed parts.
  static constexpr size_t kMaxSegments = 64;
  callprogress::Segment segs_[kMaxSegments];
  size_t segCount_ = 0, segIndex_ = 0;
  uint32_t segPos_ = 0, segLen_ = 0;  // samples into / total of current segment
  float phase2_ = 0.f, phaseStep2_ = 0.f;
  float noiseLp_ = 0.f;
  uint32_t noiseSeed_ = 0x12345678;

  volatile bool playing_ = false;
  volatile bool finished_ = false;
  volatile bool connectFinished_ = false;
  volatile uint32_t underruns_ = 0;
  uint32_t lastWriteMs_ = 0;
  volatile int32_t volumeQ15_ = 0;
  uint8_t volumePercent_ = 0;

  static constexpr size_t kDmaFrames = 256;   // per DMA buffer
  static constexpr size_t kDmaBuffers = 16;   // 16 x 256 = 256 ms of headroom at 16 kHz
  static constexpr size_t kFrames = 1024;     // frames per file read (4 KB for 16-bit stereo)
  int16_t mono_[kFrames];
  int16_t stereo_[kFrames * 2];
  uint8_t raw_[kFrames * 4];
};
