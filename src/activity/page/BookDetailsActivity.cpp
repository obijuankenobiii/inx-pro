#include "BookDetailsActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "components/global/Button.h"
#include "dictionary/DictionaryDefinitionLayout.h"
#include "dictionary/StarDictLookup.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/Star.h"
#include "components/widget/MetadataRating.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void openDictionaryLookupFromDetails(const std::string& word, const std::string& bookPath,
                                            const std::string& cachePath, std::function<void()> returnToCaller);

namespace {

constexpr int kSideMargin = 20;
constexpr int kSubtitleGap = 5;

std::string joinTags(const std::vector<std::string>& tags) {
  std::string result;
  for (const std::string& tag : tags) {
    if (tag.empty()) continue;
    if (!result.empty()) result += ", ";
    result += tag;
  }
  return result;
}

}

BookDetailsActivity::BookDetailsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string bookPath, std::string cachePath,
                                         std::function<void()> onClose)
    : SubPage("BookDetails", renderer, mappedInput, onClose),
      bookPath_(std::move(bookPath)),
      cachePath_(std::move(cachePath)),
      closeCallback_(std::move(onClose)) {}

void BookDetailsActivity::onEnter() {
  SubPage::onEnter();
  page_ = kMetadataPage;
  descriptionPage_ = 0;
  metadataScroll_ = 0;
  rows_.clear();
  addRow("PATH", bookPath_);
  descriptionLines_.clear();
  descriptionPages_.clear();
  descriptionWordHits_.clear();
  descriptionHtml_.clear();
  descriptionBodyTop_ = kListTop;
  descriptionBottom_ = renderer.getScreenHeight() - kBottomMargin;
  selectedDescriptionWord_ = -1;
  lookupButtonX_ = -1;
  lookupButtonY_ = -1;
  lookupButtonW_ = 0;
  lookupButtonH_ = 0;
  metadataLoaded_ = false;
  descriptionLoaded_ = false;
  updateRequired = true;
  renderPage();
}

void BookDetailsActivity::addRow(const char* label, const std::string& value) {
  if (!value.empty()) rows_.push_back({label, value});
}

void BookDetailsActivity::loadMetadata() {
  rows_.clear();
  descriptionLines_.clear();
  descriptionHtml_.clear();
  descriptionLoaded_ = false;

  BookMetadataCache metadata(cachePath_);
  if (metadata.load()) {
    addRow("TITLE", metadata.coreMetadata.title);
    addRow("AUTHOR", metadata.coreMetadata.author);
    addRow("SERIES", metadata.coreMetadata.series);
    addRow("SERIES INDEX", metadata.coreMetadata.seriesIndex);
    addRow("RATING", metadata.coreMetadata.rating);
    addRow("PUBLISHER", metadata.coreMetadata.publisher);
    addRow("PUBLICATION DATE", metadata.coreMetadata.publicationDate);
    addRow("LANGUAGE", metadata.coreMetadata.language);
    addRow("TAGS", joinTags(metadata.coreMetadata.tags));
    addRow("TITLE SORT", metadata.coreMetadata.titleSort);
    addRow("AUTHOR SORT", metadata.coreMetadata.authorSort);
    addRow("CALIBRE UUID", metadata.coreMetadata.calibreUuid);
    addRow("CALIBRE TIMESTAMP", metadata.coreMetadata.calibreTimestamp);
    descriptionHtml_ = metadata.coreMetadata.description;
  }

  if (rows_.empty()) addRow("PATH", bookPath_);
}

void BookDetailsActivity::loadDescription() {
  if (descriptionLoaded_) return;
  descriptionLines_ = layoutDefinitionBlocks(renderer, parseHtmlToBlocks(descriptionHtml_),
                                              renderer.getScreenWidth() - kSideMargin * 2,
                                              systemFontIdForPointSize(13));
  descriptionBodyTop_ = kListTop;
  descriptionBottom_ = renderer.getScreenHeight() - kBottomMargin -
                       renderer.text.getLineHeight(systemFontIdForPointSize(10)) - 12;
  makeDescriptionPages();
  descriptionLoaded_ = true;
}

int BookDetailsActivity::descriptionWordAt(const int x, const int y) const {
  constexpr int hitPadding = 8;
  for (size_t i = 0; i < descriptionWordHits_.size(); ++i) {
    const DescriptionWordHit& hit = descriptionWordHits_[i];
    if (x >= hit.x - hitPadding && x < hit.x + hit.width + hitPadding && y >= hit.y - hitPadding &&
        y < hit.y + hit.height + hitPadding) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void BookDetailsActivity::makeDescriptionPages() {
  descriptionPages_.clear();
  if (descriptionLines_.empty()) {
    descriptionPages_.push_back(0);
    descriptionPage_ = 0;
    return;
  }

  size_t start = 0;
  while (start < descriptionLines_.size()) {
    descriptionPages_.push_back(start);
    int y = descriptionBodyTop_;
    size_t index = start;
    while (index < descriptionLines_.size()) {
      const DefinitionStyledLine& line = descriptionLines_[index];
      const int gap = index == start ? 0 : line.extraGapBeforePx;
      const int lineHeight = renderer.text.getLineHeight(line.fontId);
      if (y + gap + lineHeight > descriptionBottom_ && index > start) break;
      y += gap + lineHeight;
      ++index;
    }
    start = index > start ? index : start + 1;
  }
  descriptionPage_ = std::min(descriptionPage_, static_cast<int>(descriptionPages_.size()) - 1);
}

int BookDetailsActivity::visibleMetadataRows() const {
  return std::max(1, (renderer.getScreenHeight() - kListTop - kBottomMargin) / kRowHeight);
}

int BookDetailsActivity::maxMetadataScroll() const {
  return std::max(0, static_cast<int>(rows_.size()) - visibleMetadataRows());
}

void BookDetailsActivity::movePage(const int direction) {
  if (page_ == kMetadataPage && direction > 0) {
    page_ = kDescriptionPage;
    descriptionPage_ = 0;
    selectedDescriptionWord_ = -1;
    loadDescription();
  } else if (page_ == kDescriptionPage && direction > 0) {
    loadDescription();
    if (descriptionPage_ + 1 < static_cast<int>(descriptionPages_.size())) {
      ++descriptionPage_;
      selectedDescriptionWord_ = -1;
    }
  } else if (page_ == kDescriptionPage && direction < 0) {
    if (descriptionPage_ > 0) {
      --descriptionPage_;
      selectedDescriptionWord_ = -1;
    } else {
      page_ = kMetadataPage;
    }
  }
  updateRequired = true;
}

void BookDetailsActivity::moveVertical(const int direction) {
  if (page_ == kMetadataPage) {
    metadataScroll_ = std::max(0, std::min(maxMetadataScroll(), metadataScroll_ + direction));
  }
  updateRequired = true;
}

void BookDetailsActivity::loop() {
  if (closeInput(true, false)) return;

  if (!metadataLoaded_) {
    loadMetadata();
    metadataLoaded_ = true;
    updateRequired = true;
  }

  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeLeft()) {
      movePage(1);
      return;
    }
    if (mappedInput.wasTouchSwipeRight()) {
      movePage(-1);
      return;
    }
    if (page_ == kMetadataPage && mappedInput.wasTouchSwipeUp()) {
      moveVertical(1);
      return;
    }
    if (page_ == kMetadataPage && mappedInput.wasTouchSwipeDown()) {
      moveVertical(-1);
      return;
    }
    if (page_ == kDescriptionPage && mappedInput.wasTouchSwipeUp()) {
      movePage(1);
      return;
    }
    if (page_ == kDescriptionPage && mappedInput.wasTouchSwipeDown()) {
      movePage(-1);
      return;
    }

    float tapX = 0.0f;
    float tapY = 0.0f;
    if (page_ == kDescriptionPage && mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
      constexpr int caretSize = 30;
      const int x = static_cast<int>(tapX * renderer.getScreenWidth());
      const int y = static_cast<int>(tapY * renderer.getScreenHeight());
      if (x >= lookupButtonX_ && x < lookupButtonX_ + lookupButtonW_ && y >= lookupButtonY_ &&
          y < lookupButtonY_ + lookupButtonH_ && selectedDescriptionWord_ >= 0 &&
          selectedDescriptionWord_ < static_cast<int>(descriptionWordHits_.size())) {
        const std::string word = descriptionWordHits_[static_cast<size_t>(selectedDescriptionWord_)].word;
        openDictionaryLookupFromDetails(word, bookPath_, cachePath_, closeCallback_);
        return;
      }
      if (mappedInput.lastTouchHeldMs() >= 300) {
        const int word = descriptionWordAt(x, y);
        if (word >= 0) {
          selectedDescriptionWord_ = word;
          updateRequired = true;
          return;
        }
      }
      const int caretY = renderer.getScreenHeight() - kBottomMargin - caretSize;
      if (y >= caretY - 10 && y < caretY + caretSize + 10) {
        if (x < kSideMargin + caretSize + 20) {
          movePage(-1);
          return;
        }
        if (x >= renderer.getScreenWidth() - kSideMargin - caretSize - 20) {
          movePage(1);
          return;
        }
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    movePage(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    movePage(1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveVertical(1);
  }

  renderPage();
}

void BookDetailsActivity::renderMetadata() const {
  const int screenWidth = renderer.getScreenWidth();
  const int visibleRows = visibleMetadataRows();
  const int labelFont = systemFontIdForPointSize(8);
  const int valueFont = systemFontId();
  const int labelHeight = renderer.text.getLineHeight(labelFont);
  const int valueHeight = renderer.text.getLineHeight(valueFont);
  const int contentHeight = labelHeight + kSubtitleGap + valueHeight;

  for (int visible = 0; visible < visibleRows; ++visible) {
    const size_t index = static_cast<size_t>(metadataScroll_ + visible);
    if (index >= rows_.size()) break;
    const int itemY = kListTop + visible * kRowHeight;
    const int labelY = itemY + (kRowHeight - contentHeight) / 2;
    const int valueY = labelY + labelHeight + kSubtitleGap;
    const int valueMaxWidth = screenWidth - kSideMargin * 2;
    renderer.text.render(labelFont, kSideMargin, labelY, rows_[index].label.c_str(), true, EpdFontFamily::BOLD);
    if (rows_[index].label == "RATING") {
      const int stars = metadataRatingStars(rows_[index].value);
      constexpr int starSize = 24;
      constexpr int starGap = 4;
      const int starY = valueY + std::max(0, (valueHeight - starSize) / 2);
      for (int star = 0; star < stars; ++star) {
        renderer.bitmap.icon(Star, kSideMargin + star * (starSize + starGap), starY, starSize, starSize);
      }
    } else {
      const std::string clippedValue = renderer.text.truncate(valueFont, rows_[index].value.c_str(), valueMaxWidth);
      renderer.text.render(valueFont, kSideMargin, valueY, clippedValue.c_str(), true, EpdFontFamily::REGULAR);
    }
    if (visible + 1 < visibleRows && index + 1 < rows_.size()) {
      renderer.line.render(kSideMargin, itemY + kRowHeight - 1, screenWidth - kSideMargin,
                           itemY + kRowHeight - 1, true, LineRender::Style::Dotted);
    }
  }
}

void BookDetailsActivity::renderDescription() {
  loadDescription();
  descriptionWordHits_.clear();
  lookupButtonX_ = -1;
  lookupButtonY_ = -1;
  lookupButtonW_ = 0;
  lookupButtonH_ = 0;
  if (descriptionLines_.empty()) {
    selectedDescriptionWord_ = -1;
    renderer.text.centered(systemFontId(), (kListTop + renderer.getScreenHeight()) / 2,
                           "No description available");
    return;
  }
  const size_t start = descriptionPages_.empty() ? 0 : descriptionPages_[static_cast<size_t>(descriptionPage_)];
  const size_t end = descriptionPage_ + 1 < static_cast<int>(descriptionPages_.size())
                         ? descriptionPages_[static_cast<size_t>(descriptionPage_ + 1)]
                         : descriptionLines_.size();
  int wordY = descriptionBodyTop_;
  for (size_t lineIndex = start; lineIndex < end; ++lineIndex) {
    const DefinitionStyledLine& line = descriptionLines_[lineIndex];
    const int lineHeight = renderer.text.getLineHeight(line.fontId);
    const int gap = lineIndex == start ? 0 : line.extraGapBeforePx;
    wordY += gap;
    int wordX = kSideMargin + line.indentPx;
    const int spaceWidth = renderer.text.getSpaceWidth(line.fontId);
    for (size_t atomIndex = 0; atomIndex < line.atoms.size(); ++atomIndex) {
      const DefinitionTextAtom& atom = line.atoms[atomIndex];
      if (atom.spaceBefore && atomIndex > 0) wordX += spaceWidth;
      const int wordWidth = renderer.text.getWidth(line.fontId, atom.text.c_str(), atom.style);
      const std::string word = StarDictLookup::cleanQueryWord(atom.text);
      if (!word.empty() && wordWidth > 0) {
        descriptionWordHits_.push_back({word, wordX, wordY, wordWidth, lineHeight});
      }
      wordX += wordWidth;
    }
    wordY += lineHeight;
  }
  if (selectedDescriptionWord_ >= static_cast<int>(descriptionWordHits_.size())) {
    selectedDescriptionWord_ = -1;
  }
  renderStyledLines(renderer, descriptionLines_, kSideMargin, descriptionBodyTop_, descriptionBottom_, start);
  if (selectedDescriptionWord_ >= 0) {
    const DescriptionWordHit& selected = descriptionWordHits_[static_cast<size_t>(selectedDescriptionWord_)];
    renderer.ui.fillSparseInkLatticeInRect(selected.x, selected.y, std::max(1, selected.width),
                                           std::max(3, selected.height), 2);
    const int font = systemFontId();
    lookupButtonW_ = Button::width(renderer, "Look up", font);
    lookupButtonH_ = Button::height;
    const bool hasNextPage = descriptionPage_ + 1 < static_cast<int>(descriptionPages_.size());
    const int rightEdge = renderer.getScreenWidth() - kSideMargin - (hasNextPage ? 30 + 12 : 0);
    lookupButtonX_ = rightEdge - lookupButtonW_;
    lookupButtonY_ = renderer.getScreenHeight() - kBottomMargin - lookupButtonH_;
    Button::render(renderer, {lookupButtonX_, lookupButtonY_, lookupButtonW_, lookupButtonH_}, "Look up", true,
                   font);
  }
  if (descriptionPages_.size() > 1) {
    const std::string pageLabel = std::to_string(descriptionPage_ + 1) + " / " +
                                  std::to_string(descriptionPages_.size());
    const int pageFont = systemFontIdForPointSize(8);
    renderer.text.centered(pageFont, renderer.getScreenHeight() - kBottomMargin -
                                     renderer.text.getLineHeight(pageFont), pageLabel.c_str());
  }
}

void BookDetailsActivity::content() {
  if (page_ == kMetadataPage) {
    renderMetadata();
  } else {
    renderDescription();
  }

  constexpr int caretSize = 30;
  const int caretY = renderer.getScreenHeight() - kBottomMargin - caretSize;
  if (page_ > kMetadataPage) {
    renderer.bitmap.icon(LibraryFilterLeft, kSideMargin, caretY, caretSize, caretSize);
  }
  if (page_ == kMetadataPage ||
      (page_ == kDescriptionPage && descriptionPage_ + 1 < static_cast<int>(descriptionPages_.size()))) {
    renderer.bitmap.icon(LibraryFilterRight, renderer.getScreenWidth() - kSideMargin - caretSize, caretY,
                         caretSize, caretSize);
  }
}
