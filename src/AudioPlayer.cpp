#include "AudioPlayer.h"

#include <math.h>
#include <string.h>

#include "WavHeader.h"
#include "config.h"
#include "driver/i2s.h"

static constexpr i2s_port_t kPort = I2S_NUM_0;
static constexpr uint32_t kToneRate = 16000;

bool AudioPlayer::begin(int bclkPin, int lrcPin, int dinPin, int sdPin, fs::FS& fs) {
  fs_ = &fs;
  sdPin_ = sdPin;
  if (sdPin_ >= 0) {
    pinMode(sdPin_, OUTPUT);
    digitalWrite(sdPin_, HIGH);  // amp on, left channel (we send L == R)
  }

  i2s_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kToneRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = kDmaBuffers;
  cfg.dma_buf_len = kDmaFrames;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins;
  memset(&pins, 0, sizeof(pins));
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = bclkPin;
  pins.ws_io_num = lrcPin;
  pins.data_out_num = dinPin;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kPort, &pins) != ESP_OK) return false;
  currentRate_ = kToneRate;
  i2s_zero_dma_buffer(kPort);

  queue_ = xQueueCreate(4, sizeof(Cmd));
  if (!queue_) return false;
  ok_ = xTaskCreatePinnedToCore(taskEntry, "audio", 8192, this, 2, &taskHandle_, 0) == pdPASS;
  return ok_;
}

void AudioPlayer::setVolume(uint8_t percent) {
  if (percent > AUDIO_MAX_VOLUME) percent = AUDIO_MAX_VOLUME;
  volumePercent_ = percent;
  volumeQ15_ = (int32_t)percent * 32767 / 100;
}

bool AudioPlayer::playFile(const char* path, bool loop) {
  if (!fs_ || !fs_->exists(path)) return false;
  Cmd c{};
  c.type = CmdType::File;
  c.loop = loop;
  strncpy(c.path, path, sizeof(c.path) - 1);
  playing_ = true;  // optimistic; task clears it if the header is bad
  xQueueSend(queue_, &c, portMAX_DELAY);
  return true;
}

void AudioPlayer::playTone(uint16_t hz, uint16_t onMs, uint16_t offMs) {
  Cmd c{};
  c.type = CmdType::Tone;
  c.hz = hz;
  c.onMs = onMs;
  c.offMs = offMs;
  playing_ = true;
  xQueueSend(queue_, &c, portMAX_DELAY);
}

void AudioPlayer::playConnect(const char* digits, const callprogress::Timing& timing) {
  Cmd c{};
  c.type = CmdType::Connect;
  strncpy(c.path, digits ? digits : "", sizeof(c.path) - 1);
  c.timing = timing;
  playing_ = true;
  xQueueSend(queue_, &c, portMAX_DELAY);
}

void AudioPlayer::stop() {
  Cmd c{};
  c.type = CmdType::Stop;
  xQueueSend(queue_, &c, portMAX_DELAY);
}

bool AudioPlayer::takeFinished() {
  if (!finished_) return false;
  finished_ = false;
  return true;
}

bool AudioPlayer::takeConnectFinished() {
  if (!connectFinished_) return false;
  connectFinished_ = false;
  return true;
}

// ----------------------------------------------------------------- task side

void AudioPlayer::taskEntry(void* arg) { static_cast<AudioPlayer*>(arg)->task(); }

void AudioPlayer::task() {
  for (;;) {
    Cmd c;
    TickType_t wait = (mode_ == Mode::Idle) ? portMAX_DELAY : 0;
    if (xQueueReceive(queue_, &c, wait) == pdTRUE) apply(c);
    switch (mode_) {
      case Mode::File: fillFromFile(); break;
      case Mode::Tone: fillTone(); break;
      case Mode::Connect: fillConnect(); break;
      case Mode::Idle: break;
    }
  }
}

void AudioPlayer::goIdle(bool finished) {
  Mode was = mode_;
  if (file_) file_.close();
  lastWriteMs_ = 0;
  mode_ = Mode::Idle;
  // On a natural end let the DMA queue drain (tx_desc_auto_clear zeroes each
  // buffer after it is sent), so the tail is not cut and whatever is queued
  // next follows without a gap. Only a stop() silences immediately.
  if (!finished) i2s_zero_dma_buffer(kPort);
  playing_ = false;
  if (finished) {
    if (was == Mode::Connect) connectFinished_ = true;
    else finished_ = true;
  }
}

void AudioPlayer::setRate(uint32_t rate) {
  if (rate != currentRate_) {
    i2s_set_sample_rates(kPort, rate);
    currentRate_ = rate;
  }
}

void AudioPlayer::apply(const Cmd& c) {
  if (file_) file_.close();
  mode_ = Mode::Idle;

  switch (c.type) {
    case CmdType::Stop:
      goIdle(false);
      break;

    case CmdType::Connect:
      lastWriteMs_ = 0;
      setRate(kToneRate);
      segCount_ = callprogress::build(c.path, c.timing, segs_, kMaxSegments);
      segIndex_ = 0;
      segPos_ = 0;
      segLen_ = 0;
      mode_ = Mode::Connect;
      playing_ = true;
      break;

    case CmdType::Tone:
      lastWriteMs_ = 0;
      setRate(kToneRate);
      phase_ = 0.f;
      phaseStep_ = 2.f * (float)M_PI * (float)c.hz / (float)kToneRate;
      toneOnSamples_ = (uint32_t)c.onMs * kToneRate / 1000;
      toneOffSamples_ = (uint32_t)c.offMs * kToneRate / 1000;
      tonePos_ = 0;
      mode_ = Mode::Tone;
      playing_ = true;
      break;

    case CmdType::File: {
      file_ = fs_->open(c.path, "r");
      uint8_t hdr[512];
      size_t n = file_ ? file_.read(hdr, sizeof(hdr)) : 0;
      WavInfo info;
      if (n == 0 || !wav::parse(hdr, n, info)) {
        Serial.printf("[audio] %s: not a PCM WAV file\n", c.path);
        goIdle(false);
        break;
      }
      file_.seek(info.dataOffset);
      bytesLeft_ = info.dataSize;
      dataOffset_ = info.dataOffset;
      dataSize_ = info.dataSize;
      loop_ = c.loop;
      fileChannels_ = info.channels;
      fileBits_ = info.bitsPerSample;
      setRate(info.sampleRate);
      lastWriteMs_ = 0;
      mode_ = Mode::File;
      playing_ = true;
      break;
    }
  }
}

void AudioPlayer::writeFrames(const int16_t* mono, size_t n) {
  int32_t vol = volumeQ15_;
  for (size_t i = 0; i < n; ++i) {
    int16_t s = (int16_t)(((int32_t)mono[i] * vol) >> 15);
    stereo_[2 * i] = s;
    stereo_[2 * i + 1] = s;
  }
  // If we arrive later than the DMA queue could cover, the output ran dry.
  uint32_t now = millis();
  uint32_t headroomMs = (uint32_t)(kDmaFrames * (kDmaBuffers - 1) * 1000UL / (currentRate_ ? currentRate_ : 16000));
  if (lastWriteMs_ && (now - lastWriteMs_) > headroomMs) underruns_++;
  size_t written = 0;
  i2s_write(kPort, stereo_, n * 4, &written, portMAX_DELAY);
  lastWriteMs_ = millis();
}

void AudioPlayer::fillFromFile() {
  size_t frameBytes = (size_t)fileChannels_ * (fileBits_ / 8);
  size_t want = kFrames * frameBytes;
  if (want > bytesLeft_) want = bytesLeft_;
  want -= want % frameBytes;
  if (want == 0) {
    if (loop_ && dataSize_ >= frameBytes) {
      file_.seek(dataOffset_);
      bytesLeft_ = dataSize_;
      return;  // next call continues from the top
    }
    goIdle(true);
    return;
  }
  size_t got = file_.read(raw_, want);
  if (got < frameBytes) {
    goIdle(true);
    return;
  }
  bytesLeft_ -= got;
  size_t frames = got / frameBytes;
  for (size_t i = 0; i < frames; ++i) {
    int32_t acc = 0;
    for (uint16_t ch = 0; ch < fileChannels_; ++ch) {
      const uint8_t* p = raw_ + i * frameBytes + ch * (fileBits_ / 8);
      acc += (fileBits_ == 16) ? (int16_t)(p[0] | (p[1] << 8)) : (int16_t)(((int)p[0] - 128) << 8);
    }
    mono_[i] = (int16_t)(acc / fileChannels_);
  }
  writeFrames(mono_, frames);
}

void AudioPlayer::fillTone() {
  uint32_t period = toneOnSamples_ + toneOffSamples_;
  for (size_t i = 0; i < kFrames; ++i) {
    bool on = (toneOffSamples_ == 0) || (tonePos_ < toneOnSamples_);
    mono_[i] = on ? (int16_t)(sinf(phase_) * 12000.f) : 0;
    phase_ += phaseStep_;
    if (phase_ > 2.f * (float)M_PI) phase_ -= 2.f * (float)M_PI;
    if (period) tonePos_ = (tonePos_ + 1) % period;
  }
  writeFrames(mono_, kFrames);
}

// Render the connect sequence one segment at a time: single or dual (DTMF)
// sine tones, silence, and a short decaying noise burst for the pickup click.
void AudioPlayer::fillConnect() {
  using callprogress::Kind;
  size_t n = 0;
  while (n < kFrames && segIndex_ < segCount_) {
    const callprogress::Segment& s = segs_[segIndex_];
    if (segPos_ == 0) {
      segLen_ = (uint32_t)s.ms * kToneRate / 1000;
      phase_ = phase2_ = 0.f;
      phaseStep_ = 2.f * (float)M_PI * (float)s.f1 / (float)kToneRate;
      phaseStep2_ = 2.f * (float)M_PI * (float)s.f2 / (float)kToneRate;
      noiseLp_ = 0.f;
    }
    int16_t v = 0;
    switch (s.kind) {
      case Kind::Silence:
        break;
      case Kind::Tone:
        if (s.f2) {
          v = (int16_t)((sinf(phase_) + sinf(phase2_)) * 6000.f);
          phase2_ += phaseStep2_;
          if (phase2_ > 2.f * (float)M_PI) phase2_ -= 2.f * (float)M_PI;
        } else {
          v = (int16_t)(sinf(phase_) * 12000.f);
        }
        phase_ += phaseStep_;
        if (phase_ > 2.f * (float)M_PI) phase_ -= 2.f * (float)M_PI;
        break;
      case Kind::Noise: {
        noiseSeed_ = noiseSeed_ * 1664525u + 1013904223u;
        float white = (float)((int32_t)(noiseSeed_ >> 16) - 32768) / 32768.f;
        noiseLp_ += 0.35f * (white - noiseLp_);  // dull it a little
        float env = segLen_ ? 1.f - (float)segPos_ / (float)segLen_ : 0.f;
        v = (int16_t)(noiseLp_ * env * env * 14000.f);
        break;
      }
    }
    mono_[n++] = v;
    if (++segPos_ >= segLen_) {
      segPos_ = 0;
      segIndex_++;
    }
  }
  if (n) writeFrames(mono_, n);
  if (segIndex_ >= segCount_) goIdle(true);
}
