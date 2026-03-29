/**
 * @file PageNote.cpp
 * @brief Page-note recording, storage, indicator, and transcription UI.
 */

#include "EpubActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <time.h>

#include "EpubAnnotationStorage.h"
#include "EpubAnnotations.h"
#include "GeminiTranscription.h"
#if FREEINK_CAP_MIC
#include "VoiceNoteActivity.h"
#endif
#include "activity/page/components/global/Button.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "images/PageNoteIcon.h"
#include "system/Fonts.h"

namespace {
constexpr unsigned long kPageNoteWordSelectionHoldMs = 500;
constexpr int kPageNoteIconSize = 24;
constexpr int kPageNoteEdgePadding = 10;
constexpr int kPageNoteHitRadius = 25;

bool isPageNoteRecord(const EpubAnnotationRecord& record) {
  return record.text.empty() && record.startSpine != EpubAnnotations::kWildcard &&
         record.endSpine != EpubAnnotations::kWildcard && record.pageWordLo == EpubAnnotations::kWildcard &&
         record.pageWordHi == EpubAnnotations::kWildcard &&
         (!record.noteAudioPath.empty() || !record.note.empty());
}

int pageNoteCenterX(const GfxRenderer& renderer) {
  return renderer.getScreenWidth() - kPageNoteEdgePadding - (kPageNoteIconSize + 1) / 2;
}

int pageNoteCenterY(const GfxRenderer& renderer) {
  return renderer.getScreenHeight() - kPageNoteEdgePadding - (kPageNoteIconSize + 1) / 2;
}

void pageNotePopupBounds(const GfxRenderer& renderer, int& x, int& y, int& width, int& height) {
  // Page notes use a bottom sheet so the reader page remains the visual context.
  width = renderer.getScreenWidth();
  height = 240;
  x = 0;
  y = renderer.getScreenHeight() - height;
}
}  // namespace

void EpubActivity::startVoiceNoteForPage() {
#if !FREEINK_CAP_MIC
  // X4 Pro has no microphone. Use the shared keyboard entry for page notes.
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "Add note", "", 10, 256, false,
      [this](const std::string& note) {
        exitActivity();
        if (!note.empty()) {
          savePageNoteText(note);
        }
        updateRequired = true;
      },
      [this]() {
        exitActivity();
        updateRequired = true;
      }));
  return;
#else
  if (!epub || !section) {
    return;
  }
  pageNoteVoiceCompletionPending_ = false;
  pageNoteVoiceSuccess_ = false;
  pageNoteVoicePath_.clear();
  const std::string voiceDirectory = epub->getCachePath() + "/voice";
  enterNewActivity(new VoiceNoteActivity(
      renderer, mappedInput, voiceDirectory,
      [this](const std::string& audioPath, const bool success) {
        INX_SERIAL.printf("[%lu] [VOICE-NOTE] page captured path=%s success=%d\n", millis(), audioPath.c_str(),
                          success ? 1 : 0);
        // Do not destroy VoiceNoteActivity from inside its own finish()/loop()
        // call. The parent consumes this completion after the child returns.
        pageNoteVoicePath_ = audioPath;
        pageNoteVoiceSuccess_ = success;
        pageNoteVoiceCompletionPending_ = true;
      },
      [this]() {
        pageNoteVoicePath_.clear();
        pageNoteVoiceSuccess_ = false;
        pageNoteVoiceCompletionPending_ = true;
      }));
#endif  // FREEINK_CAP_MIC
}

void EpubActivity::savePageNoteAudio(const std::string& audioPath) {
  if (!epub || !section || audioPath.empty()) {
    return;
  }

  EpubAnnotationRecord record;
  record.timestamp = static_cast<uint32_t>(time(nullptr));
  record.startSpine = static_cast<uint16_t>(currentSpineIndex);
  record.endSpine = record.startSpine;
  record.startPage = static_cast<uint16_t>(std::max(0, section->currentPage));
  record.endPage = record.startPage;
  record.pageWordLo = EpubAnnotations::kWildcard;
  record.pageWordHi = EpubAnnotations::kWildcard;
  record.startPageWordLo = EpubAnnotations::kWildcard;
  record.startPageWordHi = EpubAnnotations::kWildcard;
  record.noteAudioPath = audioPath;

  if (!annUi_.annotations().appendHighlight(epub->getCachePath(), epub->getSpineItemsCount(), record,
                                            currentSpineIndex, section->currentPage)) {
    readerPopup("Could not save note");
    return;
  }
  annUi_.annotations().ensurePageLoaded(epub->getCachePath(), currentSpineIndex, section->currentPage);
  updateRequired = true;
}

void EpubActivity::savePageNoteText(const std::string& text) {
  if (!epub || !section || text.empty()) {
    return;
  }

  EpubAnnotationRecord record;
  record.timestamp = static_cast<uint32_t>(time(nullptr));
  record.startSpine = static_cast<uint16_t>(currentSpineIndex);
  record.endSpine = record.startSpine;
  record.startPage = static_cast<uint16_t>(std::max(0, section->currentPage));
  record.endPage = record.startPage;
  record.pageWordLo = EpubAnnotations::kWildcard;
  record.pageWordHi = EpubAnnotations::kWildcard;
  record.startPageWordLo = EpubAnnotations::kWildcard;
  record.startPageWordHi = EpubAnnotations::kWildcard;
  record.note = text;

  if (!annUi_.annotations().appendHighlight(epub->getCachePath(), epub->getSpineItemsCount(), record,
                                            currentSpineIndex, section->currentPage)) {
    readerPopup("Could not save note");
    return;
  }
  annUi_.annotations().ensurePageLoaded(epub->getCachePath(), currentSpineIndex, section->currentPage);
  updateRequired = true;
}

bool EpubActivity::consumePageNoteVoiceCompletion() {
  if (!pageNoteVoiceCompletionPending_) {
    return false;
  }

  const bool success = pageNoteVoiceSuccess_;
  const std::string audioPath = pageNoteVoicePath_;
  pageNoteVoiceCompletionPending_ = false;
  pageNoteVoiceSuccess_ = false;
  pageNoteVoicePath_.clear();

  // VoiceNoteActivity has returned from its loop at this point, so it is now
  // safe to destroy it and continue the page-note workflow.
  exitActivity();
  if (success) {
    savePageNoteAudio(audioPath);
  } else {
    readerPopup("Could not record note");
  }
  updateRequired = true;
  return true;
}

bool EpubActivity::handlePageNoteOverlay() {
  if (!pageNotePopupOpen_) {
    return false;
  }
  if (pageNoteTranscriptionPending_) {
    pollPageNoteTranscription();
  }
  handlePageNotePopupInput();
  return true;
}

void EpubActivity::resetPageNoteState() {
  pageNotePopupOpen_ = false;
  pageNoteTranscriptionPending_ = false;
  pageNoteVoiceCompletionPending_ = false;
  pageNoteVoiceSuccess_ = false;
  pageNoteVoicePath_.clear();
}

const EpubAnnotationRecord* EpubActivity::currentPageNote() const {
  if (!epub || !section) {
    return nullptr;
  }
  for (const EpubAnnotationRecord& record : annUi_.annotations().records()) {
    if (isPageNoteRecord(record) &&
        EpubAnnotations::recordTouchesPage(record, currentSpineIndex, section->currentPage)) {
      return &record;
    }
  }
  return nullptr;
}

bool EpubActivity::handlePageNoteTouch() {
  if (pageNotePopupOpen_ || !currentPageNote() || !mappedInput.hasTouch()) {
    return false;
  }

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
    return false;
  }
  if (mappedInput.lastTouchHeldMs() >= kPageNoteWordSelectionHoldMs) {
    mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
    return false;
  }

  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  const int centerX = pageNoteCenterX(renderer);
  const int centerY = pageNoteCenterY(renderer);
  const int dx = x - centerX;
  const int dy = y - centerY;
  if (dx * dx + dy * dy > kPageNoteHitRadius * kPageNoteHitRadius) {
    mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
    return false;
  }

  pageNoteRecord_ = *currentPageNote();
  pageNoteCachePath_ = epub->getCachePath();
  pageNotePopupOpen_ = true;
  pageNoteTranscriptionPending_ = false;
  pageNoteDots_ = 1;
  pageNoteLastRefreshMs_ = millis();
  renderPageNotePopup();
  return true;
}

void EpubActivity::startPageNoteTranscription() {
  if (pageNoteTranscriptionPending_ || pageNoteRecord_.noteAudioPath.empty()) {
    return;
  }
  if (!GeminiTranscription::start(pageNoteRecord_.noteAudioPath)) {
    return;
  }
  pageNoteTranscriptionPending_ = true;
  pageNoteDots_ = 1;
  pageNoteLastRefreshMs_ = millis();
  renderPageNotePopup();
}

void EpubActivity::pollPageNoteTranscription() {
  const GeminiTranscription::Result result = GeminiTranscription::poll();
  if (!result.finished) {
    if (millis() - pageNoteLastRefreshMs_ >= 350) {
      pageNoteLastRefreshMs_ = millis();
      pageNoteDots_ = pageNoteDots_ >= 3 ? 1 : static_cast<uint8_t>(pageNoteDots_ + 1);
      renderPageNotePopup();
    }
    return;
  }

  pageNoteTranscriptionPending_ = false;
  if (result.success && !result.transcript.empty()) {
    EpubAnnotationRecord updated = pageNoteRecord_;
    updated.note = result.transcript;
    if (EpubAnnotationStorage::update(pageNoteCachePath_, pageNoteRecord_, updated)) {
      pageNoteRecord_ = updated;
      if (section) {
        // ensurePageLoaded() intentionally skips the read when the same shard
        // is cached, so invalidate that in-memory shard after updating it.
        annUi_.annotations().clearSession();
        annUi_.annotations().ensurePageLoaded(pageNoteCachePath_, currentSpineIndex, section->currentPage);
      }
    }
  }
  renderPageNotePopup();
}

void EpubActivity::renderPageNotePopup() {
  if (!pageNotePopupOpen_) {
    return;
  }

  int popupX = 0;
  int popupY = 0;
  int popupWidth = 0;
  int popupHeight = 0;
  pageNotePopupBounds(renderer, popupX, popupY, popupWidth, popupHeight);

  renderer.syncWriteBufferFromActive();
  renderer.rectangle.fill(popupX, popupY, popupWidth, popupHeight, false);
  renderer.rectangle.render(popupX, popupY, popupWidth, popupHeight, true);

  const int font = systemFontId();
  const int padding = 24;
  const int textWidth = popupWidth - padding * 2;
  const int lineHeight = renderer.text.getLineHeight(font);
  renderer.text.render(font, popupX + padding, popupY + 20, "Note", true, EpdFontFamily::BOLD);

  if (!pageNoteRecord_.note.empty()) {
    std::string remaining = pageNoteRecord_.note;
    int textY = popupY + 52;
    for (int line = 0; line < 6 && !remaining.empty(); ++line) {
      size_t length = remaining.size();
      while (length > 0 && renderer.text.getWidth(font, remaining.substr(0, length).c_str()) > textWidth) {
        --length;
      }
      if (length == 0) {
        break;
      }
      if (length < remaining.size()) {
        const size_t space = remaining.rfind(' ', length - 1);
        if (space != std::string::npos && space > 0) {
          length = space;
        }
      }
      renderer.text.render(font, popupX + padding, textY, remaining.substr(0, length).c_str(), true);
      remaining.erase(0, length);
      while (!remaining.empty() && remaining.front() == ' ') {
        remaining.erase(remaining.begin());
      }
      textY += lineHeight + 5;
    }
  } else {
    const char* label = "Transcribe note";
    const int buttonWidth = Button::width(renderer, label, font);
    constexpr int popupRightPadding = 20;
    constexpr int popupBottomPadding = 20;
    const int buttonX = renderer.getScreenWidth() - popupRightPadding - buttonWidth;
    const int buttonY = popupY + popupHeight - Button::height - popupBottomPadding;
    Button::render(renderer, {buttonX, buttonY, buttonWidth, Button::height},
                   pageNoteTranscriptionPending_ ? "" : label, true, font);
    if (pageNoteTranscriptionPending_) {
      constexpr int dotSize = 6;
      constexpr int dotGap = 8;
      const int dotsWidth = pageNoteDots_ * dotSize + (pageNoteDots_ - 1) * dotGap;
      const int dotsX = buttonX + (buttonWidth - dotsWidth) / 2;
      const int dotsY = buttonY + (Button::height - dotSize) / 2;
      for (int dot = 0; dot < pageNoteDots_; ++dot) {
        renderer.rectangle.fill(dotsX + dot * (dotSize + dotGap), dotsY, dotSize, dotSize, false);
      }
    }
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubActivity::handlePageNotePopupInput() {
  if (!pageNotePopupOpen_) {
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasTouchSwipeUp() ||
      mappedInput.wasTouchSwipeDown() || mappedInput.wasTouchSwipeLeft() || mappedInput.wasTouchSwipeRight()) {
    pageNotePopupOpen_ = false;
    pageNoteTranscriptionPending_ = false;
    renderScreen(true);
    return;
  }

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
    return;
  }

  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  int popupX = 0;
  int popupY = 0;
  int popupWidth = 0;
  int popupHeight = 0;
  pageNotePopupBounds(renderer, popupX, popupY, popupWidth, popupHeight);
  if (x < popupX || x >= popupX + popupWidth || y < popupY || y >= popupY + popupHeight) {
    pageNotePopupOpen_ = false;
    pageNoteTranscriptionPending_ = false;
    renderScreen(true);
    return;
  }

  if (pageNoteTranscriptionPending_ || !pageNoteRecord_.note.empty()) {
    return;
  }

  const int font = systemFontId();
  const int buttonWidth = Button::width(renderer, "Transcribe note", font);
  constexpr int popupRightPadding = 20;
  constexpr int popupBottomPadding = 20;
  const int buttonX = renderer.getScreenWidth() - popupRightPadding - buttonWidth;
  const int buttonY = popupY + popupHeight - Button::height - popupBottomPadding;
  if (x >= buttonX && x < buttonX + buttonWidth && y >= buttonY && y < buttonY + Button::height) {
    startPageNoteTranscription();
  }
}

void EpubActivity::drawPageNoteIndicator() {
  const int centerX = pageNoteCenterX(renderer);
  const int centerY = pageNoteCenterY(renderer);

  // Use the supplied notes artwork directly at 20x20, flush with the bottom-right edge.
  const int iconX = centerX - kPageNoteIconSize / 2;
  const int iconY = centerY - kPageNoteIconSize / 2;
  renderer.bitmap.icon(PageNoteIcon, iconX, iconY, kPageNoteIconSize, kPageNoteIconSize,
                       BitmapRender::Orientation::None);
}
