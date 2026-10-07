#include "PaperReadUi.h"

#include <cmath>
#include <cstring>

#include "components/icons/headerIcons.h"
#include "fontIds.h"

namespace {
// Spec §2.2 px sizes mapped to the closest available pt face (px = pt * 4/3).
//   21px -> 16pt  19px -> 14pt  17px -> 13pt(->12)  16px -> 12pt  15px -> 12pt
constexpr int kFont21 = NOTOSERIF_16_FONT_ID;
constexpr int kFont19 = NOTOSERIF_14_FONT_ID;
constexpr int kFont16 = NOTOSERIF_12_FONT_ID;
constexpr int kFont15 = NOTOSERIF_12_FONT_ID;

// Spec S-4: section title padding 18/32/10 (top / left / bottom).
constexpr int kSectionTopPad = 18;
constexpr int kSectionBottomPad = 10;

constexpr int kBackGlyphBox = 26;
constexpr int kHeaderTitleBaselineOffset = 34;  // 16pt box inside the 96px band
}  // namespace

int PaperReadUi::lineHeightPx(const int pt, const int multTimesTen) {
  const double fs = static_cast<double>(pt) * 4.0 / 3.0;
  const double mult = static_cast<double>(multTimesTen) / 10.0;
  // Spec §7 calibration constant 1.172 (do not change).
  return static_cast<int>(std::lround(fs * mult * 1.172));
}

void PaperReadUi::drawHeader(const GfxRenderer& renderer, const char* title) {
  drawHeaderWithIcons(renderer, title, nullptr, 0, 0, 0);
}

void PaperReadUi::drawHeaderWithIcons(const GfxRenderer& renderer, const char* title, const uint8_t* const* icons,
                                      const int iconCount, const int iconSize, const int gap) {
  const int width = renderer.getScreenWidth();
  // Paper band behind the header, then the ink rule along its bottom edge.
  renderer.fillRect(0, kHeaderTop, width, kHeaderHeight, false);

  // Back glyph (arrow-left), optically centred in the 96px band.
  const int glyphTop = kHeaderTop + (kHeaderHeight - kBackGlyphBox) / 2;
  renderer.drawIcon(icon_header_back_32_bits, kHeaderBackX, glyphTop, kBackGlyphBox);

  // Optional right-hand icons, laid out leftwards from the right margin.
  if (icons != nullptr && iconCount > 0 && iconSize > 0) {
    int x = width - kSideMargin - iconSize;
    for (int i = iconCount - 1; i >= 0; --i) {
      const int iconTop = kHeaderTop + (kHeaderHeight - iconSize) / 2;
      renderer.drawIcon(icons[i], x, iconTop, iconSize);
      x -= iconSize + gap;
    }
  }

  // Title.
  if (title != nullptr && *title != '\0') {
    renderer.drawText(kFont21, kHeaderTitleX, kHeaderTop + kHeaderTitleBaselineOffset, title, true,
                      EpdFontFamily::BOLD);
  }

  renderer.drawLine(0, kHeaderTop + kHeaderHeight - 1, width - 1, kHeaderTop + kHeaderHeight - 1, true);
}

void PaperReadUi::drawSectionTitle(const GfxRenderer& renderer, const int y, const char* title) {
  if (title == nullptr || *title == '\0') return;
  renderer.drawText(kFont16, kSideMargin, y + kSectionTopPad, title, true);
}

int PaperReadUi::chipWidth(const GfxRenderer& renderer, const char* label) {
  constexpr int kChipPadX = 16;
  const int textWidth = renderer.getTextWidth(kFont15, label == nullptr ? "" : label);
  return textWidth + kChipPadX * 2;
}

void PaperReadUi::drawChip(const GfxRenderer& renderer, const int x, const int y, const int height,
                           const char* label, const bool active) {
  const int width = chipWidth(renderer, label);
  if (active) {
    renderer.fillRect(x, y, width, height, true);
  } else {
    renderer.drawRect(x, y, width, height, true);
  }
  const int textWidth = renderer.getTextWidth(kFont15, label == nullptr ? "" : label);
  const int textY = y + (height - renderer.getLineHeight(kFont15)) / 2;
  renderer.drawText(kFont15, x + (width - textWidth) / 2, textY, label, !active);
}

void PaperReadUi::drawRecentRow(const GfxRenderer& renderer, const int y, const char* title, const char* author,
                                const char* trailing) {
  constexpr int kRowHeight = 96;
  const int width = renderer.getScreenWidth();
  const int titleY = y + 16;
  const int authorY = titleY + 34;

  if (title != nullptr && *title != '\0') {
    renderer.drawText(kFont19, kSideMargin, titleY, title, true);
  }
  if (author != nullptr && *author != '\0') {
    renderer.drawText(kFont15, kSideMargin, authorY, author, true);
  }
  if (trailing != nullptr && *trailing != '\0') {
    const int textWidth = renderer.getTextWidth(kFont16, trailing);
    renderer.drawText(kFont16, width - kSideMargin - textWidth, titleY, trailing, true);
  }

  // 1px light separator between rows.
  renderer.drawLine(kSideMargin, y + kRowHeight - 1, width - kSideMargin - 1, y + kRowHeight - 1, false);
}

void PaperReadUi::drawSettingRow(const GfxRenderer& renderer, const int y, const char* label, const char* value) {
  constexpr int kRowHeight = 78;
  const int width = renderer.getScreenWidth();
  const int textY = y + (kRowHeight - renderer.getLineHeight(kFont19)) / 2;

  if (label != nullptr && *label != '\0') {
    renderer.drawText(kFont19, kSideMargin, textY, label, true);
  }
  if (value != nullptr && *value != '\0') {
    // Value + 「 ›」 chevron, right aligned.
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s ›", value);
    const int textWidth = renderer.getTextWidth(kFont16, buffer);
    renderer.drawText(kFont16, width - kSideMargin - textWidth, textY, buffer, true);
  }

  renderer.drawLine(kSideMargin, y + kRowHeight - 1, width - kSideMargin - 1, y + kRowHeight - 1, false);
}
