#include "WordLookup.h"

#include <Arduino.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

#include "EpubActivity.h"
#include "system/FontManager.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int kHitPadding = 8;
constexpr unsigned long kNavEdgeDebounceMs = 130;
constexpr unsigned long kNavRepeatInitialMs = 700;
constexpr unsigned long kNavRepeatIntervalMs = 95;
}

void WordLookup::CaptureBufferDeleter::operator()(uint8_t* buffer) const {
  if (!buffer) return;
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(buffer);
#else
  delete[] buffer;
#endif
}

WordLookup::CaptureBuffer WordLookup::allocateCaptureBuffer(const size_t bytes) {
  if (bytes == 0) return {};
#if defined(ARDUINO_ARCH_ESP32)
  if (void* buffer = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {
    return CaptureBuffer(static_cast<uint8_t*>(buffer));
  }
  return CaptureBuffer(static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT)));
#else
  return CaptureBuffer(new (std::nothrow) uint8_t[bytes]);
#endif
}

bool WordLookup::buildGeometry(EpubActivity& activity) {
  if (!activity.section || !activity.epub) {
    words_.clear();
    lineFirst_.clear();
    focus_ = 0;
    return false;
  }

  const ViewportInfo info = activity.calculateViewport();
  const int fontId = activity.bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  Page* page = activity.section->loadPageFromSectionFile();
  if (!page) {
    words_.clear();
    lineFirst_.clear();
    focus_ = 0;
    return false;
  }

  constexpr bool omitStoredWordStrings = false;
  buildPageWordIndex(*page, activity.renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop, words_,
                     &lineFirst_, omitStoredWordStrings);
  focus_ = words_.empty() ? 0 : std::min(focus_, words_.size() - 1);
  return !words_.empty();
}

void WordLookup::clear() {
  std::vector<PageWordHit>().swap(words_);
  std::vector<size_t>().swap(lineFirst_);
  focus_ = 0;
  lastNavEdgeMs_ = 0;
  lastNavEdgeDir_ = -1;
  navRepeatDir_ = -1;
  navRepeatNextMs_ = 0;

  for (auto& chunk : captureChunks_) {
    chunk.reset();
  }
  std::vector<CaptureBuffer>().swap(captureChunks_);
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;
}

bool WordLookup::focusAt(const int x, const int y) {
  for (size_t i = 0; i < words_.size(); ++i) {
    const PageWordHit& word = words_[i];
    if (x >= word.screenX - kHitPadding && x < word.screenX + word.screenW + kHitPadding &&
        y >= word.screenY - kHitPadding && y < word.screenY + word.screenH + kHitPadding) {
      focus_ = i;
      return true;
    }
  }
  return false;
}

bool WordLookup::isDuplicateNavEdge(const int direction, const unsigned long now) {
  if (lastNavEdgeDir_ == direction && now - lastNavEdgeMs_ < kNavEdgeDebounceMs) {
    return true;
  }
  lastNavEdgeMs_ = now;
  lastNavEdgeDir_ = direction;
  return false;
}

void WordLookup::moveFocusWord(const int delta) {
  if (words_.empty()) {
    return;
  }
  if (delta < 0) {
    if (focus_ > 0) --focus_;
  } else if (focus_ + 1 < words_.size()) {
    ++focus_;
  }
}

void WordLookup::moveFocusLine(const int delta) {
  if (lineFirst_.empty() || words_.empty()) {
    return;
  }
  size_t line = 0;
  for (size_t i = 0; i < lineFirst_.size(); ++i) {
    const size_t start = lineFirst_[i];
    const size_t end = i + 1 < lineFirst_.size() ? lineFirst_[i + 1] : words_.size();
    if (focus_ >= start && focus_ < end) {
      line = i;
      break;
    }
  }
  if (delta < 0) {
    if (line > 0) focus_ = lineFirst_[line - 1];
  } else if (line + 1 < lineFirst_.size()) {
    focus_ = lineFirst_[line + 1];
  }
}

bool WordLookup::handleNavigation(EpubActivity& activity) {
  using Button = MappedInputManager::Button;
  const MappedInputManager& input = activity.mappedInput;
  const unsigned long now = millis();

  const struct {
    Button button;
    int direction;
  } buttons[] = {{Button::Left, 0}, {Button::Right, 1}, {Button::Up, 2}, {Button::Down, 3}};

  for (const auto& entry : buttons) {
    if (!input.wasPressed(entry.button)) continue;
    if (isDuplicateNavEdge(entry.direction, now)) return true;
    if (entry.direction == 0) moveFocusWord(-1);
    if (entry.direction == 1) moveFocusWord(1);
    if (entry.direction == 2) moveFocusLine(-1);
    if (entry.direction == 3) moveFocusLine(1);
    navRepeatDir_ = entry.direction;
    navRepeatNextMs_ = now + kNavRepeatInitialMs;
    return true;
  }

  const bool held[] = {input.isPressed(Button::Left), input.isPressed(Button::Right), input.isPressed(Button::Up),
                       input.isPressed(Button::Down)};
  if (!held[0] && !held[1] && !held[2] && !held[3]) {
    navRepeatDir_ = -1;
    return false;
  }
  if (navRepeatDir_ < 0 || now < navRepeatNextMs_ || !held[navRepeatDir_]) {
    return false;
  }

  if (navRepeatDir_ == 0) moveFocusWord(-1);
  if (navRepeatDir_ == 1) moveFocusWord(1);
  if (navRepeatDir_ == 2) {
    moveFocusLine(-1);
    moveFocusLine(-1);
  }
  if (navRepeatDir_ == 3) {
    moveFocusLine(1);
    moveFocusLine(1);
  }
  navRepeatNextMs_ = now + kNavRepeatIntervalMs;
  return true;
}

bool WordLookup::captureFramebuffer(EpubActivity& activity) {
  for (auto& chunk : captureChunks_) {
    chunk.reset();
  }
  captureMonolithic_.reset();
  captureUsesMonolithic_ = false;
  captureBytes_ = 0;
  captureValid_ = false;

  activity.renderer.resetTransientReaderState();
  activity.renderer.syncWriteBufferFromActive();
  uint8_t* framebuffer = activity.renderer.getFrameBuffer();
  const size_t byteCount = activity.renderer.getBufferSize();
  if (!framebuffer || byteCount == 0) {
    return false;
  }

  const size_t chunks = (byteCount + kCaptureChunkBytes - 1) / kCaptureChunkBytes;
  captureChunks_.resize(chunks);
  for (size_t i = 0; i < chunks; ++i) {
    const size_t offset = i * kCaptureChunkBytes;
    const size_t bytes = std::min(kCaptureChunkBytes, byteCount - offset);
    CaptureBuffer copy = allocateCaptureBuffer(bytes);
    if (!copy) {
      for (auto& chunk : captureChunks_) chunk.reset();
      captureChunks_.clear();
      break;
    }
    std::memcpy(copy.get(), framebuffer + offset, bytes);
    captureChunks_[i] = std::move(copy);
  }

  if (captureChunks_.size() == chunks &&
      std::all_of(captureChunks_.begin(), captureChunks_.end(), [](const auto& chunk) { return chunk != nullptr; })) {
    captureBytes_ = byteCount;
    captureValid_ = true;
    return true;
  }

  captureMonolithic_ = allocateCaptureBuffer(byteCount);
  if (!captureMonolithic_) {
    return false;
  }
  std::memcpy(captureMonolithic_.get(), framebuffer, byteCount);
  captureUsesMonolithic_ = true;
  captureBytes_ = byteCount;
  captureValid_ = true;
  return true;
}

bool WordLookup::restoreFramebuffer(EpubActivity& activity) const {
  const size_t byteCount = activity.renderer.getBufferSize();
  uint8_t* framebuffer = activity.renderer.getFrameBuffer();
  if (!captureValid_ || captureBytes_ != byteCount || !framebuffer) {
    return false;
  }

  activity.renderer.setRenderMode(GfxRenderer::BW);
  if (captureUsesMonolithic_) {
    if (!captureMonolithic_) return false;
    std::memcpy(framebuffer, captureMonolithic_.get(), byteCount);
    return true;
  }

  const size_t chunks = (byteCount + kCaptureChunkBytes - 1) / kCaptureChunkBytes;
  if (captureChunks_.size() != chunks) return false;
  for (size_t i = 0; i < chunks; ++i) {
    const size_t offset = i * kCaptureChunkBytes;
    const size_t bytes = std::min(kCaptureChunkBytes, byteCount - offset);
    if (!captureChunks_[i]) return false;
    std::memcpy(framebuffer + offset, captureChunks_[i].get(), bytes);
  }
  return true;
}
