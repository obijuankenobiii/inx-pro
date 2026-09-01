#pragma once

/**
 * @file UiLayout.h
 * @brief The one definition of the shared chrome geometry.
 *
 * Plain constants, no class and no dependencies, so ANY file can include it — including the
 * reader, where including Page.h is impossible because lib/Epub/Epub/Page.h declares a
 * different `class Page` and the two collide.
 *
 * Page re-exposes these as Page::LIST_ITEM_HEIGHT and friends, so page code can keep using
 * the shorter names. Both spellings resolve to the values here; there is no second copy.
 */

namespace UiLayout {

constexpr int LIST_ITEM_HEIGHT = 66;       ///< one row in any settings/menu list
constexpr int HEADER_HEIGHT = 66;          ///< in-page header band
constexpr int PAGE_HEADER_HEIGHT = 79;     ///< taller header used by drawer pages
constexpr int TAB_BAR_HEIGHT = 60;         ///< bottom navigation strip
constexpr int CONTENT_TOP = 36;            ///< first usable y below the status row
constexpr int CONTENT_BOTTOM_PADDING = 5;  ///< gap between content and the tab bar
constexpr int LIST_BOTTOM_PADDING = 12;

}
