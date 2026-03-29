#pragma once

#include <functional>
#include <string>
#include <vector>

#include "SubPage.h"
#include "components/search/SearchKeyboard.h"
#include "util/LibraryIndex.h"

class Search final : public SubPage {
 public:
  Search(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> returnToCaller);

  const char* name() const override { return "Search"; }
  void onEnter() override;
  void loop() override;

 protected:
  void content() override;

 private:
  std::string query;
  std::vector<LibraryIndex::Book> results;
  SearchKeyboard keyboard;
  int scroll = 0;
  bool keyboardCollapsed_ = false;

  int resultHeight() const;
  int keyboardHeight() const;
  int keyboardControlHeight() const;
  int keyboardBottom() const;
  int keyboardTop() const;
  int refreshX() const;
  int refreshY() const;
  int resultsTop() const;
  int maxScroll() const;
  void updateResults();
  void drawResults() const;
};
