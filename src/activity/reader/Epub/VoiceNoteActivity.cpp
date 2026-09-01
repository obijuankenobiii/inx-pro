#include "VoiceNoteActivity.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <SDCardManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstring>

#include "images/MicOn.h"
#include "system/MappedInputManager.h"

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr size_t kReadSamples = 256;
constexpr uint32_t kMaxRecordingBytes = kSampleRate * sizeof(int16_t) * 60;
constexpr uint32_t kCaptureTaskStackWords = 4096;
constexpr int kRecordButtonRadius = 86;
constexpr int kMicIconWidth = 72;
constexpr int kMicIconHeight = 75;
constexpr int kWaveBarWidth = 6;
constexpr int kWaveBarGap = 8;
constexpr int kWaveBarCount = 7;

int recordButtonCenterX(const GfxRenderer& renderer) { return renderer.getScreenWidth() / 2; }

int recordButtonCenterY(const GfxRenderer& renderer) { return renderer.getScreenHeight() / 2; }

bool isRecordButtonTap(const GfxRenderer& renderer, const int x, const int y) {
  const int dx = x - recordButtonCenterX(renderer);
  const int dy = y - recordButtonCenterY(renderer);
  constexpr int hitRadius = kRecordButtonRadius + 24;
  return dx * dx + dy * dy <= hitRadius * hitRadius;
}

void putLe16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void putLe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

void writeWavHeader(FsFile& file, uint32_t dataBytes) {
  uint8_t h[44] = {};
  memcpy(h + 0, "RIFF", 4);
  putLe32(h + 4, 36u + dataBytes);
  memcpy(h + 8, "WAVEfmt ", 8);
  putLe32(h + 16, 16);
  putLe16(h + 20, 1);
  putLe16(h + 22, 1);
  putLe32(h + 24, kSampleRate);
  putLe32(h + 28, kSampleRate * sizeof(int16_t));
  putLe16(h + 32, sizeof(int16_t));
  putLe16(h + 34, 16);
  memcpy(h + 36, "data", 4);
  putLe32(h + 40, dataBytes);
  file.seek(0);
  file.write(h, sizeof(h));
}

void correctDcOffset(int16_t* samples, size_t sampleCount, int32_t& previousInput, int32_t& previousOutput) {
  constexpr int dcFilterShift = 10;
  for (size_t index = 0; index < sampleCount; ++index) {
    const int32_t input = static_cast<int32_t>(samples[index]) << 16;
    const int32_t output = input - previousInput +
                           (previousOutput - (previousOutput >> dcFilterShift));
    previousInput = input;
    previousOutput = output;
    const int32_t pcm = output >> 16;
    samples[index] = static_cast<int16_t>(std::clamp(pcm, static_cast<int32_t>(-32768), static_cast<int32_t>(32767)));
  }
}
}

VoiceNoteActivity::VoiceNoteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     std::string outputDirectory,
                                     std::function<void(const std::string&, bool)> onComplete,
                                     std::function<void()> onCancel)
    : Activity("VoiceNote", renderer, mappedInput),
      outputDirectory_(std::move(outputDirectory)),
      onComplete_(std::move(onComplete)),
      onCancel_(std::move(onCancel)) {}

VoiceNoteActivity::~VoiceNoteActivity() { stopCapture(false); }

void VoiceNoteActivity::onEnter() {
  Activity::onEnter();
  SdMan.mkdir(outputDirectory_.c_str());
  audioPath_ = outputDirectory_ + "/voice_" + std::to_string(millis()) + ".wav";

  FsFile* file = new FsFile();
  if (!SdMan.openFileForWrite("VOICE", audioPath_.c_str(), *file)) {
    delete file;
    audioPath_.clear();
    render();
    return;
  }
  uint8_t zeroHeader[44] = {};
  file->write(zeroHeader, sizeof(zeroHeader));
  outputFile_ = file;
  render();
}

void VoiceNoteActivity::onExit() {
  stopCapture(false);
  Activity::onExit();
}

bool VoiceNoteActivity::startCapture() {
  if (recording_) return true;
  auto* file = static_cast<FsFile*>(outputFile_);
  if (!file) return false;

  if (!captureBuffer_) {
    captureBuffer_ = static_cast<uint8_t*>(ps_malloc(kMaxRecordingBytes));
    if (!captureBuffer_) return false;
  }
  const auto releaseCaptureBuffer = [this]() {
    if (captureBuffer_) {
      free(captureBuffer_);
      captureBuffer_ = nullptr;
    }
  };

  if (!microphone_.begin(kSampleRate)) {
    releaseCaptureBuffer();
    return false;
  }
  micStarted_ = true;
  audioBytes_ = 0;
  audioLevel_ = 0;
  displayedAudioLevel_ = 0;
  captureError_ = false;
  captureLimitReached_ = false;
  captureTaskDone_ = false;
  file->seek(44);
  recording_ = true;
  if (xTaskCreatePinnedToCore(captureTaskEntry, "voice_capture", kCaptureTaskStackWords, this, 2, &captureTask_, 1) !=
      pdPASS) {
    recording_ = false;
    captureTaskDone_ = true;
    microphone_.end();
    micStarted_ = false;
    releaseCaptureBuffer();
    return false;
  }
  return true;
}

void VoiceNoteActivity::captureTaskEntry(void* context) {
  auto* activity = static_cast<VoiceNoteActivity*>(context);
  int16_t samples[kReadSamples];
  uint32_t blockCount = 0;
  int32_t previousInput = 0;
  int32_t previousOutput = 0;

  while (activity->recording_ && activity->audioBytes_ < kMaxRecordingBytes) {
    const int samplesRead = activity->microphone_.read(samples, kReadSamples, 100);
    if (samplesRead <= 0) {
      continue;
    }
    const size_t bytesRead = static_cast<size_t>(samplesRead) * sizeof(int16_t);

    correctDcOffset(samples, bytesRead / sizeof(int16_t), previousInput, previousOutput);

    int32_t peak = 0;
    uint32_t absoluteSum = 0;
    for (size_t index = 0; index < bytesRead / sizeof(int16_t); ++index) {
      const int32_t value = samples[index];
      const uint32_t absolute = static_cast<uint32_t>(value < 0 ? -value : value);
      peak = std::max(peak, static_cast<int32_t>(absolute));
      absoluteSum += absolute;
    }
    activity->audioLevel_ = absoluteSum / (bytesRead / sizeof(int16_t));
    ++blockCount;
    if ((blockCount & 31u) == 0) {
      INX_SERIAL.printf("[%lu] [VOICE-PCM] bytes=%lu peak=%ld first=%d\n", millis(),
                        static_cast<unsigned long>(activity->audioBytes_), static_cast<long>(peak), samples[0]);
    }

    const uint32_t recordedBytes = activity->audioBytes_;
    const uint32_t allowed = kMaxRecordingBytes - std::min(kMaxRecordingBytes, recordedBytes);
    const size_t toWrite = std::min<size_t>(bytesRead, allowed);
    if (toWrite == 0 || !activity->captureBuffer_) {
      activity->captureError_ = true;
      activity->recording_ = false;
      break;
    }
    memcpy(activity->captureBuffer_ + recordedBytes, samples, toWrite);
    activity->audioBytes_ += static_cast<uint32_t>(toWrite);
    if (activity->audioBytes_ >= kMaxRecordingBytes) {
      activity->captureLimitReached_ = true;
      activity->recording_ = false;
    }
  }

  activity->captureTaskDone_ = true;
  vTaskDelete(nullptr);
}

void VoiceNoteActivity::stopCapture(const bool keepFile) {
  recording_ = false;
  while (captureTask_ != nullptr && !captureTaskDone_) {
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  captureTask_ = nullptr;
  if (micStarted_) {
    microphone_.end();
    micStarted_ = false;
  }
  auto* file = static_cast<FsFile*>(outputFile_);
  if (!file) {
    if (captureBuffer_) {
      free(captureBuffer_);
      captureBuffer_ = nullptr;
    }
    return;
  }
  if (keepFile) {
    file->seek(44);
    constexpr size_t kWriteChunk = 32 * 1024;
    size_t written = 0;
    while (written < audioBytes_) {
      const size_t chunk = std::min(kWriteChunk, static_cast<size_t>(audioBytes_ - written));
      if (!captureBuffer_ || file->write(captureBuffer_ + written, chunk) != chunk) {
        captureError_ = true;
        break;
      }
      written += chunk;
    }
    writeWavHeader(*file, audioBytes_);
    file->close();
  } else {
    file->close();
    if (!audioPath_.empty()) SdMan.remove(audioPath_.c_str());
  }
  delete file;
  outputFile_ = nullptr;
  if (captureBuffer_) {
    free(captureBuffer_);
    captureBuffer_ = nullptr;
  }
}

void VoiceNoteActivity::finish(const bool success) {
  if (complete_) return;
  stopCapture(success);
  succeeded_ = success && !audioPath_.empty();
  complete_ = true;
  if (succeeded_) {
    if (onComplete_) onComplete_(audioPath_, true);
  } else if (onCancel_) {
    onCancel_();
  }
}

void VoiceNoteActivity::loop() {
  if (complete_) return;

  if (captureError_) {
    finish(false);
    return;
  }
  if (captureLimitReached_) {
    finish(true);
    return;
  }

  bool tapped = false;
  float nx = 0.0f;
  float ny = 0.0f;
  if (mappedInput.hasTouch() && mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
    const int x = static_cast<int>(nx * renderer.getScreenWidth());
    const int y = static_cast<int>(ny * renderer.getScreenHeight());
    if (isRecordButtonTap(renderer, x, y)) {
      tapped = true;
    } else {
      finish(false);
      return;
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) tapped = true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish(false);
    return;
  }
  if (tapped) {
    if (!recording_) {
      startCapture();
      render();
    } else {
      finish(audioBytes_ > 0);
    }
  }
  if (millis() - lastRenderMs_ > 250) render();
}

void VoiceNoteActivity::render() const {
  renderer.syncWriteBufferFromActive();
  const int centerX = recordButtonCenterX(renderer);
  const int centerY = recordButtonCenterY(renderer);

  renderer.circle.render(centerX, centerY, kRecordButtonRadius, true);
  if (!recording_) {
    renderer.bitmap.iconScaled(MicOn, centerX - kMicIconWidth / 2, centerY - kMicIconHeight / 2, 108, 112,
                               kMicIconWidth, kMicIconHeight, BitmapRender::Orientation::None, true);
  } else {
    constexpr uint32_t kFullScaleVisualLevel = 2048;
    const uint32_t measuredLevel = audioLevel_;
    if (measuredLevel > displayedAudioLevel_) {
      displayedAudioLevel_ += std::max<uint32_t>(1, (measuredLevel - displayedAudioLevel_) / 2);
    } else if (displayedAudioLevel_ > measuredLevel) {
      displayedAudioLevel_ -= std::max<uint32_t>(1, (displayedAudioLevel_ - measuredLevel) / 6);
    }
    const uint32_t level = std::min(displayedAudioLevel_, kFullScaleVisualLevel);
    const int signal = static_cast<int>((level * 100u) / kFullScaleVisualLevel);
    static constexpr uint8_t kWaveShape[kWaveBarCount] = {26, 52, 78, 100, 70, 46, 24};
    const uint32_t phase = (millis() / 250u) % kWaveBarCount;
    const int totalWidth = kWaveBarCount * kWaveBarWidth + (kWaveBarCount - 1) * kWaveBarGap;
    const int firstX = centerX - totalWidth / 2;
    const int maximumHeight = 20 + signal * 55 / 100;
    for (int index = 0; index < kWaveBarCount; ++index) {
      const int shape = kWaveShape[(index + phase) % kWaveBarCount];
      const int height = 8 + maximumHeight * shape / 100;
      renderer.rectangle.fill(firstX + index * (kWaveBarWidth + kWaveBarGap), centerY - height / 2,
                              kWaveBarWidth, height, false);
    }
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
