#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "activity/reader/Epub/EpubAnnotationRecord.h"
#include "dictionary/DictionaryDefinitionLayout.h"
#include "SubPage.h"

class HomeSubPage final : public SubPage {
 public:
  enum class Section { Bookmarks, Highlights, Favorites, Dictionary };

  HomeSubPage(GfxRenderer& renderer, MappedInputManager& mappedInput, Section section,
              std::function<void()> close, std::string lookupWord = {});

  const char* name() const override;
  void onEnter() override;
  void loop() override;

 protected:
  void menu() override;
  void content() override;

 private:
  struct Row {
    std::string label;
    std::string title;
    std::string cachePath;
    int spine = -1;
    int page = -1;
    EpubAnnotationRecord annotation;
    bool highlight = false;
    bool favorite = false;
    std::string bookPath;
    int dictionaryIndex = -1;
    bool groupStart = false;
  };

  Section section;
  std::string headerName_;
  std::vector<Row> rows;
  std::vector<int> pages;
  std::string selectedBookPath_;
  int page = 0;
  int selected = -1;

  void load();
  void loadBookmarks(const std::string& cachePath, const std::string& title);
  void loadHighlights(const std::string& cachePath, const std::string& title);
  void makePages();
  bool showingBookList() const;
  int listTop() const;
  bool remove(int index);
  bool contentInput();
  void highlightContent(const Row& row);
  void dictionaryContent(const Row& row);
  void lookupContent();
  bool dictionaryLookupInput();
  void performDictionaryLookup();
  void closeDictionaryLookup();
  void saveDictionaryLookup();
  void startNoteTranscription();
  void pollNoteTranscription();

  bool transcriptionPending_ = false;
  uint32_t transcriptionLastRefreshMs_ = 0;
  uint8_t transcriptionDots_ = 1;
  std::string transcriptionCachePath_;
  EpubAnnotationRecord transcriptionRecord_{};
  int dictionarySelected_ = -1;
  std::vector<DefinitionStyledLine> dictionaryLines_;
  std::string lookupWord_;
  std::string lookupDefinition_;
  std::vector<DefinitionStyledLine> lookupLines_;
  size_t lookupScrollLine_ = 0;
  size_t lookupMaxScrollLine_ = 0;
  size_t lookupNextLine_ = 0;
  bool lookupShowing_ = false;
  bool lookupLoading_ = false;
  bool lookupHasDefinition_ = false;
  bool lookupAlreadySaved_ = false;
  int lookupSaveX_ = -1;
  int lookupSaveY_ = -1;
  int lookupSaveW_ = 0;
  int lookupSaveH_ = 0;
  int lookupNextX_ = -1;
  int lookupNextY_ = -1;
  int lookupNextW_ = 0;
  int lookupNextH_ = 0;
};
