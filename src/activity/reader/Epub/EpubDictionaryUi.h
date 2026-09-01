#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "WordLookup.h"
#include "dictionary/DictionaryDefinitionLayout.h"
#include "dictionary/StarDictLookup.h"

class EpubActivity;

/**
 * Dictionary lookup UI: chord entry, D-pad word navigation, framebuffer capture/repaint, and an
 * on-SD StarDict lookup - same interaction shape as EpubAnnotationUi (see that file), but without
 * range selection/persistence: focus always highlights a single word, and Confirm looks it up.
 */
class EpubDictionaryUi {
 public:
  EpubDictionaryUi();

  bool isActive() const { return mode_; }

  void tryChordEnter(EpubActivity& act);
  void enter(EpubActivity& act);
  bool lookupAt(EpubActivity& act, int x, int y);
  void exit(EpubActivity& act);
  void handleInput(EpubActivity& act);
  void repaint(EpubActivity& act);
  void drawUiOverlay(EpubActivity& act);

 private:
  void drawFocusHighlight(EpubActivity& act);
  void drawDefinitionPanel(EpubActivity& act);
  void drawDictionaryPicker(EpubActivity& act);
  void scanDictionaries();
  bool handleDictionaryPickerInput(EpubActivity& act);
  bool previousDefinitionPage();
  bool nextDefinitionPage();
  void performLookup(EpubActivity& act);
  void ensureDictionaryOpen();
  void saveCurrentWord(EpubActivity& act);
  /** Actually releases currentDefinition_/definitionBlocks_/definitionLines_'s heap capacity (not
   *  just .clear(), which keeps it reserved for reuse) - a big dictionary entry's parsed/laid-out
   *  form can run into the tens of KB, and .clear() alone would leave that reserved for as long as
   *  the book stays open even after the user backs out of viewing it. */
  void releaseDefinitionMemory();

  bool mode_ = false;
  bool controlsVisible_ = true;
  bool dictionaryPickerOpen_ = false;
  int dictionaryPickerScroll_ = 0;
  std::vector<std::string> dictionaryFolders_;
  WordLookup wordLookup_;

  StarDictLookup dict_;
  bool dictOpenAttempted_ = false;
  bool showingDefinition_ = false;
  std::string lookedUpWord_;
  bool wordAlreadySaved_ = false;
  std::string currentDefinition_;
  std::vector<DefinitionBlock> definitionBlocks_;
  std::vector<DefinitionStyledLine> definitionLines_;
  size_t definitionScrollLine_ = 0;
  size_t definitionMaxScrollLine_ = 0;
  size_t definitionNextLine_ = 0;
  std::vector<size_t> definitionPageHistory_;
  bool definitionScrollable_ = false;
  int panelX_ = -1;
  int panelY_ = -1;
  int panelW_ = 0;
  int panelH_ = 0;
  int closeX_ = -1;
  int closeY_ = -1;
  int closeSize_ = 0;
  int nextX_ = -1;
  int nextY_ = -1;
  int nextW_ = 0;
  int nextH_ = 0;
  int previousX_ = -1;
  int previousY_ = -1;
  int previousW_ = 0;
  int previousH_ = 0;
  int saveX_ = -1;
  int saveY_ = -1;
  int saveW_ = 0;
  int saveH_ = 0;

  unsigned long chordStartMs_ = 0;
  bool chordConsumed_ = false;
};
