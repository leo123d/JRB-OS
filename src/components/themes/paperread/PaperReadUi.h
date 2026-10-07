#pragma once

// PaperRead UI spec shared geometry and drawing primitives.
//
// Everything here is taken verbatim from F:\小纸pico\PAPERREAD-UI-SPEC.md and is
// only compiled for the readpico target. The panel is 684x1216 portrait; all
// coordinates are screen-absolute pixels measured from the top-left corner.
//
// Layout contract (spec S-1.9): the persistent bottom tab bar occupies
// y1112-1208 and is drawn by the shared Activity chrome, never by a page body.

#include <cstdint>

#include "GfxRenderer.h"

class PaperReadUi {
 public:
  // -- Screen / insets (spec R-1, R-2) --------------------------------------
  static constexpr int kScreenWidth = 684;
  static constexpr int kScreenHeight = 1216;
  static constexpr int kInsetTop = 5;
  static constexpr int kInsetRight = 5;
  static constexpr int kInsetBottom = 8;
  static constexpr int kInsetLeft = 5;

  // -- Shared page geometry -------------------------------------------------
  static constexpr int kSideMargin = 32;

  // Sub-page header (spec S-2/S-3/S-4): y5-101, h96, back glyph + 21px title.
  static constexpr int kHeaderTop = kInsetTop;  // y5
  static constexpr int kHeaderHeight = 96;      // -> y101
  static constexpr int kHeaderBackX = 26;       // back glyph left edge
  static constexpr int kHeaderBackSize = 26;
  static constexpr int kHeaderTitleX = 76;  // title left edge

  // Persistent bottom bar (spec S-1.9): y1112-1208.
  static constexpr int kTabBarTop = 1112;
  static constexpr int kTabBarHeight = 96;
  static constexpr int kTabBarBottom = kTabBarTop + kTabBarHeight;  // 1208

  // Body region between the header and the tab bar.
  static constexpr int kBodyTop = kHeaderTop + kHeaderHeight;  // 101
  static constexpr int kBodyBottom = kTabBarTop;               // 1112

  // -- Palettes (spec R-3: only four greys) ---------------------------------
  static constexpr Color kInk = Color::Black;
  static constexpr Color kLight = Color::LightGray;
  static constexpr Color kGray = Color::DarkGray;

  // -- Header ---------------------------------------------------------------
  // Draws the spec sub-page header: back glyph at x26, title at x76, and a 1px
  // ink rule along the bottom edge.
  static void drawHeader(const GfxRenderer& renderer, const char* title);
  // Header with a caller-supplied right-hand widget area (e.g. grid/list
  // toggles on the library page). `rightIcons` is drawn starting at x = width -
  // 32 walking leftwards; each icon is `iconSize` px with `gap` between them.
  static void drawHeaderWithIcons(const GfxRenderer& renderer, const char* title, const uint8_t* const* icons,
                                  int iconCount, int iconSize, int gap);

  // -- Section title (spec S-4: 16px grey, padding 18/32/10) ----------------
  static void drawSectionTitle(const GfxRenderer& renderer, int y, const char* title);

  // -- List rows ------------------------------------------------------------
  // One 96px recent-books row (spec S-2): title 19px + author 15px grey, with
  // an optional trailing status label. Draws a 1px light separator below.
  static void drawRecentRow(const GfxRenderer& renderer, int y, const char* title, const char* author,
                            const char* trailing);

  // One 78px settings row (spec S-4): label 19px, value 17px grey + chevron.
  static void drawSettingRow(const GfxRenderer& renderer, int y, const char* label, const char* value);

  // -- Chips ----------------------------------------------------------------
  // A single chip with a 1px ink outline. `active` inverts it (ink fill, white
  // text) per spec R-4. Returns the chip's width so callers can lay out a row.
  static int chipWidth(const GfxRenderer& renderer, const char* label);
  static void drawChip(const GfxRenderer& renderer, int x, int y, int height, const char* label, bool active);

  // -- Cover grid (spec S-3) -----------------------------------------------
  static constexpr int kGridColumns = 3;
  static constexpr int kGridCardWidth = 196;
  static constexpr int kGridCardHeight = 276;
  static constexpr int kGridColumnGap = 16;
  static constexpr int kGridRowGap = 38;
  static constexpr int kGridTop = 225;
  // Spec S-3: filter chips start y117 at x32; separator 1px --gray at y196.
  static constexpr int kFilterTop = 117;
  static constexpr int kFilterHeight = 44;
  static constexpr int kFilterGap = 12;
  static constexpr int kFilterSeparatorY = 196;
  // Spec S-3: pager rule y1044, label row centred on y1094.
  static constexpr int kPagerRuleY = 1044;
  static constexpr int kPagerMidY = 1094;
  // Cell origin of grid slot `slot` (row-major, 3 per row).
  static int gridCellX(int slot) { return kSideMargin + (slot % kGridColumns) * (kGridCardWidth + kGridColumnGap); }
  static int gridCellY(int slot) { return kGridTop + (slot / kGridColumns) * (kGridCardHeight + kGridRowGap); }

  // -- Utilities ------------------------------------------------------------
  // Spec R-8: px = pt * 4/3. Returns the pixel size for a pt figure.
  static constexpr int ptToPx(const int pt) { return pt * 4 / 3; }
  // Spec §7: line height in px for a given pt size and multiplier.
  static int lineHeightPx(int pt, int multTimesTen);
};
