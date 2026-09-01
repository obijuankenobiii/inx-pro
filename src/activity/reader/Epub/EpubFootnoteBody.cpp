/**
 * @file EpubFootnoteBody.cpp
 * @brief Definitions for EpubFootnoteBody.
 */

#include "EpubFootnoteBody.h"

#include <Epub/parsers/FootnoteFragmentExtractor.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "EpubActivity.h"
#include "activity/page/components/global/Button.h"
#include "dictionary/DictionaryDefinitionLayout.h"
#include "images/Close.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr int kPanelMargin = 16;
constexpr int kPanelPad = 20;

constexpr size_t kSearchChunkBytes = 512;
constexpr size_t kSearchOverlapBytes = 96;
constexpr size_t kCaptureLeadingBytes = 2048;
constexpr size_t kCaptureBytes = 4096;

}

void EpubFootnoteBody::show(EpubActivity& act, const std::string& footnoteTarget, const std::string& markerText) {
  if (!act.section || !act.epub) {
    return;
  }
  act.btnBindings_.reset();
  mode_ = true;
  markerText_ = markerText;
  releaseBodyMemory();

  if (!capture_.captureFramebuffer(act)) {
    act.readerPopup("Could not capture page");
    mode_ = false;
    return;
  }

  std::string body;
  if (footnoteTarget.size() < 3 || footnoteTarget[1] != ':') {
    body = "Invalid footnote reference.";
  } else {
    const std::string encoded = footnoteTarget.substr(2);
    const size_t hashPos = encoded.find('#');
    if (hashPos == std::string::npos) {
      body = "Invalid footnote reference.";
    } else {
      std::string internalPath = encoded.substr(0, hashPos);
      const std::string fragmentId = encoded.substr(hashPos + 1);
      if (internalPath.empty()) {
        internalPath = act.epub->getSpineItem(act.currentSpineIndex).href;
      }
      const std::string inner = loadFootnoteBodyText(act, internalPath, fragmentId);
      body = inner.empty() ? "Footnote content not found." : inner;
    }
  }

  bodyBlocks_ = parseHtmlToBlocks(body);
  const int textWidth = (act.renderer.getScreenWidth() - kPanelMargin * 2) - kPanelPad * 2;
  bodyLines_ = layoutDefinitionBlocks(act.renderer, bodyBlocks_, textWidth);
  act.updateRequired = true;
}

void EpubFootnoteBody::exit(EpubActivity& act) {
  mode_ = false;
  releaseBodyMemory();
  markerText_.clear();
  act.updateRequired = true;
}

/** See header - swaps with a default-constructed temporary rather than .clear(), so the heap capacity
 *  a big footnote body needed is actually returned instead of sitting reserved for reuse. */
void EpubFootnoteBody::releaseBodyMemory() {
  std::vector<DefinitionBlock>().swap(bodyBlocks_);
  std::vector<DefinitionStyledLine>().swap(bodyLines_);
  bodyPageHistory_.clear();
  bodyScrollLine_ = 0;
  bodyMaxScrollLine_ = 0;
  bodyNextLine_ = 0;
  bodyScrollable_ = false;
  nextX_ = -1;
  nextY_ = -1;
  nextW_ = 0;
  nextH_ = 0;
  previousX_ = -1;
  previousY_ = -1;
  previousW_ = 0;
  previousH_ = 0;
}

std::string EpubFootnoteBody::loadFootnoteBodyText(EpubActivity& act, const std::string& internalPath,
                                                    const std::string& fragmentId) {
  if (internalPath.empty() || fragmentId.empty() || !act.epub) {
    return "";
  }

  const std::string tempPath = act.epub->getCachePath() + "/.footnote_extract.tmp";
  {
    FsFile tempFile;
    if (!SdMan.openFileForWrite("FTN", tempPath, tempFile)) {
      return "";
    }
    const bool extracted = act.epub->readItemContentsToStream(internalPath, tempFile, 1024);
    tempFile.flush();
    tempFile.sync();
    tempFile.close();
    if (!extracted) {
      SdMan.remove(tempPath.c_str());
      return "";
    }
  }

  FsFile f;
  if (!SdMan.openFileForRead("FTN", tempPath, f)) {
    SdMan.remove(tempPath.c_str());
    return "";
  }

  const std::string doubleQuoted = "id=\"" + fragmentId + "\"";
  const std::string singleQuoted = "id='" + fragmentId + "'";

  constexpr size_t kWindowBufSize = kSearchChunkBytes + kSearchOverlapBytes + 1;
  std::unique_ptr<char[]> window(new (std::nothrow) char[kWindowBufSize]);
  if (!window) {
    f.close();
    SdMan.remove(tempPath.c_str());
    return "";
  }
  size_t windowLen = 0;
  long windowFileStart = 0;
  long foundOffset = -1;
  while (true) {
    const int r = f.read(reinterpret_cast<uint8_t*>(window.get() + windowLen), kSearchChunkBytes);
    if (r <= 0) {
      break;
    }
    windowLen += static_cast<size_t>(r);
    const std::string windowStr(window.get(), windowLen);
    size_t hit = windowStr.find(doubleQuoted);
    if (hit == std::string::npos) {
      hit = windowStr.find(singleQuoted);
    }
    if (hit != std::string::npos) {
      const size_t lastOpen = windowStr.rfind('<', hit);
      foundOffset = windowFileStart + static_cast<long>(lastOpen != std::string::npos ? lastOpen : hit);
      break;
    }
    const size_t keep = std::min(windowLen, kSearchOverlapBytes);
    if (keep > 0 && keep < windowLen) {
      memmove(window.get(), window.get() + (windowLen - keep), keep);
    }
    windowFileStart += static_cast<long>(windowLen - keep);
    windowLen = keep;
    if (static_cast<size_t>(r) < kSearchChunkBytes) {
      break;
    }
  }
  window.reset();

  std::string result;
  if (foundOffset >= 0) {
    const long captureStart = std::max(0L, foundOffset - static_cast<long>(kCaptureLeadingBytes));
    constexpr size_t kCaptureBufSize = kCaptureLeadingBytes + kCaptureBytes + 1;
    std::unique_ptr<char[]> captureBuf(new (std::nothrow) char[kCaptureBufSize]);
    if (captureBuf && f.seek(static_cast<uint32_t>(captureStart))) {
      const int got = f.read(reinterpret_cast<uint8_t*>(captureBuf.get()), kCaptureLeadingBytes + kCaptureBytes);
      if (got > 0) {
        result = extractElementInnerHtmlById(std::string(captureBuf.get(), static_cast<size_t>(got)), fragmentId);
      }
    }
  }

  f.close();
  SdMan.remove(tempPath.c_str());
  return result;
}

bool EpubFootnoteBody::nextBodyPage() {
  if (!bodyScrollable_ || bodyScrollLine_ >= bodyMaxScrollLine_) {
    return false;
  }
  bodyPageHistory_.push_back(bodyScrollLine_);
  bodyScrollLine_ = std::min(bodyMaxScrollLine_, std::max(bodyScrollLine_ + 1, bodyNextLine_));
  return true;
}

bool EpubFootnoteBody::previousBodyPage() {
  if (bodyPageHistory_.empty()) {
    return false;
  }
  bodyScrollLine_ = bodyPageHistory_.back();
  bodyPageHistory_.pop_back();
  return true;
}

void EpubFootnoteBody::repaint(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  if (!capture_.restoreFramebuffer(act)) {
    act.renderScreen(true);
    return;
  }
  drawUiOverlay(act);
}

void EpubFootnoteBody::drawBodyPanel(EpubActivity& act) {
  const int screenW = act.renderer.getScreenWidth();
  const int screenH = act.renderer.getScreenHeight();
  constexpr int margin = kPanelMargin;
  constexpr int pad = kPanelPad;
  const int panelX = margin;
  const int panelW = screenW - margin * 2;
  const int panelBottom = screenH - margin;
  const int defaultPanelTop = screenH * 2 / 5;
  const int minPanelTop = margin;

  const int titleFontId = MONTSERRAT_12_FONT_ID;
  const int titleH = act.renderer.text.getLineHeight(titleFontId);
  constexpr int closeSize = 40;
  const int headerH = std::max(titleH, closeSize);
  const auto& styledLines = bodyLines_;

  int contentH = 0;
  for (const DefinitionStyledLine& sl : styledLines) {
    contentH += act.renderer.text.getLineHeight(sl.fontId) + sl.extraGapBeforePx;
  }

  constexpr int kTitleGapPx = 8;
  const int neededPanelH = pad * 2 + headerH + kTitleGapPx * 2 + contentH;
  const int defaultPanelH = panelBottom - defaultPanelTop;
  const int maxPanelH = panelBottom - minPanelTop;
  const int panelH = std::min(maxPanelH, std::max(defaultPanelH, neededPanelH));
  const int panelTop = panelBottom - panelH;
  panelX_ = panelX;
  panelY_ = panelTop;
  panelW_ = panelW;
  panelH_ = panelH;

  act.renderer.rectangle.fill(panelX, panelTop, panelW, panelH, false);
  act.renderer.rectangle.render(panelX, panelTop, panelW, panelH, true);

  int y = panelTop + pad + headerH;
  const int titleY = panelTop + pad + (headerH - titleH) / 2;
  closeSize_ = closeSize;
  closeX_ = panelX + panelW - pad - closeSize;
  closeY_ = panelTop + pad + (headerH - closeSize) / 2 - 8;
  const std::string title = act.renderer.text.truncate(titleFontId, markerText_.c_str(),
                                                        closeX_ - (panelX + pad) - 8, EpdFontFamily::BOLD);
  act.renderer.text.render(titleFontId, panelX + pad, titleY, title.c_str(), true, EpdFontFamily::BOLD);
  act.renderer.bitmap.icon(Close, closeX_, closeY_, closeSize, closeSize);
  y += kTitleGapPx;
  act.renderer.line.render(panelX + pad, y, panelX + panelW - pad, y, true, LineRender::Style::Dotted);
  y += kTitleGapPx;

  const auto maxScrollLineFor = [&](const int availableH) {
    int hFromEnd = 0;
    int idx = static_cast<int>(styledLines.size()) - 1;
    while (idx >= 0) {
      const int lh = act.renderer.text.getLineHeight(styledLines[idx].fontId) + styledLines[idx].extraGapBeforePx;
      if (hFromEnd + lh > availableH) {
        break;
      }
      hFromEnd += lh;
      --idx;
    }
    return static_cast<size_t>(idx + 1);
  };

  int contentBottom = panelTop + panelH - pad;
  size_t maxScrollLine = maxScrollLineFor(contentBottom - y);
  nextX_ = -1;
  nextY_ = -1;
  nextW_ = 0;
  nextH_ = 0;
  previousX_ = -1;
  previousY_ = -1;
  previousW_ = 0;
  previousH_ = 0;

  if (maxScrollLine > 0) {
    const int buttonFont = systemFontId();
    const int previousWidth = Button::width(act.renderer, "Previous", buttonFont);
    const int buttonWidth = Button::width(act.renderer, "Next", buttonFont);
    nextW_ = buttonWidth;
    nextH_ = Button::height;
    nextX_ = panelX + panelW - pad - nextW_;
    nextY_ = contentBottom - nextH_;
    previousW_ = previousWidth;
    previousH_ = Button::height;
    previousX_ = panelX + pad;
    previousY_ = nextY_;
    contentBottom = nextY_ - 12;
    maxScrollLine = maxScrollLineFor(contentBottom - y);
  }

  bodyScrollable_ = maxScrollLine > 0;
  bodyMaxScrollLine_ = maxScrollLine;
  bodyScrollLine_ = std::min(bodyScrollLine_, bodyMaxScrollLine_);

  int usedH = 0;
  size_t nextLine = bodyScrollLine_;
  while (nextLine < styledLines.size()) {
    const DefinitionStyledLine& line = styledLines[nextLine];
    const int lineH = act.renderer.text.getLineHeight(line.fontId) + line.extraGapBeforePx;
    if (usedH + lineH > contentBottom - y) break;
    usedH += lineH;
    ++nextLine;
  }
  bodyNextLine_ = nextLine;

  renderStyledLines(act.renderer, styledLines, panelX + pad, y, contentBottom, bodyScrollLine_);
  if (bodyScrollLine_ > 0) {
    Button::render(act.renderer, {previousX_, previousY_, previousW_, previousH_}, "Previous", false, systemFontId());
  } else {
    previousX_ = -1;
    previousY_ = -1;
    previousW_ = 0;
    previousH_ = 0;
  }
  if (bodyScrollLine_ < bodyMaxScrollLine_) {
    Button::render(act.renderer, {nextX_, nextY_, nextW_, nextH_}, "Next", true, systemFontId());
  } else {
    nextX_ = -1;
    nextY_ = -1;
    nextW_ = 0;
    nextH_ = 0;
  }
}

void EpubFootnoteBody::drawUiOverlay(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  act.renderer.clearScreen(0xFF);
  drawBodyPanel(act);
  act.renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void EpubFootnoteBody::handleInput(EpubActivity& act) {
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
      const int x = static_cast<int>(tapNx * act.renderer.getScreenWidth());
      const int y = static_cast<int>(tapNy * act.renderer.getScreenHeight());
      if (x >= nextX_ && x < nextX_ + nextW_ && y >= nextY_ && y < nextY_ + nextH_) {
        if (nextBodyPage()) {
          act.updateRequired = true;
        }
        return;
      }
      if (x >= previousX_ && x < previousX_ + previousW_ && y >= previousY_ &&
          y < previousY_ + previousH_) {
        if (previousBodyPage()) {
          act.updateRequired = true;
        }
        return;
      }
      constexpr int hitPadding = 12;
      if (x >= closeX_ - hitPadding && x < closeX_ + closeSize_ + hitPadding && y >= closeY_ - hitPadding &&
          y < closeY_ + closeSize_ + hitPadding) {
        exit(act);
        act.startPageTimer();
        return;
      }
      if (x < panelX_ || x >= panelX_ + panelW_ || y < panelY_ || y >= panelY_ + panelH_) {
        exit(act);
        act.startPageTimer();
        return;
      }
    }
    return;
  }

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    exit(act);
    act.startPageTimer();
    return;
  }

  constexpr size_t kScrollLinesPerPress = 3;
  if (m.wasPressed(MappedInputManager::Button::Up)) {
    bodyScrollLine_ = (bodyScrollLine_ > kScrollLinesPerPress) ? bodyScrollLine_ - kScrollLinesPerPress : 0;
    act.updateRequired = true;
  } else if (m.wasPressed(MappedInputManager::Button::Down)) {
    bodyScrollLine_ += kScrollLinesPerPress;
    act.updateRequired = true;
  } else if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    exit(act);
    act.startPageTimer();
  }
}
