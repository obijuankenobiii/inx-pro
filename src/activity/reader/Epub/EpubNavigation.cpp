#include "EpubNavigation.h"

#include <Arduino.h>
#include <Epub.h>
#include <Epub/Section.h>
#include <esp_task_wdt.h>

#include "EpubActivity.h"
#include "KOReaderSyncActivity.h"
#include "ProgressMapper.h"
#include "TocSidebar.h"
#include "system/FontManager.h"

EpubNavigation::EpubNavigation(EpubActivity& activity) : activity_(activity) {}

EpubNavigation::~EpubNavigation() = default;

bool EpubNavigation::isTocOpen() const { return tocSidebar_ && tocSidebar_->isVisible(); }

bool EpubNavigation::handleInput(MappedInputManager& input) {
  if (!isTocOpen()) {
    return false;
  }

  tocSidebar_->handleInput(input);
  return true;
}

void EpubNavigation::render() {
  if (isTocOpen()) {
    tocSidebar_->render();
  }
}

void EpubNavigation::reset() {
  if (tocSidebar_) {
    tocSidebar_->hide();
    tocSidebar_.reset();
  }
}

void EpubNavigation::openTableOfContents() {
  if (!activity_.epub) {
    return;
  }

  if (!tocSidebar_) {
    tocSidebar_ = std::make_unique<TocSidebar>(
        activity_.renderer, [this](const int spineIndex) { onTocChapterSelected(spineIndex); },
        [this]() { onDrawerDismissed(); }, [this]() { onKoreaderSyncRequested(); });
  }

  activity_.invalidatePreparedPage();
  activity_.pauseReadingStats();
  tocSidebar_->show(activity_.epub.get(), activity_.currentSpineIndex);
}

void EpubNavigation::onKoreaderSyncRequested() {
  if (!activity_.epub || !activity_.section) {
    return;
  }

  const std::shared_ptr<Epub> sharedEpub(activity_.epub.get(), [](Epub*) {});
  PagePosition localPosition{};
  localPosition.spineIndex = activity_.currentSpineIndex;
  localPosition.pageNumber = activity_.section->currentPage;
  localPosition.totalPages = activity_.section->pageCount;
  const KOReaderPosition localProgress = ProgressMapper::toKOReader(sharedEpub, localPosition);

  activity_.enterNewActivity(new KOReaderSyncActivity(
      activity_.renderer, activity_.mappedInput, sharedEpub, activity_.epub->getPath(),
      activity_.currentSpineIndex, activity_.section->currentPage, activity_.section->pageCount, localProgress,
      activity_.getCurrentChapterTitle(),
      [this]() {
        activity_.exitActivity();
        activity_.updateRequired = true;
        activity_.startPageTimer();
      },
      [this](const int spineIndex, const int pageNumber) {
        activity_.exitActivity();
        activity_.currentSpineIndex = spineIndex;
        activity_.nextPageNumber = pageNumber;
        activity_.section.reset();
        activity_.updateRequired = true;
        activity_.startPageTimer();
      }));
}

void EpubNavigation::generateFullData() {
  if (!activity_.epub) {
    return;
  }

  const ViewportInfo info = activity_.calculateViewport();
  const int totalSpines = activity_.epub->getSpineItemsCount();
  ScreenComponents::LoadingProgressLayout layout{};
  bool haveLayout = false;

  for (int spine = 0; spine < totalSpines; ++spine) {
    esp_task_wdt_reset();
    const int percent = totalSpines > 0 ? ((spine + 1) * 100) / totalSpines : 100;
    if (!haveLayout || (spine % 2 == 0) || spine + 1 == totalSpines) {
      layout = activity_.loadingProgressShow("Generating book data", percent);
      haveLayout = true;
    } else {
      ScreenComponents::LoadingProgress::setProgress(activity_.renderer, layout, percent);
    }
    activity_.buildSection(spine, info, false);
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (haveLayout) {
    activity_.loadingProgressShow("Book data ready", 100);
    vTaskDelay(pdMS_TO_TICKS(900));
  }
  activity_.updateRequired = true;
  activity_.startPageTimer();
}

void EpubNavigation::regenerateThumbnail() {
  if (!activity_.epub) {
    return;
  }

  activity_.readerPopup("Regenerating thumbnail...");
  vTaskDelay(pdMS_TO_TICKS(150));
  SdMan.remove(activity_.epub->getThumbBmpPath().c_str());
  SdMan.remove(activity_.epub->getThumbJpegPath().c_str());
  SdMan.remove(activity_.epub->getThumbPngPath().c_str());
  SdMan.remove(activity_.epub->getSmallThumbBmpPath().c_str());

  bool ok = false;
  EpubActivity* const activity = &activity_;
  FontManager::withSdFontsReleasedForHeapIntensiveWork(
      activity_.bookSettings.getReaderFontId(), [activity, &ok]() { ok = activity->epub->generateThumbBmp(); });
  activity_.readerPopup(ok ? "Thumbnail updated" : "Thumbnail failed");
  activity_.renderer.displayBuffer();
  vTaskDelay(pdMS_TO_TICKS(ok ? 800 : 1200));
  activity_.updateRequired = true;
  activity_.startPageTimer();
}

void EpubNavigation::onDrawerDismissed() {
  activity_.updateRequired = true;
  activity_.startPageTimer();
}

void EpubNavigation::onTocChapterSelected(const int spineIndex) {
  activity_.renderScreen(true);
  activity_.currentSpineIndex = spineIndex;
  activity_.nextPageNumber = 0;
  activity_.section.reset();
  activity_.updateRequired = true;
  activity_.startPageTimer();
}
