#pragma once

/**
 * @file EpubFootnoteBody.h
 * @brief Public interface and types for EpubFootnoteBody.
 */

#include <cstddef>
#include <string>
#include <vector>

#include "WordLookup.h"
#include "dictionary/DictionaryDefinitionLayout.h"

class EpubActivity;

/**
 * Footnote body popup: shown when "View footnote" is picked from the existing long-press word-action
 * menu (see EpubActivity::wordActions/handleWordSelection() - the word is already known from that
 * selection, so unlike EpubDictionaryUi there's no separate word-focus/navigation phase here, just
 * resolve-and-show). Uses WordLookup purely for its framebuffer capture/restore (captureFramebuffer()/
 * restoreFramebuffer() don't touch its word-geometry state, so it's safe to use without buildGeometry())
 * and the same panel presentation as EpubDictionaryUi's definition panel, so this reads as the same
 * visual family as every other reader overlay rather than a new pattern.
 */
class EpubFootnoteBody {
 public:
  bool isActive() const { return mode_; }

  /** Resolves and shows the footnote body for `footnoteTarget` (as produced by
   * ChapterHtmlSlimParser::classifyFootnoteLink: "<S|F>:<resolvedPath>#<fragmentId>", resolvedPath
   * empty means same document), captioned with `markerText` (the tapped word). */
  void show(EpubActivity& act, const std::string& footnoteTarget, const std::string& markerText);
  void exit(EpubActivity& act);
  void handleInput(EpubActivity& act);
  void repaint(EpubActivity& act);
  void drawUiOverlay(EpubActivity& act);

 private:
  bool nextBodyPage();
  bool previousBodyPage();
  void drawBodyPanel(EpubActivity& act);
  /** Resolves the marker's target (same-document or cross-file) and loads its body markup via
   * FootnoteFragmentExtractor - writes the source item to a temp SD file (bounded ~1KB chunks, via
   * Epub::readItemContentsToStream - same pattern as Epub::parseTocNcxFile/parseTocNavFile) rather than
   * holding it in RAM, then does a bounded, chunked on-SD search for id="fragmentId" using small
   * heap-allocated (not stack - this runs deep in the input-handling chain) buffers, so peak memory
   * never scales with the source item's size however large a book's notes/endnotes chapter turns out to
   * be. Returns "" if the item couldn't be loaded or the id wasn't found. */
  std::string loadFootnoteBodyText(EpubActivity& act, const std::string& internalPath,
                                   const std::string& fragmentId);
  /** Actually releases bodyBlocks_/bodyLines_'s heap capacity - see EpubDictionaryUi's
   *  releaseDefinitionMemory() for why .clear() alone isn't enough. */
  void releaseBodyMemory();

  bool mode_ = false;
  WordLookup capture_;  // only used for captureFramebuffer()/restoreFramebuffer()

  std::string markerText_;
  std::vector<DefinitionBlock> bodyBlocks_;
  std::vector<DefinitionStyledLine> bodyLines_;
  std::vector<size_t> bodyPageHistory_;
  size_t bodyScrollLine_ = 0;
  size_t bodyMaxScrollLine_ = 0;
  size_t bodyNextLine_ = 0;
  bool bodyScrollable_ = false;
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
};
