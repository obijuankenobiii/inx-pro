#pragma once

#include <functional>
#include <string>
#include <vector>

#include <Epub/BookMetadataCache.h>

#include "dictionary/DictionaryDefinitionLayout.h"
#include "SubPage.h"

class BookDetailsActivity final : public SubPage {
 public:
  BookDetailsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                      std::string cachePath, std::function<void()> onClose);

  const char* name() const override { return "Book Details"; }
  void onEnter() override;
  void loop() override;

 protected:
  void content() override;

 private:
  struct InfoRow {
    std::string label;
    std::string value;
  };

  struct DescriptionWordHit {
    std::string word;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  static constexpr int kMetadataPage = 0;
  static constexpr int kDescriptionPage = 1;
  static constexpr int kRowHeight = 100;
  static constexpr int kListTop = 80;
  static constexpr int kBottomMargin = 20;

  std::string bookPath_;
  std::string cachePath_;
  std::function<void()> closeCallback_;
  std::vector<InfoRow> rows_;
  std::vector<DefinitionStyledLine> descriptionLines_;
  std::vector<size_t> descriptionPages_;
  std::vector<DescriptionWordHit> descriptionWordHits_;
  std::string descriptionHtml_;
  bool metadataLoaded_ = false;
  bool descriptionLoaded_ = false;
  int page_ = kMetadataPage;
  int descriptionPage_ = 0;
  int metadataScroll_ = 0;
  int descriptionBodyTop_ = kListTop;
  int descriptionBottom_ = 0;
  int selectedDescriptionWord_ = -1;
  int lookupButtonX_ = -1;
  int lookupButtonY_ = -1;
  int lookupButtonW_ = 0;
  int lookupButtonH_ = 0;

  void loadMetadata();
  void loadDescription();
  void makeDescriptionPages();
  int descriptionWordAt(int x, int y) const;
  void addRow(const char* label, const std::string& value);
  int visibleMetadataRows() const;
  int maxMetadataScroll() const;
  void movePage(int direction);
  void moveVertical(int direction);
  void renderMetadata() const;
  void renderDescription();
};
