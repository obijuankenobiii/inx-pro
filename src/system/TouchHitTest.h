#pragma once

/**
 * @file TouchHitTest.h
 * @brief Hit-testing for the uniform-vertical-list pattern shared by most
 * settings/menu/drawer screens (66 rows, one
 * scrollOffset, one itemsPerPage). Screens that use this pattern already
 * compute startY/itemsPerPage/scrollOffset for rendering — this just runs
 * the same arithmetic in reverse against a tap's Y position, so there's one
 * hit-test formula instead of one per screen.
 *
 * Screens with bespoke geometry (grids, covers, free-form layouts) need
 * their own hit-test when their layout is not a uniform vertical list.
 */

class TouchHitTest {
 public:
  /**
   * @param tapY Tap position in screen pixels (not normalized).
   * @param startY Y of the first visible row (top of the list content area).
   * @param itemHeight Row height (Page::LIST_ITEM_HEIGHT on screens using this pattern).
   * @param itemsPerPage Number of rows visible at once.
   * @param scrollOffset Index of the first visible item.
   * @param totalItems Total item count in the underlying list.
   * @return The tapped item's index, or -1 if the tap missed the list or landed out of bounds.
   */
  static int uniformListIndex(int tapY, int startY, int itemHeight, int itemsPerPage, int scrollOffset,
                              int totalItems) {
    if (itemHeight <= 0 || tapY < startY) {
      return -1;
    }
    const int row = (tapY - startY) / itemHeight;
    if (row < 0 || row >= itemsPerPage) {
      return -1;
    }
    const int index = row + scrollOffset;
    if (index < 0 || index >= totalItems) {
      return -1;
    }
    return index;
  }
};
