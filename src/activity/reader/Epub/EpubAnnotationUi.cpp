#include "EpubAnnotationUi.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <climits>
#include <ctime>

#include "EpubActivity.h"
#include "activity/page/components/global/Button.h"
#include "images/Close.h"
#include "images/LibraryFilterRight.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr unsigned long kChordHoldMs = 600;
constexpr int kHighlightLatticeStepPx = 2;
constexpr int kOverlayMargin = 20;
constexpr int kCaretSize = 40;
constexpr int kCaretSourceSize = 30;

ButtonBounds caretBounds(const GfxRenderer& renderer) {
  return {kOverlayMargin, renderer.getScreenHeight() - kOverlayMargin - kCaretSize, kCaretSize, kCaretSize};
}

}

EpubAnnotationUi::EpubAnnotationUi() = default;

void EpubAnnotationUi::setWordIndexCache(const int spine, const int page, const int fontId, const int headerFontId,
                                         const int marginL, const int marginT) {
  wordIndexCacheSpine_ = spine;
  wordIndexCachePage_ = page;
  wordIndexCacheFontId_ = fontId;
  wordIndexCacheHeaderFontId_ = headerFontId;
  wordIndexCacheMarginL_ = marginL;
  wordIndexCacheMarginT_ = marginT;
}

void EpubAnnotationUi::clearWordIndexCache() {
  wordIndexCacheSpine_ = -1;
  wordIndexCachePage_ = -1;
  wordIndexCacheFontId_ = -1;
  wordIndexCacheHeaderFontId_ = -1;
  wordIndexCacheMarginL_ = INT_MIN;
  wordIndexCacheMarginT_ = INT_MIN;
}

void EpubAnnotationUi::clearSessionAndCapture() {
  annotations_.clearSession();
  pendingNoteAudioPath_.clear();
  pendingNoteText_.clear();
  std::vector<std::pair<size_t, size_t>>().swap(pendingSpans_);
  wordLookup_.clear();
  clearWordIndexCache();
}

void EpubAnnotationUi::tryChordEnter(EpubActivity& act) {
  if (!act.epub || !act.section || mode_) {
    return;
  }
  const bool down = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_DOWN);
  const bool right = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_RIGHT);
  if (down && right) {
    if (chordStartMs_ == 0) {
      chordStartMs_ = millis();
    }
    if (!chordConsumed_ && millis() - chordStartMs_ >= kChordHoldMs) {
      enter(act);
      chordConsumed_ = true;
    }
  } else {
    chordStartMs_ = 0;
    chordConsumed_ = false;
  }
}

bool EpubAnnotationUi::hasSaveableContent() const {
  if (!pendingSpans_.empty()) {
    return true;
  }
  if (!selectingStarted_ || words_.empty()) {
    return false;
  }
  const size_t lo = std::min(anchor_, focus_);
  const size_t hi = std::max(anchor_, focus_);
  return lo <= hi;
}

void EpubAnnotationUi::resetSelectionToStart(EpubActivity& act) {
  pendingSpans_.clear();
  selectingStarted_ = false;
  focus_ = 0;
  anchor_ = 0;
  act.updateRequired = true;
}

void EpubAnnotationUi::clearAllStoredHighlightsOnCurrentPage(EpubActivity& act) {
  if (!act.epub || !act.section) {
    return;
  }
  annotations_.clearPageShard(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
  storedRanges_.clear();
  pendingSpans_.clear();
  selectingStarted_ = false;
  wordLookup_.clear();
  anchor_ = 0;
  clearWordIndexCache();
  suppressOverlayDraw_ = true;
  act.renderScreen(true);
  suppressOverlayDraw_ = false;
  captureFramebuffer(act);
  act.updateRequired = true;
}

void EpubAnnotationUi::normalizeSpans(std::vector<std::pair<size_t, size_t>>& spans) {
  if (spans.empty()) {
    return;
  }
  std::sort(spans.begin(), spans.end());
  size_t write = 0;
  auto cur = spans[0];
  for (size_t i = 1; i < spans.size(); ++i) {
    if (spans[i].first <= cur.second + 1) {
      cur.second = std::max(cur.second, spans[i].second);
    } else {
      spans[write++] = cur;
      cur = spans[i];
    }
  }
  spans[write++] = cur;
  spans.resize(write);
}

void EpubAnnotationUi::enter(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  act.btnBindings_.reset();
  mode_ = true;
  controlsVisible_ = true;
  pendingNoteAudioPath_.clear();
  pendingNoteText_.clear();
  selectingStarted_ = false;
  pendingSpans_.clear();
  wordLookup_.clear();
  anchor_ = 0;
  prepareWordGeometry(act);
  if (words_.empty()) {
    act.readerPopup("No text to highlight");
    exit(act);
    return;
  }
  if (!captureFramebuffer(act)) {
    act.readerPopup("Could not capture page");
    exit(act);
    return;
  }
  act.updateRequired = true;
}

bool EpubAnnotationUi::focusAt(const int x, const int y) {
  return wordLookup_.focusAt(x, y);
}

bool EpubAnnotationUi::startAt(EpubActivity& act, const int x, const int y) {
  enter(act);
  if (!mode_ || !focusAt(x, y)) {
    if (mode_) {
      exit(act);
    }
    return false;
  }
  anchor_ = focus_;
  selectingStarted_ = true;
  act.updateRequired = true;
  return true;
}

void EpubAnnotationUi::exit(EpubActivity& act) {
  INX_SERIAL.printf("[%lu] [ANNOTATION] exit spine=%d page=%d selected=%d pending=%u stored=%u\n", millis(),
                act.currentSpineIndex, act.section ? act.section->currentPage : -1, selectingStarted_ ? 1 : 0,
                static_cast<unsigned>(pendingSpans_.size()), static_cast<unsigned>(storedRanges_.size()));
  mode_ = false;
  controlsVisible_ = true;
  pendingNoteAudioPath_.clear();
  pendingNoteText_.clear();
  selectingStarted_ = false;
  std::vector<std::pair<size_t, size_t>>().swap(pendingSpans_);
  std::vector<std::pair<size_t, size_t>>().swap(storedRanges_);
  wordLookup_.clear();
  clearWordIndexCache();
  act.updateRequired = true;
}

bool EpubAnnotationUi::tryNavigationHoldRepeat(EpubActivity& act) {
  if (!wordLookup_.handleNavigation(act)) {
    return false;
  }
  act.updateRequired = true;
  return true;
}

std::string EpubAnnotationUi::extractRangeText(const size_t anchorFlat, const size_t focusFlat) const {
  if (words_.empty()) {
    return {};
  }
  const size_t lo = std::min(anchorFlat, focusFlat);
  const size_t hi = std::max(anchorFlat, focusFlat);
  std::string out;
  for (size_t i = lo; i <= hi && i < words_.size(); ++i) {
    if (!out.empty()) {
      out += ' ';
    }
    out += words_[i].text;
  }
  return out;
}

void EpubAnnotationUi::drawLatticeHighlightRect(EpubActivity& act, const int x, const int y, const int width,
                                                const int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  act.renderer.ui.fillSparseInkLatticeInRect(x, std::max(0, y), width, height, kHighlightLatticeStepPx);
}

void EpubAnnotationUi::drawLatticeHighlightForWordIndexRange(EpubActivity& act, const size_t lo, const size_t hi) {
  if (words_.empty() || lo > hi || hi >= words_.size()) {
    return;
  }
  size_t a = lo;
  while (a <= hi) {
    const int lineY = words_[a].screenY;
    size_t b = a + 1;
    int minX = words_[a].screenX;
    int maxR = words_[a].screenX + words_[a].screenW;
    const int fid0 = words_[a].fontId > 0 ? words_[a].fontId : act.bookSettings.getReaderFontId();
    int rowH = std::max(3, words_[a].screenH > 0 ? words_[a].screenH : act.renderer.text.getLineHeight(fid0));
    while (b <= hi && words_[b].screenY == lineY) {
      minX = std::min(minX, words_[b].screenX);
      maxR = std::max(maxR, words_[b].screenX + words_[b].screenW);
      const int fid = words_[b].fontId > 0 ? words_[b].fontId : act.bookSettings.getReaderFontId();
      const int lh = std::max(3, words_[b].screenH > 0 ? words_[b].screenH : act.renderer.text.getLineHeight(fid));
      rowH = std::max(rowH, lh);
      ++b;
    }
    drawLatticeHighlightRect(act, minX, lineY, std::max(1, maxR - minX), rowH);
    a = b;
  }
}

void EpubAnnotationUi::ensureDiskListLoaded(EpubActivity& act) {
  if (!act.epub || !act.section) {
    return;
  }
  annotations_.ensurePageLoaded(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
}

void EpubAnnotationUi::updateStoredRangesForPage(const EpubActivity& act) {
  if (!act.section) {
    storedRanges_.clear();
    return;
  }
  EpubAnnotations::mergeStoredRangesForPage(annotations_.records(), act.currentSpineIndex, act.section->currentPage,
                                            words_, storedRanges_);
}

void EpubAnnotationUi::clampSelectionToValidWords() {
  if (words_.empty()) {
    pendingSpans_.clear();
    return;
  }
  const size_t last = words_.size() - 1;
  focus_ = std::min(focus_, last);
  if (selectingStarted_) {
    anchor_ = std::min(anchor_, last);
  }
  for (auto& pr : pendingSpans_) {
    pr.first = std::min(pr.first, last);
    pr.second = std::min(pr.second, last);
    if (pr.first > pr.second) {
      std::swap(pr.first, pr.second);
    }
  }
  pendingSpans_.erase(std::remove_if(pendingSpans_.begin(), pendingSpans_.end(),
                                     [](const std::pair<size_t, size_t>& p) { return p.first > p.second; }),
                      pendingSpans_.end());
}

void EpubAnnotationUi::prepareWordGeometry(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  ensureDiskListLoaded(act);
  const ViewportInfo info = act.calculateViewport();
  const int fontId = act.bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  const int mt = info.totalMarginTop;
  const int ml = info.totalMarginLeft;

  const bool wordIndexCacheHit = wordIndexCacheSpine_ == act.currentSpineIndex &&
                                 wordIndexCachePage_ == act.section->currentPage && wordIndexCacheFontId_ == fontId &&
                                 wordIndexCacheHeaderFontId_ == headerFontId && wordIndexCacheMarginL_ == ml &&
                                 wordIndexCacheMarginT_ == mt;

  const bool anyWordText =
      std::any_of(words_.begin(), words_.end(), [](const PageWordHit& w) { return !w.text.empty(); });

  if (wordIndexCacheHit && !words_.empty() && anyWordText) {
    storedRanges_.clear();
    focus_ = std::min(focus_, words_.size() - 1);
    if (selectingStarted_) {
      anchor_ = std::min(anchor_, words_.size() - 1);
    }
    return;
  }

  storedRanges_.clear();
  if (!wordLookup_.buildGeometry(act)) {
    return;
  }
  setWordIndexCache(act.currentSpineIndex, act.section->currentPage, fontId, headerFontId, ml, mt);
}

bool EpubAnnotationUi::captureFramebuffer(EpubActivity& act) {
  return wordLookup_.captureFramebuffer(act);
}

void EpubAnnotationUi::repaint(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  if (!wordLookup_.restoreFramebuffer(act)) {
    act.renderScreen(true);
    return;
  }
  drawUiOverlay(act);
}

void EpubAnnotationUi::drawStoredOverlay(EpubActivity& act) {
  if (storedRanges_.empty()) {
    return;
  }
  act.renderer.setRenderMode(GfxRenderer::BW);
  for (const auto& pr : storedRanges_) {
    drawLatticeHighlightForWordIndexRange(act, pr.first, pr.second);
  }
}

void EpubAnnotationUi::drawHighlights(EpubActivity& act) {
  if (!mode_ || words_.empty()) {
    return;
  }
  for (const auto& pr : pendingSpans_) {
    if (pr.first < words_.size() && pr.second < words_.size() && pr.first <= pr.second) {
      drawLatticeHighlightForWordIndexRange(act, pr.first, pr.second);
    }
  }
  if (selectingStarted_) {
    const size_t lo = std::min(anchor_, focus_);
    const size_t hi = std::max(anchor_, focus_);
    drawLatticeHighlightForWordIndexRange(act, lo, hi);
    return;
  }
  if (focus_ < words_.size()) {
    drawLatticeHighlightForWordIndexRange(act, focus_, focus_);
  }
}

void EpubAnnotationUi::drawUiOverlay(EpubActivity& act) {
  if (!mode_ || suppressOverlayDraw_) {
    return;
  }
  const GfxRenderer::Orientation o = act.renderer.getOrientation();
  drawHighlights(act);
  constexpr int closeSize = 40;
  constexpr int margin = 20;
  const int closeX = act.renderer.getScreenWidth() - margin - closeSize;
  const int closeY = margin;
  if (controlsVisible_) {
    act.renderer.bitmap.icon(Close, closeX, closeY, closeSize, closeSize);
    const int font = systemFontId();
    const int saveWidth = Button::width(act.renderer, "Save", font);
    const ButtonBounds save{act.renderer.getScreenWidth() - margin - saveWidth,
                            act.renderer.getScreenHeight() - margin - Button::height, saveWidth, Button::height};
    Button::render(act.renderer, save, "Save", true, font);
    const int noteWidth = Button::width(act.renderer, "Add note", font);
    const ButtonBounds note{save.x - margin - noteWidth, save.y, noteWidth, Button::height};
    Button::render(act.renderer, note, "Add note", true, font);
  }
  const ButtonBounds caret = caretBounds(act.renderer);
  const auto orientation = controlsVisible_ ? BitmapRender::Orientation::Rotate90CW
                                            : BitmapRender::Orientation::Rotate270CW;
  act.renderer.bitmap.iconScaled(LibraryFilterRight, caret.x, caret.y, kCaretSourceSize, kCaretSourceSize,
                                 kCaretSize, kCaretSize, orientation);
  act.renderer.setOrientation(GfxRenderer::Portrait);
  const char* backHint = hasSaveableContent() ? "Save" : "Exit";
  const char* mid = selectingStarted_ ? "Stop" : "Start";
  const auto labels = act.mappedInput.mapLabels(backHint, mid, "Prev", "Next");
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void EpubAnnotationUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (m.hasTouch() && m.wasTouchSwipeUpForRenderer(act.renderer)) {
    exit(act);
    act.startPageTimer();
    return;
  }

  if (m.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (m.wasTouchTapInScreen(act.renderer, tapNx, tapNy)) {
      constexpr int closeSize = 40;
      constexpr int margin = 20;
      const int closeX = act.renderer.getScreenWidth() - margin - closeSize;
      const int closeY = margin;
      const int x = static_cast<int>(tapNx * act.renderer.getScreenWidth());
      const int y = static_cast<int>(tapNy * act.renderer.getScreenHeight());
      const ButtonBounds caret = caretBounds(act.renderer);
      if (x >= caret.x && x < caret.x + caret.width && y >= caret.y && y < caret.y + caret.height) {
        controlsVisible_ = !controlsVisible_;
        act.updateRequired = true;
        return;
      }

      if (controlsVisible_ && x >= closeX && x < closeX + closeSize && y >= closeY && y < closeY + closeSize) {
        INX_SERIAL.printf("[%lu] [ANNOTATION] close tap=(%d,%d) spine=%d page=%d\n", millis(), x, y,
                      act.currentSpineIndex, act.section ? act.section->currentPage : -1);
        exit(act);
        act.startPageTimer();
        return;
      }

      if (controlsVisible_) {
        const int font = systemFontId();
        const int saveWidth = Button::width(act.renderer, "Save", font);
        const ButtonBounds save{act.renderer.getScreenWidth() - margin - saveWidth,
                                act.renderer.getScreenHeight() - margin - Button::height, saveWidth, Button::height};
        const int noteWidth = Button::width(act.renderer, "Add note", font);
        const ButtonBounds note{save.x - margin - noteWidth, save.y, noteWidth, Button::height};
        INX_SERIAL.printf("[%lu] [ANNOTATION] touch=(%d,%d) save=(%d,%d,%d,%d) spine=%d page=%d\n", millis(), x, y,
                      save.x, save.y, save.width, save.height, act.currentSpineIndex,
                      act.section ? act.section->currentPage : -1);
        if (x >= save.x && x < save.x + save.width && y >= save.y && y < save.y + save.height) {
        INX_SERIAL.printf("[%lu] [ANNOTATION] save tap=(%d,%d) bounds=(%d,%d,%d,%d) spine=%d page=%d\n", millis(), x, y,
                      save.x, save.y, save.width, save.height, act.currentSpineIndex,
                      act.section ? act.section->currentPage : -1);
          saveToStorage(act);
          act.startPageTimer();
          return;
        }
        if (x >= note.x && x < note.x + note.width && y >= note.y && y < note.y + note.height) {
        std::vector<std::pair<size_t, size_t>> spans = pendingSpans_;
        if (selectingStarted_ && !words_.empty()) {
          const size_t lo = std::min(anchor_, focus_);
          const size_t hi = std::max(anchor_, focus_);
          if (lo <= hi) spans.push_back({lo, std::min(hi, words_.size() - 1)});
        }
        normalizeSpans(spans);
        if (spans.empty()) {
          act.readerPopup("Select text first");
          return;
        }
        const size_t lo = spans.front().first;
        const size_t hi = spans.front().second;
        const std::string selectedText = extractRangeText(lo, hi);
        if (selectedText.empty()) {
          act.readerPopup("Select text first");
          return;
        }
        act.startVoiceNoteForSelection(selectedText, static_cast<uint16_t>(std::min<size_t>(lo, 0xFFFEu)),
                                       static_cast<uint16_t>(std::min<size_t>(hi, 0xFFFEu)));
        act.startPageTimer();
          return;
        }
      }

      const size_t oldAnchor = anchor_;
      const size_t oldFocus = focus_;
      if (focusAt(x, y)) {
        const size_t tapped = focus_;
        if (!selectingStarted_) {
          anchor_ = tapped;
          focus_ = tapped;
          selectingStarted_ = true;
        } else {
          const size_t first = std::min(oldAnchor, oldFocus);
          const size_t last = std::max(oldAnchor, oldFocus);
          if (tapped < first) {
            anchor_ = tapped;
            focus_ = last;
          } else {
            anchor_ = first;
            focus_ = tapped;
          }
        }
        act.updateRequired = true;
        return;
      }
    }
  }

  if (m.wasReleased(MappedInputManager::Button::Power)) {
    const unsigned long ht = m.getHeldTime();
    if (ht < 600) {
      const bool hadSavedFile =
          act.epub && act.section &&
          annotations_.pageShardExists(act.epub->getCachePath(), act.currentSpineIndex, act.section->currentPage);
      if (hadSavedFile) {
        clearAllStoredHighlightsOnCurrentPage(act);
      } else {
        resetSelectionToStart(act);
      }
      return;
    }
  }
  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (hasSaveableContent()) {
      saveToStorage(act);
    } else {
      exit(act);
    }
    act.startPageTimer();
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!selectingStarted_) {
      selectingStarted_ = true;
      anchor_ = focus_;
    } else {
      const size_t lo = std::min(anchor_, focus_);
      const size_t hi = std::max(anchor_, focus_);
      if (!words_.empty() && lo <= hi) {
        pendingSpans_.push_back({lo, std::min(hi, words_.size() - 1)});
      }
      selectingStarted_ = false;
    }
    act.updateRequired = true;
    return;
  }
  if (tryNavigationHoldRepeat(act)) {
    return;
  }
}

void EpubAnnotationUi::saveToStorage(EpubActivity& act) {
  std::vector<std::pair<size_t, size_t>> spans = pendingSpans_;
  if (selectingStarted_ && !words_.empty()) {
    const size_t lo = std::min(anchor_, focus_);
    const size_t hi = std::max(anchor_, focus_);
    if (lo <= hi) {
      spans.push_back({lo, std::min(hi, words_.size() - 1)});
    }
  }
  normalizeSpans(spans);
  INX_SERIAL.printf("[%lu] [ANNOTATION] save start spine=%d page=%d spans=%u\n", millis(), act.currentSpineIndex,
                act.section ? act.section->currentPage : -1, static_cast<unsigned>(spans.size()));
  if (spans.empty()) {
    act.readerPopup("Nothing to save");
    return;
  }

  if (!act.section) {
    act.readerPopup("Could not save");
    exit(act);
    return;
  }

  const std::string cachePath = act.epub->getCachePath();
  const uint32_t ts = static_cast<uint32_t>(time(nullptr));
  bool anyOk = false;

  for (const auto& sp : spans) {
    const std::string seg = extractRangeText(sp.first, sp.second);
    if (seg.empty()) {
      continue;
    }
    EpubAnnotationRecord neu{};
    neu.timestamp = ts;
    neu.text = seg;
    {
      const uint16_t s = static_cast<uint16_t>(act.currentSpineIndex);
      const uint16_t p = static_cast<uint16_t>(act.section->currentPage);
      neu.startSpine = s;
      neu.startPage = p;
      neu.endSpine = s;
      neu.endPage = p;
    }
    neu.pageWordLo = static_cast<uint16_t>(sp.first);
    neu.pageWordHi = static_cast<uint16_t>(sp.second);
    neu.startPageWordLo = EpubAnnotations::kWildcard;
    neu.startPageWordHi = EpubAnnotations::kWildcard;
    neu.noteAudioPath = pendingNoteAudioPath_;
    neu.note = pendingNoteText_;

    if (annotations_.appendHighlight(cachePath, act.epub->getSpineItemsCount(), neu, act.currentSpineIndex,
                                     act.section->currentPage)) {
      anyOk = true;
    }
  }

  if (!anyOk) {
    act.readerPopup("Could not save");
    exit(act);
    return;
  }

  annotations_.ensurePageLoaded(cachePath, act.currentSpineIndex, act.section->currentPage);
  clearWordIndexCache();
  INX_SERIAL.printf("[%lu] [ANNOTATION] save complete spine=%d page=%d records=%u\n", millis(), act.currentSpineIndex,
                act.section ? act.section->currentPage : -1,
                static_cast<unsigned>(annotations_.records().size()));

  exit(act);
}
