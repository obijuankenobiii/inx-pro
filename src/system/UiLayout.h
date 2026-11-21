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

constexpr int LIST_ITEM_HEIGHT = 66;
constexpr int HEADER_HEIGHT = 66;
constexpr int PAGE_HEADER_HEIGHT = 79;
constexpr int TAB_BAR_HEIGHT = 60;
constexpr int LIBRARY_BOTTOM_BAR_HEIGHT = 70;
constexpr int CONTENT_TOP = 36;
constexpr int CONTENT_BOTTOM_PADDING = 5;
constexpr int LIST_BOTTOM_PADDING = 12;

}
