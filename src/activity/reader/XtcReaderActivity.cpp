/**
 * @file XtcReaderActivity.cpp
 * @brief Definitions for XtcReaderActivity.
 */

/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "state/RecentBooks.h"
#include "state/BookState.h"
#include "state/Session.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "images/Close.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long STATS_SAVE_INTERVAL_MS = 30000;
constexpr int XTC_TOC_PADDING = 20;
constexpr int XTC_TOC_CLOSE_SIZE = 24;
constexpr int XTC_TOC_CLOSE_HIT_SIZE = 40;
constexpr int XTC_TOC_HEADER_HEIGHT = 74;
constexpr int XTC_TOC_ROW_HEIGHT = 66;

void drawScrollBar(const GfxRenderer& renderer, const int x, const int y, const int height, const int total,
                   const int visible, const int offset) {
  if (total <= visible || height <= 0) return;

  constexpr int width = 3;
  const int maxOffset = std::max(1, total - visible);
  const int thumbHeight = std::max(14, height * visible / total);
  const int thumbTravel = std::max(1, height - thumbHeight);
  const int thumbY = y + offset * thumbTravel / maxOffset;
  renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Gray), true);
  renderer.rectangle.fill(x, thumbY, width, thumbHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
}

int xtcTocWidth(const GfxRenderer& renderer) {
  const int screenWidth = renderer.getScreenWidth();
  return std::min(screenWidth - 40, std::max(240, screenWidth * 2 / 3));
}

int xtcTocVisibleRows(const GfxRenderer& renderer) {
  return std::max(1, (renderer.getScreenHeight() - XTC_TOC_HEADER_HEIGHT) / XTC_TOC_ROW_HEIGHT);
}

uint8_t xtcQualityGray2Code(const uint8_t level) {
  const uint8_t l = level & 3u;
  if (l == 1u) return 2u;
  if (l == 2u) return 1u;
  return l;
}

int chapterIndexForPage(const Xtc& book, uint32_t page) {
  if (!book.hasChapters()) {
    return 0;
  }
  const auto& ch = book.getChapters();
  int best = -1;
  for (size_t i = 0; i < ch.size(); i++) {
    if (page >= static_cast<uint32_t>(ch[i].startPage)) {
      best = static_cast<int>(i);
    }
  }
  return best < 0 ? 0 : best;
}

std::string formatReadingTime(const uint32_t timeMs) {
  const uint32_t seconds = timeMs / 1000;
  const uint32_t minutes = seconds / 60;
  const uint32_t hours = minutes / 60;

  char buffer[32];
  if (hours > 0) {
    snprintf(buffer, sizeof(buffer), "%uh %um", hours, minutes % 60);
  } else if (minutes > 0) {
    snprintf(buffer, sizeof(buffer), "%um", minutes);
  } else {
    snprintf(buffer, sizeof(buffer), "%us", seconds);
  }
  return std::string(buffer);
}

bool isLandscapeReader(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

bool xtcDrawerPrev(const MappedInputManager& input, const GfxRenderer& renderer) {
  return isLandscapeReader(renderer) ? input.wasPressed(MappedInputManager::Button::Right)
                                     : input.wasPressed(MappedInputManager::Button::Up);
}

bool xtcDrawerNext(const MappedInputManager& input, const GfxRenderer& renderer) {
  return isLandscapeReader(renderer) ? input.wasPressed(MappedInputManager::Button::Left)
                                     : input.wasPressed(MappedInputManager::Button::Down);
}

}

void XtcReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<XtcReaderActivity*>(param);
  self->displayTaskLoop();
}

void XtcReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!xtc) {
    return;
  }

  renderingMutex = xSemaphoreCreateMutex();

  xtc->setupCacheDir();
  loadProgress();
  ensureThumbnailExists();
  initStats();

  APP_STATE.lastRead = xtc->getPath();
  APP_STATE.saveToFile();

  const uint32_t n = xtc->getPageCount();
  float progressFrac = 0.f;
  if (n > 0) {
    progressFrac = (static_cast<float>(std::min(currentPage, n - 1)) + 1.f) / static_cast<float>(n);
  }
  RECENT_BOOKS.addBook(xtc->getPath(), xtc->getCachePath(), xtc->getTitle(), xtc->getAuthor(), progressFrac);
  BOOK_STATE.addOrUpdateBook(xtc->getPath(), xtc->getTitle(), xtc->getAuthor());
  BOOK_STATE.setReading(xtc->getPath(), true, xtc->getTitle());

  updateRequired = true;
  pagesUntilFullRefresh = READER_SETTINGS.getRefreshFrequency();

  xTaskCreate(&XtcReaderActivity::taskTrampoline, "XtcReaderActivityTask", 4096, this, 1, &displayTaskHandle);
}

void XtcReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vTaskDelay(pdMS_TO_TICKS(10));

  if (READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

#if FREEINK_DEVICE_X4PRO
  renderer.clearScreen(0xFF);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
#endif

  if (pageStartTime > 0) {
    endPageTimer();
  }
  if (xtc) {
    saveBookStatsToFile();
    saveProgress();
    const uint32_t n = xtc->getPageCount();
    uint32_t progPage = currentPage;
    if (n > 0 && progPage >= n) {
      progPage = n - 1;
    }
    const float progressFrac = (n > 0) ? (static_cast<float>(progPage) + 1.f) / static_cast<float>(n) : 0.f;
    RECENT_BOOKS.addBook(xtc->getPath(), xtc->getCachePath(), xtc->getTitle(), xtc->getAuthor(), progressFrac);
    APP_STATE.lastRead = xtc->getPath();
    APP_STATE.saveToFile();
  }

  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  xtc.reset();
}

void XtcReaderActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (suppressBackUntilReleased) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) {
      suppressBackUntilReleased = false;
    }
    return;
  }

  if (menuDrawerVisible) {
    handleMenuDrawerInput();
    return;
  }

  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    onGoBack();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoToHome();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  if (READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_TAP && xtc && xtc->hasChapters() &&
      !xtc->getChapters().empty() && mappedInput.hasTouch() &&
      mappedInput.wasTouchSwipeRightForRenderer(renderer)) {
      openTableOfContents();
      return;
  }

  bool tapPrevTriggered = false;
  bool tapNextTriggered = false;
  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_TAP &&
        mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      (tapNx < 0.5f ? tapPrevTriggered : tapNextTriggered) = true;
    }
  }

  const bool swipePrevTriggered = READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_SWIPE &&
                                  mappedInput.hasTouch() && mappedInput.wasTouchSwipeRightForRenderer(renderer);
  const bool swipeNextTriggered = READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_SWIPE &&
                                  mappedInput.hasTouch() && mappedInput.wasTouchSwipeLeftForRenderer(renderer);

  const bool usePressForPageTurn = READER_SETTINGS.longPressChapterSkip == SystemSetting::LONG_PRESS_OFF;
  const MappedInputManager::MotionGesture motionGesture = mappedInput.readMotionGesture(
      static_cast<uint8_t>(renderer.getOrientation()), SETTINGS.shakePageTurn, SETTINGS.shakePageTurnSensitivity);
  const bool powerReleased = mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool powerPagePrevious = powerReleased &&
                                 READER_SETTINGS.btnPowerShortAction == SystemSetting::BTN_ACTION_PAGE_PREVIOUS;
  const bool powerPageTurn = powerReleased && READER_SETTINGS.btnPowerShortAction == SystemSetting::BTN_ACTION_PAGE_NEXT;
  if (powerReleased && READER_SETTINGS.btnPowerShortAction == SystemSetting::BTN_ACTION_PAGE_REFRESH) {
    renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
    return;
  }

  if (powerReleased && READER_SETTINGS.btnPowerShortAction == SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS) {
    openTableOfContents();
    return;
  }

  const bool prevTriggered = tapPrevTriggered || swipePrevTriggered ||
                             motionGesture == MappedInputManager::MotionGesture::Previous ||
                             powerPagePrevious ||
                             (usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                     mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                  : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                     mappedInput.wasReleased(MappedInputManager::Button::Left)));
  const bool nextTriggered =
      tapNextTriggered || swipeNextTriggered || motionGesture == MappedInputManager::MotionGesture::Next ||
      (usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                              mappedInput.wasPressed(MappedInputManager::Button::Right))
                           : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right)));

  if (!prevTriggered && !nextTriggered) {
    if (READER_SETTINGS.pageAutoTurnSeconds > 0 && xtc && xtc->getPageCount() > 0 && currentPage < xtc->getPageCount() &&
        pageStartTime > 0 &&
        millis() - pageStartTime >= static_cast<uint32_t>(READER_SETTINGS.pageAutoTurnSeconds) * 1000UL) {
      turnPage(true);
    }
    return;
  }

  if (xtc->getPageCount() == 0) {
    return;
  }

  const uint32_t pageCount = xtc->getPageCount();

  if (currentPage >= pageCount) {
    turnPage(false);
    return;
  }

  const bool skipPages =
      READER_SETTINGS.longPressChapterSkip != SystemSetting::LONG_PRESS_OFF && mappedInput.getHeldTime() > skipPageMs;
  const int skipAmount =
      !skipPages ? 1 : (READER_SETTINGS.longPressChapterSkip == SystemSetting::LONG_PRESS_PAGE_SKIP_5 ? 5 : 10);

  if (prevTriggered) {
    turnPage(false, skipAmount);
  } else if (nextTriggered) {
    turnPage(true, skipAmount);
  }
}

int XtcReaderActivity::chapterIndexForCurrentPage() const {
  if (!xtc || xtc->getPageCount() == 0) {
    return 0;
  }
  const uint32_t page = std::min<uint32_t>(currentPage, xtc->getPageCount() - 1);
  return chapterIndexForPage(*xtc, page);
}

void XtcReaderActivity::turnPage(const bool forward, const int skipAmount) {
  if (!xtc || xtc->getPageCount() == 0) {
    return;
  }

  const uint32_t pageCount = xtc->getPageCount();
  if (currentPage >= pageCount) {
    endPageTimer();
    currentPage = pageCount - 1;
    startPageTimer();
    updateRequired = true;
    return;
  }

  endPageTimer();
  if (forward) {
    const uint32_t oldPage = currentPage;
    const int oldChapter = chapterIndexForPage(*xtc, oldPage);
    currentPage += static_cast<uint32_t>(std::max(1, skipAmount));
    if (currentPage >= pageCount) {
      currentPage = pageCount;
    }
    const uint32_t refPage = std::min(currentPage, pageCount - 1);
    const int newChapter = chapterIndexForPage(*xtc, refPage);
    if (newChapter > oldChapter) {
      bookStats.totalChaptersRead += static_cast<uint32_t>(newChapter - oldChapter);
    }
  } else if (currentPage >= static_cast<uint32_t>(std::max(1, skipAmount))) {
    currentPage -= static_cast<uint32_t>(std::max(1, skipAmount));
  } else {
    currentPage = 0;
  }
  startPageTimer();
  updateRequired = true;
}

void XtcReaderActivity::openTableOfContents() {
  if (!xtc || !xtc->hasChapters() || xtc->getChapters().empty()) {
    return;
  }

  menuDrawerVisible = true;
  chapterSelectedIndex = chapterIndexForCurrentPage();
  chapterScrollOffset = std::max(0, chapterSelectedIndex - 2);
  renderMenuChapters();
}

void XtcReaderActivity::closeMenuDrawer(const bool repaintPage) {
  menuDrawerVisible = false;
  if (repaintPage) {
    updateRequired = true;
  }
}

void XtcReaderActivity::handleMenuDrawerInput() {
  if (mappedInput.hasTouch() && xtc && xtc->hasChapters()) {
    const int visibleRows = xtcTocVisibleRows(renderer);
    const int chapterCount = static_cast<int>(xtc->getChapters().size());
    const bool swipeUp = mappedInput.wasTouchSwipeUpForRenderer(renderer) || mappedInput.wasTouchSwipeUp();
    const bool swipeDown = mappedInput.wasTouchSwipeDownForRenderer(renderer) || mappedInput.wasTouchSwipeDown();

    if (swipeUp) {
      chapterScrollOffset = std::min(std::max(0, chapterCount - visibleRows), chapterScrollOffset + visibleRows);
      renderMenuChapters();
      return;
    }
    if (swipeDown) {
      chapterScrollOffset = std::max(0, chapterScrollOffset - visibleRows);
      renderMenuChapters();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    suppressBackUntilReleased = true;
    closeMenuDrawer(true);
    return;
  }

  if (!xtc || !xtc->hasChapters() || xtc->getChapters().empty()) {
    closeMenuDrawer(true);
    return;
  }

  const int chapterCount = static_cast<int>(xtc->getChapters().size());
  if (xtcDrawerPrev(mappedInput, renderer)) {
    chapterSelectedIndex = (chapterSelectedIndex - 1 + chapterCount) % chapterCount;
    if (chapterSelectedIndex < chapterScrollOffset) chapterScrollOffset = chapterSelectedIndex;
    renderMenuChapters();
    return;
  }
  if (xtcDrawerNext(mappedInput, renderer)) {
    chapterSelectedIndex = (chapterSelectedIndex + 1) % chapterCount;
    if (chapterSelectedIndex >= chapterScrollOffset + xtcTocVisibleRows(renderer)) {
      chapterScrollOffset = chapterSelectedIndex - xtcTocVisibleRows(renderer) + 1;
    } else if (chapterSelectedIndex < chapterScrollOffset) {
      chapterScrollOffset = chapterSelectedIndex;
    }
    renderMenuChapters();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    endPageTimer();
    currentPage = static_cast<uint32_t>(xtc->getChapters()[chapterSelectedIndex].startPage);
    startPageTimer();
    closeMenuDrawer(true);
    return;
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      return;
    }

    const int x = static_cast<int>(tapNx * renderer.getScreenWidth());
    const int y = static_cast<int>(tapNy * renderer.getScreenHeight());
    const int drawerW = xtcTocWidth(renderer);
    constexpr int drawerX = 0;
    constexpr int drawerY = 0;
    constexpr int headerH = XTC_TOC_HEADER_HEIGHT;
    const int visibleRows = xtcTocVisibleRows(renderer);

    if (x < drawerX || x >= drawerX + drawerW || y < drawerY || y >= renderer.getScreenHeight()) {
      suppressBackUntilReleased = true;
      closeMenuDrawer(true);
      return;
    }
    const int closeX = drawerW - XTC_TOC_PADDING - XTC_TOC_CLOSE_HIT_SIZE;
    const int closeY = (headerH - XTC_TOC_CLOSE_HIT_SIZE) / 2;
    if (x >= closeX && x < closeX + XTC_TOC_CLOSE_HIT_SIZE && y >= closeY && y < closeY + XTC_TOC_CLOSE_HIT_SIZE) {
      suppressBackUntilReleased = true;
      closeMenuDrawer(true);
      return;
    }
    if (y >= drawerY + headerH && y < drawerY + headerH + visibleRows * XTC_TOC_ROW_HEIGHT) {
      const int row = (y - drawerY - headerH) / XTC_TOC_ROW_HEIGHT;
      const int tappedIndex = chapterScrollOffset + row;
      if (tappedIndex >= 0 && tappedIndex < chapterCount) {
        endPageTimer();
        currentPage = static_cast<uint32_t>(xtc->getChapters()[tappedIndex].startPage);
        chapterSelectedIndex = tappedIndex;
        startPageTimer();
        closeMenuDrawer(true);
      }
    }
  }
}

void XtcReaderActivity::renderMenuChapters() {
  const int screenH = renderer.getScreenHeight();
  const int drawerW = xtcTocWidth(renderer);
  const int visibleRows = xtcTocVisibleRows(renderer);
  const int chapterCount = xtc ? static_cast<int>(xtc->getChapters().size()) : 0;
  if (chapterCount <= 0) {
    menuDrawerVisible = false;
    return;
  }
  chapterScrollOffset = std::max(0, std::min(chapterScrollOffset, std::max(0, chapterCount - visibleRows)));

  renderer.syncWriteBufferFromActive();
  renderer.rectangle.fill(0, 0, drawerW, screenH, false);
  renderer.rectangle.render(drawerW - 1, 0, 1, screenH, true);

  const int titleY = (XTC_TOC_HEADER_HEIGHT - renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID)) / 2 + 5;
  renderer.text.render(MONTSERRAT_12_FONT_ID, XTC_TOC_PADDING, titleY, "Table of Contents", true,
                       EpdFontFamily::REGULAR);
  const int closeX = drawerW - XTC_TOC_PADDING - XTC_TOC_CLOSE_HIT_SIZE;
  const int closeY = (XTC_TOC_HEADER_HEIGHT - XTC_TOC_CLOSE_HIT_SIZE) / 2;
  renderer.bitmap.iconScaled(Close, closeX + (XTC_TOC_CLOSE_HIT_SIZE - XTC_TOC_CLOSE_SIZE) / 2,
                             closeY + (XTC_TOC_CLOSE_HIT_SIZE - XTC_TOC_CLOSE_SIZE) / 2 + 5,
                             XTC_TOC_CLOSE_HIT_SIZE, XTC_TOC_CLOSE_HIT_SIZE, XTC_TOC_CLOSE_SIZE,
                             XTC_TOC_CLOSE_SIZE, BitmapRender::Orientation::None);
  renderer.line.render(0, XTC_TOC_HEADER_HEIGHT - 1, drawerW, XTC_TOC_HEADER_HEIGHT - 1, true);

  const auto& chapters = xtc->getChapters();
  for (int i = 0; i < visibleRows && chapterScrollOffset + i < chapterCount; ++i) {
    const int idx = chapterScrollOffset + i;
    const int rowY = XTC_TOC_HEADER_HEIGHT + i * XTC_TOC_ROW_HEIGHT;
    const bool selected = idx == chapterSelectedIndex;
    if (selected) {
      renderer.rectangle.fill(0, rowY, drawerW, XTC_TOC_ROW_HEIGHT, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int textY = rowY + (XTC_TOC_ROW_HEIGHT - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
    const std::string& title = chapters[idx].name;
    const int tone = selected ? 0 : 1;
    const int textWidth = std::max(40, drawerW - XTC_TOC_PADDING * 2);
    const std::string truncatedTitle = renderer.text.truncate(
        MONTSERRAT_10_FONT_ID, title.empty() ? "Chapter" : title.c_str(), textWidth);
    renderer.text.render(MONTSERRAT_10_FONT_ID, XTC_TOC_PADDING, textY, truncatedTitle.c_str(), tone);
    if (i + 1 < visibleRows && chapterScrollOffset + i + 1 < chapterCount) {
      renderer.line.render(0, rowY + XTC_TOC_ROW_HEIGHT - 1, drawerW, rowY + XTC_TOC_ROW_HEIGHT - 1, true,
                           LineRender::Style::Dotted);
    }
  }

  drawScrollBar(renderer, drawerW - 8, XTC_TOC_HEADER_HEIGHT, visibleRows * XTC_TOC_ROW_HEIGHT, chapterCount,
                visibleRows, chapterScrollOffset);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void XtcReaderActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void XtcReaderActivity::renderScreen() {
  if (!xtc) {
    return;
  }

  if (currentPage >= xtc->getPageCount()) {
    const uint32_t pageCount = xtc->getPageCount();
    if (pageCount > 0) {
      bookStats.progressPercent = 100.f;
      bookStats.lastPageNumber = static_cast<uint16_t>(std::min<uint32_t>(pageCount - 1, UINT16_MAX));
      bookStats.lastSpineIndex = static_cast<uint16_t>(chapterIndexForPage(*xtc, pageCount - 1));
    }
    saveBookStatsToFile();
    renderEndOfBookStats();
    return;
  }

  renderPage();
  saveProgress();
}

void XtcReaderActivity::renderEndOfBookStats() {
  renderer.clearScreen(0xff);

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  constexpr int valueFont = MONTSERRAT_18_FONT_ID;
  constexpr int labelFont = MONTSERRAT_10_FONT_ID;

  const int statsX = (screenW - 250) / 2;
  const int statsY = (screenH - 300) / 2;
  int currentY = statsY;
  char buffer[32];

  renderer.text.render(MONTSERRAT_18_FONT_ID, statsX, statsY - 90, "End of book", true, EpdFontFamily::BOLD);

  const std::string timeStr = formatReadingTime(bookStats.totalReadingTimeMs);
  renderer.text.render(valueFont, statsX, currentY, timeStr.c_str(), true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Reading Time", true);
  currentY += 87;

  snprintf(buffer, sizeof(buffer), "%u", bookStats.totalPagesRead);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Pages", true);
  currentY += 87;

  snprintf(buffer, sizeof(buffer), "%u", bookStats.totalChaptersRead);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Chapters", true);
  currentY += 87;

  if (bookStats.avgPageTimeMs > 0) {
    snprintf(buffer, sizeof(buffer), "%us", bookStats.avgPageTimeMs / 1000);
  } else {
    snprintf(buffer, sizeof(buffer), "-");
  }
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Average / Page", true);

  currentY += 87;
  snprintf(buffer, sizeof(buffer), "%u", bookStats.sessionCount);
  renderer.text.render(valueFont, statsX, currentY, buffer, true, EpdFontFamily::BOLD);
  renderer.text.render(labelFont, statsX, currentY + 45, "Reading Sessions", true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    INX_SERIAL.printf("[%lu] [XTR] Failed to allocate page buffer (%lu bytes)\n", millis(), pageBufferSize);
    renderer.clearScreen();
    renderer.text.centered(MONTSERRAT_12_FONT_ID, 300, "Memory error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    INX_SERIAL.printf("[%lu] [XTR] Failed to load page %lu\n", millis(), currentPage);
    free(pageBuffer);
    renderer.clearScreen();
    renderer.text.centered(MONTSERRAT_12_FONT_ID, 300, "Page load error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();

  const int refreshFrequency = READER_SETTINGS.getRefreshFrequency();
  auto displayPageAndTrackRefresh = [&] {
    if (refreshFrequency > 0 && pagesUntilFullRefresh <= 1) {
      renderer.displayBufferAsync(HalDisplay::HALF_REFRESH);
      pagesUntilFullRefresh = refreshFrequency;
    } else {
      renderer.displayBufferAsync();
      if (refreshFrequency > 0) {
        pagesUntilFullRefresh--;
      }
    }
  };

  const uint16_t maxSrcY = pageHeight;

  const uint8_t imageQuality = READER_SETTINGS.readerImageGrayscale < SystemSetting::READER_IMAGE_QUALITY_COUNT
                                   ? READER_SETTINGS.readerImageGrayscale
                                   : SystemSetting::READER_IMAGE_LOW;

  if (bitDepth == 2) {
    const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
    const uint8_t* plane1 = pageBuffer;
    const uint8_t* plane2 = pageBuffer + planeSize;
    const size_t colBytes = (pageHeight + 7) / 8;

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    auto renderBwPreview = [&] {
      renderer.clearScreen();
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          if (getPixelValue(x, y) >= 1) {
            renderer.drawPixel(x, y, true);
          }
        }
      }
    };

    if (imageQuality == SystemSetting::READER_IMAGE_LOW) {
      renderBwPreview();
      displayPageAndTrackRefresh();

      free(pageBuffer);
      INX_SERIAL.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit low/BW)\n", millis(), currentPage + 1,
                    xtc->getPageCount());
      return;
    }

    if (imageQuality == SystemSetting::READER_IMAGE_HIGH) {
      renderer.renderGrayscalePasses(/*quality=*/true, /*preserveText=*/false,
                                     [&] {
                                       renderer.clearScreen(0xFF);
                                       const GfxRenderer::RenderMode renderMode = renderer.getRenderMode();
                                       for (uint16_t y = 0; y < pageHeight; y++) {
                                         for (uint16_t x = 0; x < pageWidth; x++) {
                                           const uint8_t code = xtcQualityGray2Code(getPixelValue(x, y));
                                           if ((renderMode == GfxRenderer::GRAY2_LSB && ((code & 0b01u) == 0)) ||
                                               (renderMode == GfxRenderer::GRAY2_MSB && ((code & 0b10u) == 0))) {
                                             renderer.drawPixel(x, y, true);
                                           }
                                         }
                                       }
                                     },
                                     /*fastQuality=*/true);

      if (refreshFrequency > 0) {
        if (pagesUntilFullRefresh <= 1) {
          pagesUntilFullRefresh = refreshFrequency;
        } else {
          pagesUntilFullRefresh--;
        }
      }
      free(pageBuffer);
      INX_SERIAL.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit high quality)\n", millis(), currentPage + 1,
                    xtc->getPageCount());
      return;
    }

    renderBwPreview();
    displayPageAndTrackRefresh();

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) == 1) {
          renderer.drawPixel(x, y, false);
        }
      }
    }
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        const uint8_t pv = getPixelValue(x, y);
        if (pv == 1 || pv == 2) {
          renderer.drawPixel(x, y, false);
        }
      }
    }
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();

    renderer.clearScreen();
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
    }

    renderer.cleanupGrayscaleWithFrameBuffer();

    free(pageBuffer);

    INX_SERIAL.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit grayscale)\n", millis(), currentPage + 1,
                  xtc->getPageCount());
    return;
  } else {
    const size_t srcRowBytes = (pageWidth + 7) / 8;
    auto isBlackAt = [&](const uint16_t srcX, const uint16_t srcY) {
      const size_t srcByte = srcY * srcRowBytes + srcX / 8;
      const size_t srcBit = 7 - (srcX % 8);
      return !((pageBuffer[srcByte] >> srcBit) & 1);
    };

    if (imageQuality == SystemSetting::READER_IMAGE_HIGH) {
      renderer.renderGrayscalePasses(/*quality=*/true, /*preserveText=*/false,
                                     [&] {
                                       renderer.clearScreen(0xFF);
                                       for (uint16_t y = 0; y < pageHeight; y++) {
                                         for (uint16_t x = 0; x < pageWidth; x++) {
                                           if (isBlackAt(x, y)) {
                                             renderer.drawPixel(x, y, true);
                                           }
                                         }
                                       }
                                     },
                                     /*fastQuality=*/true);
      free(pageBuffer);
      INX_SERIAL.printf("[%lu] [XTR] Rendered page %lu/%lu (%u-bit, high quality path)\n", millis(), currentPage + 1,
                        xtc->getPageCount(), bitDepth);
      return;
    }

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        if (isBlackAt(srcX, srcY)) {
          renderer.drawPixel(srcX, srcY, true);
        }
      }
    }
  }

  free(pageBuffer);

  displayPageAndTrackRefresh();

  INX_SERIAL.printf("[%lu] [XTR] Rendered page %lu/%lu (%u-bit)\n", millis(), currentPage + 1, xtc->getPageCount(),
                bitDepth);
}

void XtcReaderActivity::saveProgress() const {
  FsFile f;
  if (SdMan.openFileForWrite("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = (currentPage >> 16) & 0xFF;
    data[3] = (currentPage >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void XtcReaderActivity::loadProgress() {
  FsFile f;
  if (SdMan.openFileForRead("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      INX_SERIAL.printf("[%lu] [XTR] Loaded progress: page %lu\n", millis(), currentPage);

      if (currentPage >= xtc->getPageCount()) {
        currentPage = 0;
      }
    }
    f.close();
  }
}

void XtcReaderActivity::ensureThumbnailExists() {
  if (!xtc) {
    return;
  }
  const std::string thumbPath = xtc->getThumbBmpPath();
  if (!SdMan.exists(thumbPath.c_str())) {
    xtc->generateThumbBmp();
  }
}

void XtcReaderActivity::initStats() {
  if (!xtc) {
    return;
  }

  if (loadBookStats(xtc->getCachePath().c_str(), bookStats)) {
    bookStats.sessionCount++;
  } else {
    bookStats.path = xtc->getCachePath();
    bookStats.title = xtc->getTitle();
    bookStats.author = xtc->getAuthor();
    bookStats.totalReadingTimeMs = 0;
    bookStats.totalPagesRead = 0;
    bookStats.totalChaptersRead = 0;
    bookStats.lastReadTimeMs = millis();
    bookStats.progressPercent = 0;
    bookStats.lastSpineIndex = static_cast<uint16_t>(chapterIndexForPage(*xtc, currentPage));
    bookStats.lastPageNumber = static_cast<uint16_t>(std::min<uint32_t>(currentPage, UINT16_MAX));
    bookStats.avgPageTimeMs = 0;
    bookStats.sessionCount = 1;
  }

  bookStats.lastReadTimeMs = millis();
  pageStartTime = millis();
  lastSaveTime = millis();
}

void XtcReaderActivity::startPageTimer() { pageStartTime = millis(); }

void XtcReaderActivity::endPageTimer() {
  if (pageStartTime == 0 || !xtc) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t timeSpent = now - pageStartTime;

  if (timeSpent < 1000) {
    pageStartTime = 0;
    return;
  }

  const uint32_t pageCount = xtc->getPageCount();
  if (pageCount > 0 && currentPage < pageCount) {
    bookStats.totalReadingTimeMs += timeSpent;
    bookStats.totalPagesRead++;
    bookStats.lastReadTimeMs = now;
    bookStats.lastPageNumber = static_cast<uint16_t>(std::min<uint32_t>(currentPage, UINT16_MAX));
    bookStats.lastSpineIndex = static_cast<uint16_t>(chapterIndexForPage(*xtc, currentPage));
    bookStats.progressPercent = (static_cast<float>(currentPage) + 1.f) / static_cast<float>(pageCount) * 100.f;

    if (bookStats.totalPagesRead > 0) {
      bookStats.avgPageTimeMs = bookStats.totalReadingTimeMs / bookStats.totalPagesRead;
    }

    if (now - lastSaveTime >= STATS_SAVE_INTERVAL_MS) {
      saveBookStatsToFile();
      lastSaveTime = now;
    }
  }

  pageStartTime = 0;
}

void XtcReaderActivity::saveBookStatsToFile() {
  if (!xtc) {
    return;
  }
  bookStats.lastReadTimeMs = millis();
  bookStats.path = xtc->getCachePath();
  bookStats.title = xtc->getTitle();
  bookStats.author = xtc->getAuthor();
  ::saveBookStats(xtc->getCachePath().c_str(), bookStats);
}
