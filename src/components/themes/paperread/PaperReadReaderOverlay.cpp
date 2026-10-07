#include "PaperReadReaderOverlay.h"

#include <cstdio>
#include <cstring>

#include "I18n.h"
#include "PaperReadUi.h"
#include "components/icons/headerIcons.h"
#include "fontIds.h"

namespace paperread_overlay {
namespace {

// Spec §2.2 px -> closest available pt face (px = pt * 4/3).
constexpr int kFont21 = NOTOSERIF_16_FONT_ID;  // 21px
constexpr int kFont19 = NOTOSERIF_14_FONT_ID;  // 19px
constexpr int kFont17 = NOTOSERIF_12_FONT_ID;  // 17px
constexpr int kFont16 = NOTOSERIF_12_FONT_ID;  // 16px
constexpr int kFont15 = NOTOSERIF_12_FONT_ID;  // 15px

// O-1 geometry.
constexpr int kToolbarTopBand = PaperReadUi::kHeaderTop;          // y5
constexpr int kToolbarTopHeight = PaperReadUi::kHeaderHeight;     // 96 -> y101
constexpr int kToolbarBottomTop = 1115;                           // spec O-1
constexpr int kToolbarBottomBottom = PaperReadUi::kTabBarBottom;  // 1208
constexpr int kToolbarItems = 4;

// Panel header.
constexpr int kPanelTop = PaperReadUi::kHeaderTop;  // y5
constexpr int kPanelHeadHeight = 96;                // -> y101
constexpr int kPanelBodyTop = kPanelTop + kPanelHeadHeight;

// O-2 rows.
constexpr int kSearchTop = 117;
constexpr int kSearchHeight = 44;
constexpr int kSearchLeft = 32;
constexpr int kSearchRight = 652;
constexpr int kTocRowHeight = 88;

// O-4 layout.
constexpr int kMarksRowHeight = 96;
constexpr int kMarksBodyBottom = 1012;
constexpr int kMarksActionTop = 1120;

// O-5 geometry.
constexpr int kTextPanelTop = 560;
constexpr int kTextPanelBottom = PaperReadUi::kTabBarBottom;  // 1208
constexpr int kTextHeadHeight = 72;
constexpr int kTextRowHeight = 86;
constexpr int kTextLabelX = 32;
constexpr int kTextControlX = 140;

int textWidth(const GfxRenderer& r, const int font, const char* s) {
  return r.getTextWidth(font, s == nullptr ? "" : s);
}

// Spec O-1: 4px grid of 0.9px ink dots on the paper (about 12.5% coverage).
// Ink text keeps its pure black; only the paper base darkens.
void drawDitherMask(const GfxRenderer& renderer, const int top, const int bottom) {
  const int width = renderer.getScreenWidth();
  for (int y = top; y < bottom; y += 4) {
    for (int x = 2; x < width; x += 4) renderer.drawPixel(x, y, true);
  }
}

// A chip sized to its label; `active` inverts it (spec R-4). Returns width.
int drawChip(const GfxRenderer& r, const int x, const int y, const int height, const char* label, const bool active) {
  const int w = textWidth(r, kFont15, label) + 24;
  if (active) {
    r.fillRect(x, y, w, height, true);
  } else {
    r.drawRect(x, y, w, height, true);
  }
  const int tw = textWidth(r, kFont15, label);
  r.drawText(kFont15, x + (w - tw) / 2, y + (height - r.getLineHeight(kFont15)) / 2, label, !active);
  return w;
}

void drawPanelHeader(const GfxRenderer& r, const char* title) {
  const int width = r.getScreenWidth();
  r.fillRect(0, kPanelTop, width, kPanelHeadHeight, false);
  constexpr int kBackBox = 26;
  const int glyphTop = kPanelTop + (kPanelHeadHeight - kBackBox) / 2;
  r.drawIcon(icon_header_back_32_bits, PaperReadUi::kHeaderBackX, glyphTop, kBackBox);
  r.drawText(kFont21, PaperReadUi::kHeaderTitleX, kPanelTop + 34, title, true, EpdFontFamily::BOLD);
  r.drawLine(0, kPanelTop + kPanelHeadHeight - 1, width - 1, kPanelTop + kPanelHeadHeight - 1, true);
}

}  // namespace

void drawMask(const GfxRenderer& renderer, const int top, const int bottom) { drawDitherMask(renderer, top, bottom); }

void render(const GfxRenderer& r, const OverlayModel& m) {
  const int width = r.getScreenWidth();

  // ---- O-5: bottom sheet, everything above it is masked --------------------
  if (m.panel == Panel::Text) {
    drawDitherMask(r, PaperReadUi::kHeaderTop, kTextPanelTop - PaperReadUi::kHeaderTop);
    r.fillRect(0, kTextPanelTop, width, kTextPanelBottom - kTextPanelTop, false);
    r.drawLine(0, kTextPanelTop, width - 1, kTextPanelTop, true);

    r.drawText(kFont21, PaperReadUi::kSideMargin, kTextPanelTop + 20, tr(STR_OVERLAY_FONT), true, EpdFontFamily::BOLD);
    r.drawLine(0, kTextPanelTop + kTextHeadHeight - 1, width - 1, kTextPanelTop + kTextHeadHeight - 1, true);

    int y = kTextPanelTop + kTextHeadHeight;
    auto rowLabel = [&](const char* label) {
      r.drawText(kFont19, kTextLabelX, y + (kTextRowHeight - r.getLineHeight(kFont19)) / 2, label, true);
    };
    auto rowRule = [&]() { r.drawLine(0, y + kTextRowHeight - 1, width - 1, y + kTextRowHeight - 1, false); };

    // 字体
    rowLabel(tr(STR_FONT_FAMILY));
    {
      const int chipY = y + (kTextRowHeight - 40) / 2;
      int x = kTextControlX;
      x += drawChip(r, x, chipY, 40, "霞鹜文楷", m.fontIndex == 0) + 12;
      x += drawChip(r, x, chipY, 40, "MiSans", m.fontIndex == 1) + 12;
      x += drawChip(r, x, chipY, 40, "美黑", m.fontIndex == 2) + 12;
      drawChip(r, x, chipY, 40, "典黑", m.fontIndex == 3);
    }
    rowRule();
    y += kTextRowHeight;

    // 字号
    rowLabel(tr(STR_FONT_SIZE));
    {
      const int chipY = y + (kTextRowHeight - 40) / 2;
      int x = kTextControlX;
      x += drawChip(r, x, chipY, 40, "-", false) + 12;
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d pt", m.sizePt);
      const int tw = textWidth(r, kFont21, buf);
      r.drawText(kFont21, x + (150 - tw) / 2, y + (kTextRowHeight - r.getLineHeight(kFont21)) / 2, buf, true);
      x += 150 + 12;
      drawChip(r, x, chipY, 40, "+", false);
    }
    rowRule();
    y += kTextRowHeight;

    // 行距
    rowLabel(tr(STR_LINE_SPACING));
    {
      const int chipY = y + (kTextRowHeight - 40) / 2;
      static const char* kSpacing[] = {"1.4", "1.6", "1.8", "2.0"};
      int x = kTextControlX;
      for (int i = 0; i < 4; ++i) x += drawChip(r, x, chipY, 40, kSpacing[i], i == m.spacingIndex) + 12;
    }
    rowRule();
    y += kTextRowHeight;

    // 边距
    rowLabel(tr(STR_SCREEN_MARGIN));
    {
      const int chipY = y + (kTextRowHeight - 40) / 2;
      const char* kMargin[] = {tr(STR_MARGIN_NARROW), tr(STR_MARGIN_MEDIUM), tr(STR_WIDE)};
      int x = kTextControlX;
      for (int i = 0; i < 3; ++i) x += drawChip(r, x, chipY, 40, kMargin[i], i == m.marginIndex) + 12;
    }
    rowRule();
    y += kTextRowHeight;

    // 对齐
    rowLabel(tr(STR_ALIGNMENT));
    {
      const int chipY = y + (kTextRowHeight - 40) / 2;
      const char* kAlign[] = {tr(STR_JUSTIFY), tr(STR_ALIGN_LEFT), tr(STR_CENTER)};
      int x = kTextControlX;
      for (int i = 0; i < 3; ++i) x += drawChip(r, x, chipY, 40, kAlign[i], i == m.alignIndex) + 12;
    }

    r.drawText(kFont15, kTextLabelX, kTextPanelBottom - 34, tr(STR_FONT_NOTE), true);
    return;
  }

  // ---- O-2 / O-3 / O-4: full-screen white panels ---------------------------
  if (m.panel == Panel::Contents || m.panel == Panel::Progress || m.panel == Panel::Marks) {
    r.fillRect(0, 0, width, r.getScreenHeight(), false);
    const char* title = m.panel == Panel::Contents   ? tr(STR_OVERLAY_CONTENTS)
                        : m.panel == Panel::Progress ? tr(STR_OVERLAY_PROGRESS)
                                                     : tr(STR_OVERLAY_MARKS);
    drawPanelHeader(r, title);
    r.drawLine(0, kPanelTop, width - 1, kPanelTop, true);
    r.drawLine(0, kPanelBodyTop - 1, width - 1, kPanelBodyTop - 1, true);

    if (m.panel == Panel::Contents) {
      r.drawRect(kSearchLeft, kSearchTop, kSearchRight - kSearchLeft, kSearchHeight, true);
      r.drawText(kFont16, kSearchLeft + 16, kSearchTop + 14, tr(STR_SEARCH_CHAPTERS), true);
      int y = kSearchTop + kSearchHeight + 16;
      for (int i = 0; i < m.tocCount; ++i) {
        if (y + kTocRowHeight > PaperReadUi::kScreenHeight) break;
        const bool inv = i == m.tocSelected;
        if (inv) r.fillRect(0, y, width, kTocRowHeight, true);
        if (m.tocText) r.drawText(kFont19, PaperReadUi::kSideMargin, y + 22, m.tocText(m.tocCtx, i), !inv);
        if (m.tocPage) {
          const char* p = m.tocPage(m.tocCtx, i);
          const int pw = textWidth(r, kFont16, p);
          r.drawText(kFont16, width - PaperReadUi::kSideMargin - pw, y + 24, p, !inv);
        }
        if (i + 1 < m.tocCount)
          r.drawLine(PaperReadUi::kSideMargin, y + kTocRowHeight - 1, width - PaperReadUi::kSideMargin - 1,
                     y + kTocRowHeight - 1, false);
        y += kTocRowHeight;
      }
      return;
    }

    if (m.panel == Panel::Progress) {
      char big[8];
      std::snprintf(big, sizeof(big), "%d", m.percent);
      const int cy = kPanelBodyTop + 120;
      const int bw = textWidth(r, kFont21, big);
      r.drawText(kFont21, (width - bw - 24) / 2, cy, big, true, EpdFontFamily::BOLD);
      r.drawText(kFont21, (width + bw - 24) / 2, cy + 40, "%", true);
      char sub[96];
      std::snprintf(sub, sizeof(sub), "%s · %s", m.chapterLabel, m.pageInfo);
      const int sw = textWidth(r, kFont17, sub);
      r.drawText(kFont17, (width - sw) / 2, cy + 130, sub, true);
      const int barY = cy + 170;
      const int barLeft = 60;
      const int barRight = 624;
      r.fillRect(barLeft, barY, barRight - barLeft, 3, true);
      const int knobX = barLeft + (barRight - barLeft - 20) * m.percent / 100;
      r.fillRect(knobX, barY - 9, 20, 20, true);
      const int chipY = barY + 60;
      const int chipW = textWidth(r, kFont15, tr(STR_PREV_CHAPTER)) + 24;
      const int chipW2 = textWidth(r, kFont15, tr(STR_NEXT_CHAPTER)) + 24;
      int cx = (width - (chipW + 20 + chipW2)) / 2;
      cx += drawChip(r, cx, chipY, 44, tr(STR_PREV_CHAPTER), false) + 20;
      drawChip(r, cx, chipY, 44, tr(STR_NEXT_CHAPTER), false);
      const int inputY = chipY + 80;
      const int inputX = (width - 120) / 2 - 60;
      r.drawRect(inputX, inputY, 120, 40, true);
      char pageBuf[16];
      std::snprintf(pageBuf, sizeof(pageBuf), "%d", m.pageNumber);
      const int pbw = textWidth(r, kFont19, pageBuf);
      r.drawText(kFont19, inputX + (120 - pbw) / 2, inputY + 10, pageBuf, true);
      drawChip(r, inputX + 136, inputY, 40, tr(STR_JUMP), false);
      return;
    }

    // O-4 Marks.
    int y = PaperReadUi::kBodyTop;
    for (int i = 0; i < m.markCount; ++i) {
      if (y + kMarksRowHeight > kMarksBodyBottom) break;
      const OverlayModel::MarkRow* row = m.markAt ? m.markAt(m.markCtx, i) : nullptr;
      if (row == nullptr) continue;
      if (row->bookmark) {
        r.drawText(kFont17, 44, y + 20, row->label, true);
        if (row->detail) r.drawText(kFont15, 44, y + 48, row->detail, true);
      } else {
        r.drawText(kFont17, PaperReadUi::kSideMargin, y + 20, row->label, true);
        if (row->detail) r.drawText(kFont16, 96, y + 24, row->detail, true);
      }
      if (row->page) {
        const int pw = textWidth(r, kFont16, row->page);
        r.drawText(kFont16, width - PaperReadUi::kSideMargin - pw, y + 22, row->page, true);
      }
      r.drawLine(PaperReadUi::kSideMargin, y + kMarksRowHeight - 1, width - PaperReadUi::kSideMargin - 1,
                 y + kMarksRowHeight - 1, false);
      y += kMarksRowHeight;
    }
    if (m.markCount == 0) {
      const char* empty = tr(STR_NO_MARKS);
      const int ew = textWidth(r, kFont17, empty);
      r.drawText(kFont17, (width - ew) / 2, kPanelBodyTop + 120, empty, true);
    }
    char add[64];
    std::snprintf(add, sizeof(add), tr(STR_ADD_BOOKMARK_FMT), m.pageNumber);
    const int addW = drawChip(r, PaperReadUi::kSideMargin, kMarksActionTop, 48, add, false);
    drawChip(r, PaperReadUi::kSideMargin + addW + 14, kMarksActionTop, 48, tr(STR_EXPORT_NOTES), true);
    return;
  }

  // ---- O-1: toolbar (dithered mask + top and bottom white bands) -----------
  if (m.panel == Panel::Toolbar) {
    drawDitherMask(r, PaperReadUi::kBodyTop, kToolbarBottomTop - PaperReadUi::kBodyTop);

    r.fillRect(0, kToolbarTopBand, width, kToolbarTopHeight, false);
    constexpr int kBackBox = 26;
    r.drawIcon(icon_header_back_32_bits, PaperReadUi::kHeaderBackX,
               kToolbarTopBand + (kToolbarTopHeight - kBackBox) / 2, kBackBox);
    if (m.bookTitle) r.drawText(kFont19, PaperReadUi::kHeaderTitleX, kToolbarTopBand + 20, m.bookTitle, true);
    if (m.chapterTitle) r.drawText(kFont15, PaperReadUi::kHeaderTitleX, kToolbarTopBand + 54, m.chapterTitle, true);
    r.drawText(kFont21, width - PaperReadUi::kSideMargin - 20, kToolbarTopBand + 30, "...", true);
    r.drawLine(0, kToolbarTopBand + kToolbarTopHeight - 1, width - 1, kToolbarTopBand + kToolbarTopHeight - 1, true);

    r.fillRect(0, kToolbarBottomTop, width, kToolbarBottomBottom - kToolbarBottomTop, false);
    r.drawLine(0, kToolbarBottomTop, width - 1, kToolbarBottomTop, true);
    const char* kItems[] = {tr(STR_OVERLAY_CONTENTS), tr(STR_OVERLAY_PROGRESS), tr(STR_OVERLAY_MARKS),
                            tr(STR_OVERLAY_FONT)};
    const int cellWidth = width / kToolbarItems;
    const int itemHeight = kToolbarBottomBottom - kToolbarBottomTop;
    for (int i = 0; i < kToolbarItems; ++i) {
      const int left = i * cellWidth;
      const int right = i + 1 == kToolbarItems ? width : (i + 1) * cellWidth;
      const int tw = textWidth(r, kFont16, kItems[i]);
      r.drawText(kFont16, left + (right - left - tw) / 2, kToolbarBottomTop + itemHeight / 2 - 8, kItems[i], true);
      if (i > 0) r.drawLine(left, kToolbarBottomTop + 1, left, kToolbarBottomBottom - 1, false);
    }
    return;
  }
}

// --- hit testing -----------------------------------------------------------
ToolbarHit hitToolbar(const int x, const int y, const int screenWidth) {
  ToolbarHit hit{-1, false, false};
  if (y < kToolbarTopBand + kToolbarTopHeight) {
    hit.back = x < PaperReadUi::kHeaderTitleX;
    return hit;
  }
  if (y >= kToolbarBottomTop && y < kToolbarBottomBottom) {
    const int cellWidth = screenWidth / kToolbarItems;
    hit.topItem = x / cellWidth;
    if (hit.topItem >= kToolbarItems) hit.topItem = kToolbarItems - 1;
    return hit;
  }
  if (y >= PaperReadUi::kBodyTop && y < kToolbarBottomTop) hit.outside = true;
  return hit;
}

int hitContentsRow(const int x, const int y) {
  (void)x;
  const int start = kSearchTop + kSearchHeight + 16;
  if (y < start) return -1;
  return (y - start) / kTocRowHeight;
}

int hitMarksRow(const int x, const int y) {
  (void)x;
  if (y < PaperReadUi::kBodyTop || y >= kMarksBodyBottom) return -1;
  return (y - PaperReadUi::kBodyTop) / kMarksRowHeight;
}

int hitTextRow(const int x, const int y) {
  (void)x;
  if (y < kTextPanelTop + kTextHeadHeight) return -1;
  const int row = (y - (kTextPanelTop + kTextHeadHeight)) / kTextRowHeight;
  return (row >= 0 && row < 5) ? row : -1;
}

int hitTextChip(const int x, const int y, const int row) {
  if (row < 0 || row >= 5) return -1;
  if (y < kTextPanelTop + kTextHeadHeight) return -1;
  const int rowTop = kTextPanelTop + kTextHeadHeight + row * kTextRowHeight;
  if (y < rowTop || y >= rowTop + kTextRowHeight) return -1;
  if (x < kTextControlX) return -1;
  const int rel = (x - kTextControlX) / 72;
  if (row == 1) return rel;  // - / value / + occupy three buckets
  if (row == 0) return rel < 4 ? rel : 3;
  if (row == 2) return rel < 4 ? rel : 3;
  return rel < 3 ? rel : 2;
}

ProgressHit hitProgress(const int x, const int y, const int screenWidth) {
  (void)screenWidth;
  const int chipY = kPanelBodyTop + 120 + 170 + 60;
  if (y >= chipY && y < chipY + 44) return x < 342 ? ProgressHit::PrevChapter : ProgressHit::NextChapter;
  const int inputY = chipY + 80;
  if (y >= inputY && y < inputY + 40) {
    const int inputX = (PaperReadUi::kScreenWidth - 120) / 2 - 60;
    return x >= inputX && x < inputX + 136 + 60 ? ProgressHit::Jump : ProgressHit::None;
  }
  return ProgressHit::None;
}

MarksAction hitMarksAction(const int x, const int y, const int screenWidth) {
  (void)screenWidth;
  if (y < kMarksActionTop || y >= kMarksActionTop + 48) return MarksAction::None;
  return x < 320 ? MarksAction::AddBookmark : MarksAction::Export;
}

}  // namespace paperread_overlay
