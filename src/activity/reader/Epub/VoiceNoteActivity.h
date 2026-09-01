#pragma once

#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

#include "activity/Activity.h"
#include <Microphone.h>

/**
 * Sticky voice-note capture screen.
 *
 * Capture runs separately from the display loop, and transcription is
 * explicitly started later from the saved highlight.
 */
class VoiceNoteActivity final : public Activity {
 public:
  VoiceNoteActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string outputDirectory,
                    std::function<void(const std::string&, bool)> onComplete,
                    std::function<void()> onCancel = nullptr);
  ~VoiceNoteActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;

  bool isComplete() const { return complete_; }
  bool succeeded() const { return succeeded_; }
  const std::string& audioPath() const { return audioPath_; }

 private:
  std::string outputDirectory_;
  std::string audioPath_;
  std::function<void(const std::string&, bool)> onComplete_;
  std::function<void()> onCancel_;
  freeink::Microphone microphone_;
  void* outputFile_ = nullptr;
  bool micStarted_ = false;
  bool recording_ = false;
  bool complete_ = false;
  bool succeeded_ = false;
  volatile bool captureTaskDone_ = true;
  volatile bool captureError_ = false;
  volatile bool captureLimitReached_ = false;
  volatile uint32_t audioBytes_ = 0;
  volatile uint32_t audioLevel_ = 0;
  mutable uint32_t displayedAudioLevel_ = 0;
  uint8_t* captureBuffer_ = nullptr;
  TaskHandle_t captureTask_ = nullptr;
  unsigned long lastRenderMs_ = 0;

  static void captureTaskEntry(void* context);
  bool startCapture();
  void stopCapture(bool keepFile);
  void finish(bool success);
  void render() const;
};
