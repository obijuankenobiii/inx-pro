#pragma once

#include <array>
#include <bitset>
#include <functional>
#include <map>
#include <string_view>
#include <unordered_map>
#include <string>
#include <unordered_set>
#include <vector>

#include "Epub/PsramAllocator.h"
#include "Page.h"
#include "system/PluginManager.h"
#include "util/LibraryIndex.h"
#include "util/MetadataIndex.h"
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
  enum class FilterCategory : uint8_t { Categories, Title, Type, Author, Series, Tags, Options };
  enum class StateFilter { None, Favorites, Reading, Finished, Metadata, Plugin };

  struct MetadataKeyHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
  };

  struct MetadataKeyEqual {
    using is_transparent = void;
    bool operator()(std::string_view left, std::string_view right) const noexcept { return left == right; }
  };

  using MetadataKeySet = std::unordered_set<EpubPsramString, MetadataKeyHash, MetadataKeyEqual,
                                            EpubPsramAllocator<EpubPsramString>>;
  using MetadataPathCounts = std::unordered_map<
      EpubPsramString, size_t, MetadataKeyHash, MetadataKeyEqual,
      EpubPsramAllocator<std::pair<const EpubPsramString, size_t>>>;

  struct MetadataFilterSelection {
    MetadataIndex::Kind kind;
    MetadataKeySet selectedKeys;
    MetadataPathCounts pathMatchCounts;
  };

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
  std::bitset<26> titleFilters_;
  std::bitset<4> typeFilters_;
  std::array<MetadataFilterSelection, 3> metadataFilters_{{
      {MetadataIndex::Kind::Authors, {}, {}},
      {MetadataIndex::Kind::Series, {}, {}},
      {MetadataIndex::Kind::Tags, {}, {}},
  }};
  FilterCategory filterCategory_ = FilterCategory::Categories;
  std::vector<MetadataIndex::Group> filterMetadataGroups_;
  bool filterMetadataIndexAvailable_ = false;
  int filterScrollOffset_ = 0;
  bool sortOpen = false;
  bool filterOpen = false;
  bool refreshing = false;
  int popupBook = -1;
  bool folderDeleteConfirm = false;
  bool sidebarOpen = false;
  bool allBooksMode = false;
  StateFilter stateFilter = StateFilter::None;
  MetadataIndex::Kind metadataKind_ = MetadataIndex::Kind::Authors;
  std::string metadataGroupKey_;
  bool metadataIndexAvailable_ = false;
  int sidebarScrollOffset_ = 0;
  std::unordered_set<std::string> favorites;
  std::unordered_map<std::string, std::string> pluginGroupByPath_;
  std::unordered_map<std::string, float> pluginOrderByPath_;
  std::unordered_map<std::string, std::vector<LibraryIndex::Book>> pluginBooksByGroup_;
  std::unordered_map<std::string, std::string> pluginNameByGroup_;
  std::unordered_map<std::string, std::string> metadataKeyByGroup_;
  std::string activePluginGroup_;

  void load();
  void open(int index);
  void select(int index, bool longPress);
  void popup() const;
  bool popupInput();
  bool isFavorite(const LibraryIndex::Book& book) const;
  void markFavorite(const LibraryIndex::Book& book);
  void markCompleted(const LibraryIndex::Book& book);
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
  void openFilterCategory(FilterCategory category);
  void applyMetadataFilter(int index);
  int filterRowCount() const;
  int selectedFilterCount(FilterCategory category) const;
  void resetFilterSelections();
  void clearFilters();
  static int metadataFilterIndex(FilterCategory category);
  static const char* filterCategoryLabel(FilterCategory category);
  static const char* typeFilterLabel(int index);
  static const char* typeFilterCategory(int index);
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
  std::vector<std::string> groupCovers(const LibraryIndex::Book& group, int limit) const;
};
