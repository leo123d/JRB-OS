#pragma once

// PaperRead spec O-1..O-5: reader toolbar and its panels.
//
// Compiled only for readpico. The reader page (S-5) stays untouched; this
// module owns the overlay chrome drawn on top of it. Geometry is taken
// verbatim from F:\小纸pico\PAPERREAD-UI-SPEC.md.
//
//   O-1 Toolbar  : dithered mask y101-1115 + top band y5-101 + bottom band
//                  y1115-1208 with four items (目录/进度/标记/字体).
//   O-2 Contents : full-screen white panel, search box + chapter rows.
//   O-3 Progress : full-screen white panel, big percent + bar + chapter chips.
//   O-4 Marks    : full-screen white panel, bookmark/underline rows + actions.
//   O-5 Text     : bottom sheet y560-1208 with font/size/spacing/margin/align.

#include <cstdint>

#include "GfxRenderer.h"

namespace paperread_overlay {

// Which overlay is showing. None = the plain reading page (S-5.1).
enum class Panel : uint8_t { None, Toolbar, Contents, Progress, Marks, Text };

struct OverlayModel {
  Panel panel = Panel::None;
  const char* bookTitle = "";     // O-1 top band, line 1
  const char* chapterTitle = "";  // O-1 top band, line 2 / O-2 headings
  const char* pageInfo = "";      // e.g. "12 / 300"
  int pageNumber = 1;             // O-3 jump target / O-4 "页 N"
  int pageCount = 1;              // O-3 denominator
  int percent = 0;                // 0..100
  const char* chapterLabel = "";  // O-3 subtitle

  // O-2 contents rows.
  int tocCount = 0;
  const char* (*tocText)(void* ctx, int index) = nullptr;
  const char* (*tocPage)(void* ctx, int index) = nullptr;
  void* tocCtx = nullptr;
  int tocSelected = -1;

  // O-4 marks rows.
  struct MarkRow {
    const char* label;   // "书签" / "划线"
    const char* detail;  // chapter or excerpt
    const char* page;    // "页 N"
    bool bookmark;
  };
  int markCount = 0;
  const MarkRow* (*markAt)(void* ctx, int index) = nullptr;
  void* markCtx = nullptr;

  // O-5 text controls: current selections (0-based index into each chip row).
  int fontIndex = 0;
  int sizePt = 20;
  int spacingIndex = 1;  // 1.4 / 1.6 / 1.8 / 2.0
  int marginIndex = 1;   // 窄 / 中 / 宽
  int alignIndex = 0;    // 两端对齐 / 左对齐 / 居中
};

// Draw one overlay frame on top of the already-rendered page. The caller holds
// the render lock and has the page buffer available for masks.
void render(const GfxRenderer& renderer, const OverlayModel& model);

// Spec O-1: the dithered mask that darkens the paper without touching the ink.
// Exposed so the reader can pre-dither before drawing the bands.
void drawMask(const GfxRenderer& renderer, int top, int bottom);

// Hit-testing for touch, mirroring the spec's interaction table.
// Returns true when the point was consumed.
struct ToolbarHit {
  int topItem;   // -1 none, 0..3 = 目录/进度/标记/字体
  bool back;     // top-band back arrow
  bool outside;  // tap outside the bands (mask area) -> collapse
};
ToolbarHit hitToolbar(int x, int y, int screenWidth);

// O-2 / O-3 / O-4 / O-5 row hit-testing (returns row index or -1).
int hitContentsRow(int x, int y);
int hitMarksRow(int x, int y);
int hitTextRow(int x, int y);
int hitTextChip(int x, int y, int row);  // returns chip index within a row, or -1

// O-3 chapter-chip / jump hit-testing.
enum class ProgressHit { None, PrevChapter, NextChapter, Jump };
ProgressHit hitProgress(int x, int y, int screenWidth);

// O-4 bottom action chips.
enum class MarksAction { None, AddBookmark, Export };
MarksAction hitMarksAction(int x, int y, int screenWidth);

}  // namespace paperread_overlay
