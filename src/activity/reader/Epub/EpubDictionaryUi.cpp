#include "EpubDictionaryUi.h"

#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

#include "EpubActivity.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/global/PopUp.h"
#include "dictionary/DictionaryDefinitionLayout.h"
#include "images/Close.h"
#include "images/LibraryFilterRight.h"
#include "state/SavedDictionaryWords.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/SdIoMutex.h"
#include "util/StringUtils.h"

namespace {

constexpr unsigned long kChordHoldMs = 600;
constexpr int kHighlightLatticeStepPx = 2;

constexpr int kDefinitionPanelMargin = 16;
constexpr int kDefinitionPanelPad = 20;
constexpr int kLookupOverlayMargin = 20;
constexpr int kLookupCaretSize = 40;
constexpr int kLookupCaretSourceSize = 30;
constexpr int kLookupCaretGap = 10;

ButtonBounds lookupButtonBounds(const GfxRenderer& renderer) {
  const int font = systemFontId();
  const int width = Button::width(renderer, "Look up", font);
  return {renderer.getScreenWidth() - kLookupOverlayMargin - width,
          renderer.getScreenHeight() - kLookupOverlayMargin - Button::height, width, Button::height};
}

ButtonBounds lookupCaretBounds(const GfxRenderer& renderer) {
  return {kLookupOverlayMargin, renderer.getScreenHeight() - kLookupOverlayMargin - kLookupCaretSize,
          kLookupCaretSize, kLookupCaretSize};
}

ButtonBounds dictionaryButtonBounds(const GfxRenderer& renderer, const char* label) {
  const int font = systemFontId();
  const int naturalWidth = Button::width(renderer, label, font);
  const ButtonBounds lookup = lookupButtonBounds(renderer);
  const int dictionaryLeftLimit = kLookupOverlayMargin + kLookupCaretSize + kLookupCaretGap;
  const int availableWidth = lookup.x - kLookupCaretGap - dictionaryLeftLimit;
  const int maxWidth = std::max(120, std::min(280, availableWidth));
  const int width = std::min(naturalWidth, maxWidth);
  const int dictionaryX = lookup.x - kLookupCaretGap - width;
  return {dictionaryX, renderer.getScreenHeight() - kLookupOverlayMargin - Button::height, width, Button::height};
}

std::string dictionaryButtonLabel(const GfxRenderer& renderer) {
  std::string label = READER_SETTINGS.dictionaryFolder[0] == '\0' ? "Select dictionary"
                                                                    : READER_SETTINGS.dictionaryFolder;
  constexpr const char* disclosure = " ‹›";
  const std::string labelWithDisclosure = label + disclosure;
  const ButtonBounds bounds = dictionaryButtonBounds(renderer, labelWithDisclosure.c_str());
  const int font = systemFontId();
  const int disclosureWidth = renderer.text.getWidth(font, disclosure);
  const int maxTextWidth = std::max(1, bounds.width - Button::horizontalPadding * 2 - disclosureWidth);
  return renderer.text.truncate(font, label.c_str(), maxTextWidth, EpdFontFamily::REGULAR) + disclosure;
}

std::string stripSurroundingPunctuation(const std::string& s) {
  size_t start = 0;
  size_t end = s.size();
  auto keep = [](unsigned char c) { return std::isalnum(c) || c == '\'' || c == '-'; };
  while (start < end && !keep(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  while (end > start && !keep(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

bool endsWithLineBreakHyphen(const PageWordHit& first, const PageWordHit& second) {
  return !first.text.empty() && first.text.back() == '-' && first.elementIndex != second.elementIndex &&
         first.screenY != second.screenY;
}

std::string lookupWordAt(const std::vector<PageWordHit>& words, const size_t focus) {
  if (focus >= words.size()) {
    return {};
  }

  std::string word = words[focus].text;
  if (word.empty()) {
    return {};
  }

  if (focus + 1 < words.size() && endsWithLineBreakHyphen(words[focus], words[focus + 1]) &&
      !words[focus + 1].text.empty()) {
    word.pop_back();
    word += words[focus + 1].text;
  } else if (focus > 0 && endsWithLineBreakHyphen(words[focus - 1], words[focus])) {
    word = words[focus - 1].text.substr(0, words[focus - 1].text.size() - 1) + word;
  }
  return stripSurroundingPunctuation(word);
}

}

EpubDictionaryUi::EpubDictionaryUi() = default;

void EpubDictionaryUi::tryChordEnter(EpubActivity& act) {
  if (!act.epub || !act.section || mode_) {
    return;
  }
  const bool down = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_DOWN);
  const bool left = act.mappedInput.rawHalIsPressed(HalGPIO::BTN_LEFT);
  if (down && left) {
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

void EpubDictionaryUi::enter(EpubActivity& act) {
  if (!act.section || !act.epub) {
    return;
  }
  act.btnBindings_.reset();
  mode_ = true;
  controlsVisible_ = true;
  dictionaryPickerOpen_ = false;
  dictionaryPickerScroll_ = 0;
  dictionaryFolders_.clear();
  showingDefinition_ = false;
  lookedUpWord_.clear();
  releaseDefinitionMemory();
  wordLookup_.clear();

  if (!wordLookup_.buildGeometry(act)) {
    act.readerPopup("No text to look up");
    exit(act);
    return;
  }
  if (!wordLookup_.captureFramebuffer(act)) {
    act.readerPopup("Could not capture page");
    exit(act);
    return;
  }
  act.updateRequired = true;
}

bool EpubDictionaryUi::lookupAt(EpubActivity& act, const int x, const int y) {
  enter(act);
  if (!mode_ || !wordLookup_.focusAt(x, y)) {
    if (mode_) {
      exit(act);
    }
    return false;
  }
  performLookup(act);
  return true;
}

void EpubDictionaryUi::exit(EpubActivity& act) {
  mode_ = false;
  controlsVisible_ = true;
  dictionaryPickerOpen_ = false;
  dictionaryPickerScroll_ = 0;
  dictionaryFolders_.clear();
  showingDefinition_ = false;
  lookedUpWord_.clear();
  releaseDefinitionMemory();
  wordLookup_.clear();
  act.updateRequired = true;
}

void EpubDictionaryUi::scanDictionaries() {
  dictionaryFolders_.clear();
  dictionaryPickerScroll_ = 0;

  SdIoMutex::Lock ioLock;
  FsFile root = SdMan.open("/dictionaries");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }

  for (FsFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (entry.isDirectory()) {
      char name[160] = {};
      entry.getName(name, sizeof(name));
      const std::string folderName = name;
      const std::string folderPath = std::string("/dictionaries/") + folderName;
      FsFile dictionary = SdMan.open(folderPath.c_str());
      bool hasIdx = false;
      bool hasDict = false;
      if (dictionary && dictionary.isDirectory()) {
        for (FsFile file = dictionary.openNextFile(); file; file = dictionary.openNextFile()) {
          if (!file.isDirectory()) {
            char fileName[160] = {};
            file.getName(fileName, sizeof(fileName));
            const std::string nameString = fileName;
            if (nameString.size() > 0 && nameString[0] != '.') {
              hasIdx = hasIdx || StringUtils::checkFileExtension(nameString, ".idx");
              hasDict = hasDict || StringUtils::checkFileExtension(nameString, ".dict");
            }
          }
          file.close();
          if (hasIdx && hasDict) break;
        }
      }
      dictionary.close();
      if (hasIdx && hasDict) dictionaryFolders_.push_back(folderName);
    }
    entry.close();
  }
  root.close();
  std::sort(dictionaryFolders_.begin(), dictionaryFolders_.end());
}

bool EpubDictionaryUi::handleDictionaryPickerInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;
  const PopUpBounds box = PopUp::bounds(act.renderer, static_cast<int>(dictionaryFolders_.size()));
  const int maxScroll = std::max(0, static_cast<int>(dictionaryFolders_.size()) - box.rows);

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    dictionaryPickerOpen_ = false;
    act.updateRequired = true;
    return true;
  }
  if (m.wasTouchSwipeUpForRenderer(act.renderer)) {
    dictionaryPickerScroll_ = std::min(dictionaryPickerScroll_ + box.rows, maxScroll);
    act.updateRequired = true;
    return true;
  }
  if (m.wasTouchSwipeDownForRenderer(act.renderer)) {
    dictionaryPickerScroll_ = std::max(0, dictionaryPickerScroll_ - box.rows);
    act.updateRequired = true;
    return true;
  }
  if (!m.hasTouch()) return true;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!m.wasTouchTapInScreen(act.renderer, tapNx, tapNy)) return true;
  const int x = static_cast<int>(tapNx * act.renderer.getScreenWidth());
  const int y = static_cast<int>(tapNy * act.renderer.getScreenHeight());
  if (x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height) {
    dictionaryPickerOpen_ = false;
    act.updateRequired = true;
    return true;
  }

  const int row = (y - box.y - box.header) / box.row;
  const int index = dictionaryPickerScroll_ + row;
  if (row >= 0 && row < box.rows && index >= 0 && index < static_cast<int>(dictionaryFolders_.size())) {
    const std::string& chosen = dictionaryFolders_[static_cast<size_t>(index)];
    strncpy(READER_SETTINGS.dictionaryFolder, chosen.c_str(), sizeof(READER_SETTINGS.dictionaryFolder) - 1);
    READER_SETTINGS.dictionaryFolder[sizeof(READER_SETTINGS.dictionaryFolder) - 1] = '\0';
    READER_SETTINGS.saveToFile();
    dict_.close();
    dictOpenAttempted_ = false;
    dictionaryPickerOpen_ = false;
    act.updateRequired = true;
  }
  return true;
}

/** See header - swaps with a default-constructed temporary rather than .clear(), so the heap
 *  capacity a big definition needed is actually returned instead of sitting reserved for reuse. */
void EpubDictionaryUi::releaseDefinitionMemory() {
  std::string().swap(currentDefinition_);
  std::vector<DefinitionBlock>().swap(definitionBlocks_);
  std::vector<DefinitionStyledLine>().swap(definitionLines_);
  definitionScrollLine_ = 0;
  definitionMaxScrollLine_ = 0;
  definitionNextLine_ = 0;
  std::vector<size_t>().swap(definitionPageHistory_);
  definitionScrollable_ = false;
  nextX_ = -1;
  nextY_ = -1;
  nextW_ = 0;
  nextH_ = 0;
  previousX_ = -1;
  previousY_ = -1;
  previousW_ = 0;
  previousH_ = 0;
  saveX_ = -1;
  saveY_ = -1;
  saveW_ = 0;
  saveH_ = 0;
}

void EpubDictionaryUi::ensureDictionaryOpen() {
  if (dictOpenAttempted_) {
    return;
  }
  dictOpenAttempted_ = true;
  if (READER_SETTINGS.dictionaryFolder[0] == '\0') {
    INX_SERIAL.printf("[%lu] [DICT] ensureDictionaryOpen: READER_SETTINGS.dictionaryFolder is empty\n", millis());
    return;
  }
  const std::string folder = std::string("/dictionaries/") + READER_SETTINGS.dictionaryFolder;
  const bool opened = dict_.open(folder);
  INX_SERIAL.printf("[%lu] [DICT] ensureDictionaryOpen: open('%s') -> %d\n", millis(), folder.c_str(), opened ? 1 : 0);
}

void EpubDictionaryUi::performLookup(EpubActivity& act) {
  const auto& words = wordLookup_.words();
  if (words.empty() || wordLookup_.focus() >= words.size()) {
    return;
  }
  lookedUpWord_ = lookupWordAt(words, wordLookup_.focus());
  currentDefinition_.clear();
  definitionScrollLine_ = 0;
  definitionPageHistory_.clear();
  wordAlreadySaved_ = !lookedUpWord_.empty() && SAVED_WORDS.contains(lookedUpWord_);

  bool truncated = false;
  if (lookedUpWord_.empty()) {
    currentDefinition_ = "Nothing to look up.";
  } else if (READER_SETTINGS.dictionaryFolder[0] == '\0') {
    currentDefinition_ = "No dictionary selected. Pick one in Settings > Reader > Choose dictionary.";
  } else {
    act.readerPopup("Looking up...");
    ensureDictionaryOpen();
    if (!dict_.isOpen()) {
      currentDefinition_ = "Could not open the selected dictionary.";
    } else if (!dict_.lookup(lookedUpWord_, currentDefinition_, &truncated)) {
      currentDefinition_ = "No definition found.";
    }
    INX_SERIAL.printf("[%lu] [DICT] performLookup: lookup returned ok=%d bytes=%u\n", millis(),
                  currentDefinition_ != "No definition found." ? 1 : 0,
                  static_cast<unsigned>(currentDefinition_.size()));
  }
  if (truncated) {
    while (!currentDefinition_.empty()) {
      const auto last = static_cast<unsigned char>(currentDefinition_.back());
      if ((last & 0xC0) == 0x80) {
        currentDefinition_.pop_back();
        continue;
      }
      if (last >= 0xC0) {
        currentDefinition_.pop_back();
      }
      break;
    }
    currentDefinition_ += " \xE2\x80\xA6";
  }
  INX_SERIAL.printf("[%lu] [DICT] performLookup: parsing definition bytes=%u\n", millis(),
                static_cast<unsigned>(currentDefinition_.size()));
  definitionBlocks_ = parseHtmlToBlocks(currentDefinition_);
  INX_SERIAL.printf("[%lu] [DICT] performLookup: parsed blocks=%u\n", millis(),
                static_cast<unsigned>(definitionBlocks_.size()));
  const int textWidth =
      (act.renderer.getScreenWidth() - kDefinitionPanelMargin * 2) - kDefinitionPanelPad * 2;
  definitionLines_ = layoutDefinitionBlocks(act.renderer, definitionBlocks_, textWidth);
  INX_SERIAL.printf("[%lu] [DICT] performLookup: laid out lines=%u\n", millis(),
                static_cast<unsigned>(definitionLines_.size()));
  showingDefinition_ = true;
  act.updateRequired = true;
}

/** Saves lookedUpWord_ and the definition currently shown in the panel to the global saved-words
 *  list. The operation is idempotent so both the visible button and hardware Confirm are safe to
 *  repeat. */
void EpubDictionaryUi::saveCurrentWord(EpubActivity& act) {
  if (lookedUpWord_.empty() || wordAlreadySaved_) {
    return;
  }
  if (SAVED_WORDS.add(lookedUpWord_, currentDefinition_)) {
    wordAlreadySaved_ = true;
    act.updateRequired = true;
  }
}

void EpubDictionaryUi::handleInput(EpubActivity& act) {
  const MappedInputManager& m = act.mappedInput;

  if (dictionaryPickerOpen_) {
    handleDictionaryPickerInput(act);
    return;
  }

  if (m.hasTouch() && m.wasTouchSwipeUpForRenderer(act.renderer)) {
    if (showingDefinition_) {
      showingDefinition_ = false;
      releaseDefinitionMemory();
      act.updateRequired = true;
    } else {
      exit(act);
      act.startPageTimer();
    }
    return;
  }

  if (m.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (m.wasTouchTapInScreen(act.renderer, tapNx, tapNy)) {
      const int x = static_cast<int>(tapNx * act.renderer.getScreenWidth());
      const int y = static_cast<int>(tapNy * act.renderer.getScreenHeight());
      if (showingDefinition_) {
        if (!definitionPageHistory_.empty() && x >= previousX_ && x < previousX_ + previousW_ && y >= previousY_ &&
            y < previousY_ + previousH_) {
          if (previousDefinitionPage()) {
            act.updateRequired = true;
          }
          return;
        }
        if (x >= nextX_ && x < nextX_ + nextW_ && y >= nextY_ && y < nextY_ + nextH_) {
          if (nextDefinitionPage()) {
            act.updateRequired = true;
          }
          return;
        }
        if (x >= saveX_ && x < saveX_ + saveW_ && y >= saveY_ && y < saveY_ + saveH_) {
          saveCurrentWord(act);
          return;
        }
        constexpr int hitPadding = 12;
        if (x >= closeX_ - hitPadding && x < closeX_ + closeSize_ + hitPadding && y >= closeY_ - hitPadding &&
            y < closeY_ + closeSize_ + hitPadding) {
          showingDefinition_ = false;
          releaseDefinitionMemory();
          act.updateRequired = true;
          return;
        }
        if (x < panelX_ || x >= panelX_ + panelW_ || y < panelY_ || y >= panelY_ + panelH_) {
          showingDefinition_ = false;
          releaseDefinitionMemory();
          act.updateRequired = true;
          return;
        }
      } else {
        const ButtonBounds caret = lookupCaretBounds(act.renderer);
        if (x >= caret.x && x < caret.x + caret.width && y >= caret.y && y < caret.y + caret.height) {
          controlsVisible_ = !controlsVisible_;
          act.updateRequired = true;
          return;
        }

        if (controlsVisible_) {
          constexpr int margin = 20;
          constexpr int closeSize = 40;
          const int closeX = act.renderer.getScreenWidth() - margin - closeSize;
          const int closeY = margin;
          if (x >= closeX && x < closeX + closeSize && y >= closeY && y < closeY + closeSize) {
            exit(act);
            act.startPageTimer();
            return;
          }

          const int font = systemFontId();
          const std::string dictionaryLabel = dictionaryButtonLabel(act.renderer);
          const ButtonBounds dictionary = dictionaryButtonBounds(act.renderer, dictionaryLabel.c_str());
          if (x >= dictionary.x && x < dictionary.x + dictionary.width && y >= dictionary.y &&
              y < dictionary.y + dictionary.height) {
            scanDictionaries();
            if (dictionaryFolders_.empty()) {
              act.readerPopup("No dictionaries found");
            } else {
              dictionaryPickerOpen_ = true;
              dictionaryPickerScroll_ = 0;
              act.updateRequired = true;
            }
            return;
          }

          const ButtonBounds lookup = lookupButtonBounds(act.renderer);
          if (x >= lookup.x && x < lookup.x + lookup.width && y >= lookup.y && y < lookup.y + lookup.height) {
            performLookup(act);
            return;
          }
        }
        if (wordLookup_.focusAt(x, y)) {
          act.updateRequired = true;
          return;
        }
      }
    }
  }

  if (m.wasReleased(MappedInputManager::Button::Back)) {
    if (showingDefinition_) {
      showingDefinition_ = false;
      releaseDefinitionMemory();
      act.updateRequired = true;
    } else {
      exit(act);
      act.startPageTimer();
    }
    return;
  }
  if (m.wasReleased(MappedInputManager::Button::Confirm)) {
    if (showingDefinition_) {
      saveCurrentWord(act);
    } else {
      performLookup(act);
    }
    act.updateRequired = true;
    return;
  }
  if (showingDefinition_) {
    constexpr size_t kScrollLinesPerPress = 3;
    if (m.wasPressed(MappedInputManager::Button::Up)) {
      definitionScrollLine_ = (definitionScrollLine_ > kScrollLinesPerPress) ? definitionScrollLine_ - kScrollLinesPerPress : 0;
      act.updateRequired = true;
    } else if (m.wasPressed(MappedInputManager::Button::Down)) {
      definitionScrollLine_ += kScrollLinesPerPress;
      act.updateRequired = true;
    }
    return;
  }
  if (wordLookup_.handleNavigation(act)) {
    act.updateRequired = true;
    return;
  }
}

bool EpubDictionaryUi::previousDefinitionPage() {
  if (definitionPageHistory_.empty()) {
    return false;
  }
  definitionScrollLine_ = definitionPageHistory_.back();
  definitionPageHistory_.pop_back();
  return true;
}

bool EpubDictionaryUi::nextDefinitionPage() {
  if (!definitionScrollable_ || definitionScrollLine_ >= definitionMaxScrollLine_) {
    return false;
  }
  definitionPageHistory_.push_back(definitionScrollLine_);
  definitionScrollLine_ = std::min(definitionMaxScrollLine_, std::max(definitionScrollLine_ + 1, definitionNextLine_));
  return true;
}

void EpubDictionaryUi::repaint(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  if (!wordLookup_.restoreFramebuffer(act)) {
    act.renderScreen(true);
    return;
  }
  drawUiOverlay(act);
}

void EpubDictionaryUi::drawFocusHighlight(EpubActivity& act) {
  const auto& words = wordLookup_.words();
  if (words.empty() || wordLookup_.focus() >= words.size()) {
    return;
  }
  const PageWordHit& w = words[wordLookup_.focus()];
  act.renderer.ui.fillSparseInkLatticeInRect(w.screenX, std::max(0, w.screenY), std::max(1, w.screenW),
                                             std::max(3, w.screenH), kHighlightLatticeStepPx);
}

void EpubDictionaryUi::drawDefinitionPanel(EpubActivity& act) {
  const int screenW = act.renderer.getScreenWidth();
  const int screenH = act.renderer.getScreenHeight();
  constexpr int margin = kDefinitionPanelMargin;
  constexpr int pad = kDefinitionPanelPad;
  const int panelX = margin;
  const int panelW = screenW - margin * 2;
  const int panelBottom = screenH - margin;
  const int defaultPanelTop = screenH * 2 / 5;
  const int minPanelTop = margin;

  const int titleFontId = MONTSERRAT_12_FONT_ID;
  const int titleH = act.renderer.text.getLineHeight(titleFontId);
  constexpr int closeSize = 40;
  const int headerH = std::max(titleH, closeSize);
  const auto& styledLines = definitionLines_;

  int contentH = 0;
  for (const DefinitionStyledLine& sl : styledLines) {
    contentH += act.renderer.text.getLineHeight(sl.fontId) + sl.extraGapBeforePx;
  }

  constexpr int kTitleGapPx = 8;
  constexpr int kButtonGapPx = 12;
  const int buttonFont = systemFontId();
  const char* saveLabel = wordAlreadySaved_ ? "Saved" : "Save";
  const int saveW = Button::width(act.renderer, saveLabel, buttonFont);
  const int buttonH = Button::height;
  const int neededPanelH = pad * 2 + headerH + kTitleGapPx * 2 + contentH + kButtonGapPx + buttonH;
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
  const std::string title = act.renderer.text.truncate(titleFontId, lookedUpWord_.c_str(),
                                                        closeX_ - (panelX + pad) - 8, EpdFontFamily::BOLD);
  act.renderer.text.render(titleFontId, panelX + pad, titleY, title.c_str(), true, EpdFontFamily::BOLD);
  act.renderer.bitmap.icon(Close, closeX_, closeY_, closeSize, closeSize);
  if (wordAlreadySaved_) {
    const int tagFontId = MONTSERRAT_8_FONT_ID;
    const char* tag = "\xE2\x98\x85 Saved";
    const int tagW = act.renderer.text.getWidth(tagFontId, tag);
    const int tagY = titleY + (titleH - act.renderer.text.getLineHeight(tagFontId)) / 2;
    act.renderer.text.render(tagFontId, closeX_ - 8 - tagW, tagY, tag, true);
  }
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
  saveW_ = saveW;
  saveH_ = buttonH;
  saveX_ = panelX + panelW - pad - saveW;
  saveY_ = contentBottom - saveH_;
  contentBottom = saveY_ - kButtonGapPx;

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
    previousW_ = buttonH;
    previousH_ = buttonH;
    previousX_ = panelX + pad;
    previousY_ = saveY_;

    const int buttonWidth = Button::width(act.renderer, "Next", buttonFont);
    nextW_ = buttonWidth;
    nextH_ = buttonH;
    nextX_ = panelX + panelW - pad - nextW_;
    nextY_ = saveY_;
    saveX_ = nextX_ - kButtonGapPx - saveW_;
  }

  definitionScrollable_ = maxScrollLine > 0;
  definitionMaxScrollLine_ = maxScrollLine;
  definitionScrollLine_ = std::min(definitionScrollLine_, definitionMaxScrollLine_);

  int usedH = 0;
  size_t nextLine = definitionScrollLine_;
  while (nextLine < styledLines.size()) {
    const DefinitionStyledLine& line = styledLines[nextLine];
    const int lineH = act.renderer.text.getLineHeight(line.fontId) + line.extraGapBeforePx;
    if (usedH + lineH > contentBottom - y) break;
    usedH += lineH;
    ++nextLine;
  }
  definitionNextLine_ = nextLine;

  renderStyledLines(act.renderer, styledLines, panelX + pad, y, contentBottom, definitionScrollLine_);
  if (!definitionPageHistory_.empty()) {
    Button::render(act.renderer, {previousX_, previousY_, previousW_, previousH_}, "", false, buttonFont);
    constexpr int caretSize = 40;
    act.renderer.bitmap.iconScaled(LibraryFilterRight, previousX_ + (previousW_ - caretSize) / 2,
                                   previousY_ + (previousH_ - caretSize) / 2, kLookupCaretSourceSize,
                                   kLookupCaretSourceSize, caretSize, caretSize,
                                   BitmapRender::Orientation::Rotate180);
  }
  Button::render(act.renderer, {saveX_, saveY_, saveW_, saveH_}, saveLabel, false, buttonFont);
  if (definitionScrollLine_ < definitionMaxScrollLine_) {
    Button::render(act.renderer, {nextX_, nextY_, nextW_, nextH_}, "Next", true, buttonFont);
  } else {
    nextX_ = -1;
    nextY_ = -1;
    nextW_ = 0;
    nextH_ = 0;
  }
}

void EpubDictionaryUi::drawUiOverlay(EpubActivity& act) {
  if (!mode_) {
    return;
  }
  const GfxRenderer::Orientation o = act.renderer.getOrientation();
  if (showingDefinition_) {
    drawDefinitionPanel(act);
  } else {
    drawFocusHighlight(act);
    if (controlsVisible_) {
      constexpr int margin = 20;
      constexpr int closeSize = 40;
      const int closeX = act.renderer.getScreenWidth() - margin - closeSize;
      act.renderer.bitmap.icon(Close, closeX, margin, closeSize, closeSize);

      const int font = systemFontId();
      const std::string dictionaryLabel = dictionaryButtonLabel(act.renderer);
      const ButtonBounds dictionary = dictionaryButtonBounds(act.renderer, dictionaryLabel.c_str());
      Button::render(act.renderer, dictionary, dictionaryLabel.c_str(), false, font);
      const ButtonBounds lookup = lookupButtonBounds(act.renderer);
      Button::render(act.renderer, lookup, "Look up", true, font);
      if (dictionaryPickerOpen_) {
        const PopUpBounds box = PopUp::bounds(act.renderer, static_cast<int>(dictionaryFolders_.size()));
        PopUp::background(act.renderer, box);
        PopUp::title(act.renderer, box, "Dictionary");
        PopUp::list(act.renderer, box, dictionaryFolders_, -1, dictionaryPickerScroll_);
        PopUp::border(act.renderer, box);
      }
    }
    const ButtonBounds caret = lookupCaretBounds(act.renderer);
    const auto orientation = controlsVisible_ ? BitmapRender::Orientation::Rotate90CW
                                              : BitmapRender::Orientation::Rotate270CW;
    act.renderer.bitmap.iconScaled(LibraryFilterRight, caret.x, caret.y, kLookupCaretSourceSize,
                                   kLookupCaretSourceSize, caret.width, caret.height, orientation);
  }
  act.renderer.setOrientation(GfxRenderer::Portrait);
  const char* back = showingDefinition_ ? "Close" : "Exit";
  const char* mid = showingDefinition_ ? (wordAlreadySaved_ ? "Saved" : "Save") : "Look up";
  const auto labels = act.mappedInput.mapLabels(back, mid, "Prev", "Next");
  const bool showUpDown = !showingDefinition_ || definitionScrollable_;
  act.renderer.setOrientation(o);
  act.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
