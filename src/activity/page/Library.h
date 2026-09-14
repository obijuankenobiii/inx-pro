#pragma once

#include <map>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>

#include "Page.h"
#include "system/PluginManager.h"
#include "util/LibraryIndex.h"
#include "views/Library/Grid.h"
#include "views/Library/List.h"
#include "views/Library/Thumb.h"

class Library final : public Page {
 public:
  Library(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path = "/",
          PluginManager::LibraryMenuLink pluginMenu = {});

  const char* name() const override { return "Library"; }
  const std::string& currentPath() const { return path; }
  void onEnter() override;
  void loop() override;

 protected:
  void center() const override;
  Action centerTap(int tapX, int tapY) const override;
  void title() const override;
  bool menuAction(Action action) override;
  void menu() override;
  void content() override;
  void refresh() override;
  void search() override;

 private:
  enum class Sort { TitleAZ, TitleZA, FolderAZ, FolderZA, AuthorAZ, AuthorZA };
  enum class View { List, Grid, Thumb };
  enum class FilterTab { Title, Type, Options };
  enum class StateFilter { None, Favorites, Reading, Finished, Author, Plugin };

  static constexpr int buttonSize = 40;
  static constexpr int buttonGap = 25;
  static constexpr int refreshTouchPadding = 16;

  std::string path;
  PluginManager::LibraryMenuLink pluginMenu_;
  bool pluginMode_ = false;
  std::vector<LibraryIndex::Book> items;
  std::vector<LibraryIndex::Book> books;
  views::library::Grid grid;
  views::library::List list;
  views::library::Thumb thumb;
  Sort sort = Sort::TitleAZ;
  View view = View::Grid;
  char filter = 0;
  int filterPage = 0;
  int filterIndex = 9;
  FilterTab filterTab = FilterTab::Title;
  std::string typeFilter;
  int typeFilterIndex = 0;
  bool sortOpen = false;
  bool filterOpen = false;
  bool refreshing = false;
  int popupBook = -1;
  bool folderDeleteConfirm = false;
  bool sidebarOpen = false;
  bool allBooksMode = false;
  StateFilter stateFilter = StateFilter::None;
  std::string authorFolder;
  std::string authorFolderKey;
  bool authorIndexAvailable = false;
  std::unordered_set<std::string> favorites;
  std::unordered_map<std::string, std::string> pluginGroupByPath_;
  std::unordered_map<std::string, int> pluginOrderByPath_;
  std::unordered_map<std::string, std::vector<LibraryIndex::Book>> pluginBooksByGroup_;
  std::unordered_map<std::string, std::string> pluginNameByGroup_;
  std::string activePluginGroup_;

  void load();
  void open(int index);
  void select(int index, bool longPress);
  void popup() const;
  bool popupInput();
  bool isFavorite(const LibraryIndex::Book& book) const;
  void markFavorite(const LibraryIndex::Book& book);
  void reset(const LibraryIndex::Book& book);
  void erase(const LibraryIndex::Book& book);
  void eraseFolder(const LibraryIndex::Book& folder);
  void sortDropdown() const;
  void filterPopup() const;
  void handleSortTap(int tapX, int tapY);
  void handleFilterTap(int tapX, int tapY);
  void applySort(int index);
  void applyFilter(int index);
  void applyTypeFilter(int index);
  void changeFilterPage(int delta);
  static const char* typeFilterLabel(int index);
  static const char* typeFilterCategory(int index);
  char letter(int index) const;
  int buttonX(int index) const;
  int buttonY() const;
  int sortCount() const;
  int sortIndex() const;
  const uint8_t* viewIcon() const;
  void resetViews();
  /** Re-applies the remembered thumb page for the current path. Must run after resetViews(), which
   *  zeroes it, and before thumb.load(), which paginates. */
  void restoreThumbPage();
  bool handleSidebarInput();
  bool handleSidebarTap();
  void drawSidebar() const;
  std::vector<std::string> pluginGroupCovers(const LibraryIndex::Book& group, int limit) const;
};
