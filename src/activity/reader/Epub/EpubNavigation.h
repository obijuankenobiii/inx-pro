#pragma once

#include <memory>

class EpubActivity;
class TocSidebar;
class MappedInputManager;

/**
 * Owns the table-of-contents surface that sits above the rendered EPUB page.
 * TocSidebar remains responsible for drawing and local input state; this class
 * wires it to the active book and performs reader-level navigation on selection.
 */
class EpubNavigation {
 public:
  explicit EpubNavigation(EpubActivity& activity);
  ~EpubNavigation();

  EpubNavigation(const EpubNavigation&) = delete;
  EpubNavigation& operator=(const EpubNavigation&) = delete;

  bool handleInput(MappedInputManager& input);
  void render();
  void reset();

  bool isTocOpen() const;

  void openTableOfContents();
  void generateFullData();
  void regenerateThumbnail();

 private:
  void onDrawerDismissed();
  void onTocChapterSelected(int spineIndex);
  void onKoreaderSyncRequested();

  EpubActivity& activity_;
  std::unique_ptr<TocSidebar> tocSidebar_;
};
