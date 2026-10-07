#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/inx/InxTheme.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"
#include "util/ReadingStatsAnalytics.h"

namespace {
struct HomeMenuEntry {
  HomeMenuItem item;
  StrId label;
  UIIcon icon;
};

constexpr HomeMenuEntry kDefaultMenuOrder[] = {
    {HomeMenuItem::LIBRARY, StrId::STR_LIBRARY, Library},
    {HomeMenuItem::RECENTS, StrId::STR_MENU_RECENT_BOOKS, Recent},
    {HomeMenuItem::SETTINGS_MENU, StrId::STR_SETTINGS_TITLE, Settings},
};
constexpr HomeMenuEntry kCarouselMenuOrder[] = {
    {HomeMenuItem::LIBRARY, StrId::STR_LIBRARY, Library},
    {HomeMenuItem::RECENTS, StrId::STR_MENU_RECENT_BOOKS, Recent},
    {HomeMenuItem::SETTINGS_MENU, StrId::STR_SETTINGS_TITLE, Settings},
};
constexpr int kHomeMenuItemCount = 3;

// PaperRead (decision 7): three entries only -- 最近阅读 / 书库 / 设置.
// The OPDS / apps / file-transfer slots are gone, so the old `hasOpds` index
// shifting is removed and both tables are a flat, identical three entries.
constexpr const HomeMenuEntry* menuEntryAtIndex(int index, bool carousel) {
  (void)carousel;
  if (index < 0 || index >= kHomeMenuItemCount) return nullptr;
  return &kDefaultMenuOrder[index];
}

constexpr HomeMenuItem indexToMenuItem(int index, bool carousel) {
  const HomeMenuEntry* entry = menuEntryAtIndex(index, carousel);
  return entry == nullptr ? HomeMenuItem::NONE : entry->item;
}

constexpr int menuItemToIndex(HomeMenuItem item, bool carousel) {
  for (int i = 0; i < kHomeMenuItemCount; ++i) {
    if (indexToMenuItem(i, carousel) == item) return i;
  }
  return 0;
}

static_assert(indexToMenuItem(0, true) == HomeMenuItem::LIBRARY);
static_assert(indexToMenuItem(1, true) == HomeMenuItem::RECENTS);
static_assert(indexToMenuItem(2, true) == HomeMenuItem::SETTINGS_MENU);
static_assert(menuItemToIndex(HomeMenuItem::SETTINGS_MENU, true) == 2);
static_assert(menuItemToIndex(HomeMenuItem::LIBRARY, false) == 0);
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Recents, File transfer, Settings, Apps
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (true) {  // PaperRead: library slot always present
    count++;
  }
  return count;
}

void HomeActivity::requestCarouselUpdate(CarouselUpdateScope scope) {
  if (scope == CarouselUpdateScope::Full) {
    carouselUpdateScope = scope;
  } else {
    // A queued full redraw must not be downgraded by a later menu event.
    CarouselUpdateScope expected = CarouselUpdateScope::None;
    carouselUpdateScope.compare_exchange_strong(expected, CarouselUpdateScope::MenuOnly);
  }
  requestUpdate();
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(coverGridUi ? maxBooks : std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::fillCoverGridFromLibrary() {
  if (recentBooks.size() >= CoverGridHomeUi::MAX_BOOKS) return;
  // Keep the index and record together off the task stack; reuse for every row.
  struct LibraryReader {
    library::LibraryIndexFile index;
    library::ClixRecord record;
  };
  auto reader = makeUniqueNoThrow<LibraryReader>();
  if (!reader) {
    LOG_ERR("HOME", "OOM: library index");
    return;
  }
  auto& index = reader->index;
  auto& record = reader->record;
  if (!index.open(library::libraryIndexPath())) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0) ||
        !index.open(library::libraryIndexPath())) {
      LOG_ERR("HOME", "Cannot populate cover grid from library");
      return;
    }
  }
  for (uint16_t row = 0; row < index.bookCount() && recentBooks.size() < CoverGridHomeUi::MAX_BOOKS; ++row) {
    RecentBook book;
    if (!index.readRecord(index.ordinalForRow(library::SortOrder::RecentDesc, row), record) ||
        !index.readPath(record, book.path))
      continue;
    if (std::any_of(recentBooks.begin(), recentBooks.end(),
                    [&](const RecentBook& existing) { return existing.path == book.path; }) ||
        RecentBooksStore::isMissing(book))
      continue;
    if (!index.readTitle(record, book.title) && !index.readName(record, book.title)) continue;
    index.readAuthor(record, book.author);
    if (index.ioFailed()) break;
    recentBooks.push_back(std::move(book));
  }
}

void HomeActivity::resolveGridCoverPaths() {
  for (auto& book : recentBooks) {
    if (!book.coverBmpPath.empty()) continue;
    // Constructors only derive cache paths; no metadata parsing or image generation.
    // Keep these large objects off the task stack and release each before the next book.
    if (FsHelpers::hasReflowableBookExtension(book.path)) {
      auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
      if (!epub) {
        LOG_ERR("HOME", "OOM: EPUB thumbnail path");
        continue;
      }
      book.coverBmpPath = epub->getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
      if (!xtc) {
        LOG_ERR("HOME", "OOM: XTC thumbnail path");
        continue;
      }
      book.coverBmpPath = xtc->getThumbBmpPath();
    }
  }
}

void HomeActivity::loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect) {
  if (!book.coverBmpPath.empty() && Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, height).c_str()))
    return;
  // Only one parser lives at a time; EPUB/XTC objects exceed the stack budget.
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: cover EPUB");
      return;
    }
    book.coverBmpPath = epub->getThumbBmpPath();
    if (Storage.exists(epub->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (epub->generateThumbBmpFromSource(height)) {
      return;
    }
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: cover XTC");
      return;
    }
    book.coverBmpPath = xtc->getThumbBmpPath();
    if (Storage.exists(xtc->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (xtc->load() && xtc->generateThumbBmp(height)) {
      return;
    }
  }
  book.coverBmpPath.clear();
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;
  const bool useFullCover =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // The cover grid shares one slot size; generating at any other height
    // would rescale the dithered thumb at draw time and alias badly.
    const int thumbHeight = coverGridUi ? coverGridUi->thumbHeightFor() : coverHeight;
    if (coverGridUi) {
      loadGridCover(book, thumbHeight, showingLoading, popupRect);
      ++progress;
      if (showingLoading) GUI.fillPopupProgress(renderer, popupRect, progress * 100 / recentBooks.size());
      continue;
    }
    const int currentProgress = progress++;
    const bool isEpub = FsHelpers::hasReflowableBookExtension(book.path);
    const bool isXtc = FsHelpers::hasXtcExtension(book.path);

    // Keep the persisted path theme-neutral; Carousel redirects only this activity's copy.
    if (isEpub) {
      const Epub epub(book.path, "/.crosspoint");
      if (useFullCover) {
        const std::string thumbPath = epub.getThumbBmpPath();
        if (book.coverBmpPath != thumbPath) {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, thumbPath);
        }
        book.coverBmpPath = epub.getCoverBmpPath();
      } else if (book.coverBmpPath.empty()) {
        book.coverBmpPath = epub.getThumbBmpPath();
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
      }
    } else if (isXtc) {
      const Xtc xtc(book.path, "/.crosspoint");
      if (useFullCover) {
        const std::string thumbPath = xtc.getThumbBmpPath();
        if (book.coverBmpPath != thumbPath) {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, thumbPath);
        }
        book.coverBmpPath = xtc.getCoverBmpPath();
      } else if (book.coverBmpPath.empty()) {
        book.coverBmpPath = xtc.getThumbBmpPath();
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
      }
    } else {
      continue;
    }
    if (book.coverBmpPath.empty()) continue;

    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (Storage.exists(coverPath.c_str())) {
      bool invalidCache = false;
      {
        HalFile cachedCover;
        if (!Storage.openFileForRead("HOME", coverPath, cachedCover)) {
          LOG_ERR("HOME", "Failed to open cached cover: %s", coverPath.c_str());
          continue;
        }
        if (cachedCover.fileSize() == 0) {
          // EPUB uses an empty thumbnail as a persistent "no supported cover" marker.
          if (isEpub && !useFullCover) continue;
          invalidCache = true;
        } else {
          Bitmap bitmap(cachedCover);
          invalidCache =
              bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0;
        }
      }
      if (!invalidCache) continue;
      LOG_ERR("HOME", "Removing invalid cached cover: %s", coverPath.c_str());
      if (!Storage.remove(coverPath.c_str())) {
        LOG_ERR("HOME", "Failed to remove invalid cached cover: %s", coverPath.c_str());
        continue;
      }
    }

    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }
    GUI.fillPopupProgress(renderer, popupRect, 10 + currentProgress * (90 / recentBooks.size()));
    std::string generatedPath;
    if (isEpub) {
      {
        GfxRenderer::FrameBufferLoan loan(renderer);
        generatedPath = useFullCover ? BookCoverLoader::ensureFullCover(book.path)
                                     : BookCoverLoader::ensureThumbnail(book.path, coverHeight);
      }
      // Inflate used the old framebuffer bytes. Rebuild a complete, known
      // loading frame before the next progress refresh can reach the panel.
      renderer.clearScreen();
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 10 + currentProgress * (90 / recentBooks.size()));
    } else {
      generatedPath = useFullCover ? BookCoverLoader::ensureFullCover(book.path)
                                   : BookCoverLoader::ensureThumbnail(book.path, coverHeight);
    }
    if (generatedPath.empty() && isXtc) LOG_ERR("HOME", "Failed to generate XTC cover: %s", book.path.c_str());
  }

  recentsLoaded = true;
  recentsLoading = false;
  coverRendered = false;
  coverBufferStored = false;
  requestCarouselUpdate(CarouselUpdateScope::Full);
}

void HomeActivity::onEnter() {
  Activity::onEnter();


  const auto& metrics = UITheme::getInstance().getMetrics();
  if (UITheme::getInstance().hasCoverGridHome()) {
    // Screen-lifetime interaction tables and component properties exceed the stack budget.
    coverGridUi = makeUniqueNoThrow<CoverGridHomeUi>(renderer);
    if (!coverGridUi) LOG_ERR("HOME", "OOM: cover grid UI; using standard home");
  }
  loadRecentBooks(coverGridUi ? CoverGridHomeUi::MAX_BOOKS : metrics.homeRecentBooksCount);
  hasContinueReading = !recentBooks.empty();
  if (coverGridUi) {
    fillCoverGridFromLibrary();
    resolveGridCoverPaths();
    coverGridUi->begin(recentBooks, true, hasContinueReading);
  }

  const auto base = static_cast<int>(recentBooks.size());
  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  selectorIndex =
      initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, isCarousel);
  lastCarouselBookIndex = 0;

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  coverGridUi.reset();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  if (coverBufferUnavailable) return false;

  // Thumbnail generation may borrow the framebuffer; cache only the final render.
  if (!recentsLoaded) return false;

  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;

  if (!coverBuffer || coverBufferSize < needed) {
    // The carousel region is up to ~44 KB, too large for the task stack. Allocate
    // once and reuse it for every selection during this HomeActivity lifetime.
    auto replacement = makeUniqueNoThrow<uint8_t[]>(needed);
    if (!replacement) {
      LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
      // ponytail: the theme/region is fixed for this Activity lifetime; retry
      // only after re-entering Home, when heap fragmentation may have changed.
      coverBufferUnavailable = true;
      return false;
    }
    coverBuffer = std::move(replacement);
    coverBufferSize = needed;
  }

  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer.get(),
                                   coverBufferSize)) {
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer.get(),
                                     coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  coverBuffer.reset();
  coverBufferSize = 0;
  coverBufferStored = false;
  coverBufferUnavailable = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bookCount = static_cast<int>(recentBooks.size());
  const int renderedMenuCount = menuCount - (metrics.homeContinueReadingInMenu ? 0 : bookCount);
  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  auto activateSelection = [this, isCarousel] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, isCarousel)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::LIBRARY:
        onLibraryOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      default:
        break;
    }
  };

  if (isCarousel) {
    const bool coversFocused = selectorIndex < bookCount;
    const int rowIndex = coversFocused ? selectorIndex : selectorIndex - bookCount;

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (coversFocused) {
        selectorIndex = ButtonNavigator::nextIndex(rowIndex, bookCount);
        lastCarouselBookIndex = selectorIndex;
      } else {
        selectorIndex = bookCount + ButtonNavigator::nextIndex(rowIndex, renderedMenuCount);
      }
      requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (coversFocused) {
        selectorIndex = ButtonNavigator::previousIndex(rowIndex, bookCount);
        lastCarouselBookIndex = selectorIndex;
      } else {
        selectorIndex = bookCount + ButtonNavigator::previousIndex(rowIndex, renderedMenuCount);
      }
      requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (bookCount > 0) {
        if (coversFocused) {
          lastCarouselBookIndex = selectorIndex;
          selectorIndex = bookCount;
        } else {
          selectorIndex = std::clamp(lastCarouselBookIndex, 0, bookCount - 1);
        }
        requestCarouselUpdate(CarouselUpdateScope::Full);
      }
      return;
    }
  } else if (!coverGridUi) {
    buttonNavigator.onNext([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

  if (coverGridUi) {
    const int touched = coverGridUi->selectedAction(mappedInput);
    if (touched >= 0 && touched < menuCount) {
      selectorIndex = touched;
      activateSelection();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelection();
      return;
    }
    // Side page buttons walk the covers, front Left/Right walk the tabs
    // (selectorIndex is flat: books first, then the tab items). A press while
    // selection sits in the other band jumps into this band first.
    const int coverCount = static_cast<int>(recentBooks.size());
    const auto cycleBand = [this](const int base, const int count, const int dir) {
      if (count <= 0) return;
      int idx = selectorIndex - base;
      if (idx < 0 || idx >= count) {
        idx = dir > 0 ? 0 : count - 1;
      } else {
        idx = (idx + count + dir) % count;
      }
      selectorIndex = base + idx;
      requestUpdate();
    };
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up},
                                         [&cycleBand, coverCount] { cycleBand(0, coverCount, -1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                         [&cycleBand, coverCount] { cycleBand(0, coverCount, +1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [&cycleBand, coverCount, menuCount] {
      cycleBand(coverCount, menuCount - coverCount, -1);
    });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [&cycleBand, coverCount, menuCount] {
      cycleBand(coverCount, menuCount - coverCount, +1);
    });
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (isCarousel) {
    const bool coversFocused = selectorIndex < bookCount;
    const int rowIndex = coversFocused ? selectorIndex : selectorIndex - bookCount;
    switch (swipe) {
      case MappedInputManager::SwipeDir::Left:
        if (coversFocused) {
          selectorIndex = ButtonNavigator::nextIndex(rowIndex, bookCount);
          lastCarouselBookIndex = selectorIndex;
        } else {
          selectorIndex = bookCount + ButtonNavigator::nextIndex(rowIndex, renderedMenuCount);
        }
        requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
        return;
      case MappedInputManager::SwipeDir::Right:
        if (coversFocused) {
          selectorIndex = ButtonNavigator::previousIndex(rowIndex, bookCount);
          lastCarouselBookIndex = selectorIndex;
        } else {
          selectorIndex = bookCount + ButtonNavigator::previousIndex(rowIndex, renderedMenuCount);
        }
        requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
        return;
      case MappedInputManager::SwipeDir::Up:
      case MappedInputManager::SwipeDir::Down:
        if (bookCount > 0) {
          if (coversFocused) {
            lastCarouselBookIndex = selectorIndex;
            selectorIndex = bookCount;
          } else {
            selectorIndex = std::clamp(lastCarouselBookIndex, 0, bookCount - 1);
          }
          requestCarouselUpdate(CarouselUpdateScope::Full);
        }
        return;
      case MappedInputManager::SwipeDir::None:
        break;
    }
  } else {
    if (swipe == MappedInputManager::SwipeDir::Up) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
      return;
    }
  }

  int tx = 0;
  int ty = 0;
  if (!recentBooks.empty() && mappedInput.wasScreenTouchDown(tx, ty) && tx >= 0 && tx < renderer.getScreenWidth() &&
      ty >= metrics.homeTopPadding && ty < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    int touchedBook = 0;
    if (isCarousel) {
      const int centerBook =
          selectorIndex < bookCount ? selectorIndex : std::clamp(lastCarouselBookIndex, 0, bookCount - 1);
      if (tx < renderer.getScreenWidth() / 3) {
        touchedBook = ButtonNavigator::previousIndex(centerBook, bookCount);
      } else if (tx >= renderer.getScreenWidth() * 2 / 3) {
        touchedBook = ButtonNavigator::nextIndex(centerBook, bookCount);
      } else {
        touchedBook = centerBook;
      }
      lastCarouselBookIndex = touchedBook;
    } else {
      // Multi-cover themes (Lyra3Covers and the like) render several recent
      // books side by side: map the finger to the cover it is on instead of
      // always settling on the first book.
      touchedBook = GUI.recentBookIndexAt(tx, renderer.getScreenWidth());
      touchedBook = std::clamp(touchedBook, 0, static_cast<int>(recentBooks.size()) - 1);
    }
    if (selectorIndex != touchedBook) {
      selectorIndex = touchedBook;
      requestCarouselUpdate(CarouselUpdateScope::Full);
    }
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!recentBooks.empty() && mappedInput.wasScreenTapped(tapX, tapY) && tapX >= 0 &&
      tapX < renderer.getScreenWidth() && tapY >= metrics.homeTopPadding &&
      tapY < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    if (!isCarousel) {
      selectorIndex = GUI.recentBookIndexAt(tapX, renderer.getScreenWidth());
      selectorIndex = std::clamp(selectorIndex, 0, static_cast<int>(recentBooks.size()) - 1);
    }
    activateSelection();
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  int menuRow = -1;
  MappedInputManager::RowTouch menuTouch;
  if (isCarousel) {
    const int menuBottom = renderer.getScreenHeight();
    const int columnWidth = renderer.getScreenWidth() / renderedMenuCount;
    menuTouch = mappedInput.colTouch(menuRow, 0, columnWidth, renderedMenuCount, menuBottom - metrics.menuRowHeight,
                                     menuBottom, columnWidth);
  } else {
    menuTouch = mappedInput.rowTouch(menuRow, menuTop, metrics.menuRowHeight + metrics.menuSpacing, renderedMenuCount,
                                     0, INT32_MAX, metrics.menuRowHeight);
  }
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        if (isCarousel) {
          requestCarouselUpdate(CarouselUpdateScope::MenuOnly);
        } else {
          requestUpdate();
        }
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

#if FREEINK_DEVICE_READPICO
// ---------------------------------------------------------------------------
// PaperRead home (UI spec S-1). The layout is fixed-geometry: every offset
// below is taken verbatim from the spec table so the panel matches the
// simulator. The tab bar is drawn separately by the shared Activity chrome.
// ---------------------------------------------------------------------------
namespace paperread_home {
// Spec S-1 constants (px, screen-absolute; panel is 684x1216 portrait).
constexpr int kSideMargin = 32;         // S-1.1 / S-1.3 / S-1.4
constexpr int kMastheadTop = 5;
constexpr int kMastheadHeight = 64;     // y5-69
constexpr int kHeroTop = 97;
constexpr int kHeroCoverWidth = 200;
constexpr int kHeroCoverHeight = 283;
constexpr int kHeroGap = 32;
constexpr int kButtonTop = 430;
constexpr int kButtonHeight = 96;       // y430-526
constexpr int kDividerY = 566;
constexpr int kRecentLabelY = 590;
constexpr int kCardRowY = 630;
constexpr int kCardCoverWidth = 150;
constexpr int kCardCoverHeight = 212;
constexpr int kStatsDividerY = 924;
constexpr int kStatLine1Y = 966;
constexpr int kStatLine2Y = 998;
constexpr int kTabBarTop = 1112;        // reserved; chrome draws into it

// Home body palette: only ink + the 4-step grey (spec R-3).
constexpr freeink::ui::Color kInk = freeink::ui::Color::Black;
constexpr freeink::ui::Color kGray = freeink::ui::Color::DarkGray;
}  // namespace paperread_home
#endif  // FREEINK_DEVICE_READPICO

void HomeActivity::render(RenderLock&&) {
  static_assert(canRenderCarouselMenuOnly(true, true, CarouselUpdateScope::MenuOnly));
  static_assert(!canRenderCarouselMenuOnly(false, true, CarouselUpdateScope::MenuOnly));
  static_assert(!canRenderCarouselMenuOnly(true, false, CarouselUpdateScope::MenuOnly));
  static_assert(!canRenderCarouselMenuOnly(true, true, CarouselUpdateScope::Full));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

#if FREEINK_DEVICE_READPICO
  // PaperRead home (spec S-1) replaces the generic carousel/list home entirely.
  // The stack of covers still needs the shared loader, so keep recents warm.
  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    const int themeThumbHeight = GUI.homeCoverThumbHeight(renderer);
    loadRecentCovers(themeThumbHeight > 0 ? themeThumbHeight : std::max(paperread_home::kHeroCoverHeight,
                                                                        paperread_home::kCardCoverHeight));
  }
  renderer.clearScreen();
  renderPaperReadHome();
  // Spec S-1.9: the persistent bottom bar is the home page's only chrome.
  if (usesMainTabBar()) {
    const MainTabLayout tabLayout = mainTabLayout();
    GUI.drawMainTabBar(renderer, tabLayout.tabBar, MainTab::Home);
    if (tabLayout.statusBar.height > 0) GUI.drawMainTabStatusBar(renderer, tabLayout.statusBar);
  }
  renderer.displayBuffer();
  return;
#endif

  const int homeMenuItemCount = kHomeMenuItemCount;
  const bool showContinueReading = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  std::vector<const char*> menuItems;
  std::vector<UIIcon> menuIcons;
  menuItems.reserve(homeMenuItemCount + (showContinueReading ? 1 : 0));
  menuIcons.reserve(homeMenuItemCount + (showContinueReading ? 1 : 0));
  for (int i = 0; i < homeMenuItemCount; ++i) {
    const HomeMenuEntry* entry = menuEntryAtIndex(i, isCarousel);
    menuItems.push_back(I18N.get(entry->label));
    menuIcons.push_back(entry->icon);
  }

  if (showContinueReading) {
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  const Rect headerRect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding};
  const Rect menuRect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
                      pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                                    metrics.homeMenuTopOffset + metrics.buttonHintsHeight)};
  auto drawHeader = [&] {
    GUI.drawHeader(renderer, headerRect,
                   metrics.homeShowRecentBookTitle && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);
  };
  auto drawMenu = [&] {
    GUI.drawHomeMenu(
        renderer, menuRect, static_cast<int>(menuItems.size()),
        metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
        [&menuItems](int index) { return std::string(menuItems[index]); },
        [&menuIcons](int index) { return menuIcons[index]; });
  };

  const CarouselUpdateScope updateScope = carouselUpdateScope.exchange(CarouselUpdateScope::None);
  const bool menuOnlyUpdate = canRenderCarouselMenuOnly(isCarousel, recentsLoaded, updateScope);
  if (menuOnlyUpdate) {
    renderer.fillRect(headerRect.x, headerRect.y, headerRect.width, headerRect.height, false);
    drawHeader();
    renderer.fillRect(0, pageHeight - metrics.menuRowHeight, pageWidth, metrics.menuRowHeight, false);
    drawMenu();
    renderer.displayBuffer();
    return;
  }

  if (isCarousel) {
    coverRendered = false;
    coverBufferStored = false;
  }

  renderer.clearScreen();
  if (coverGridUi) {
    coverGridUi->setSelection(selectorIndex);
    UITheme::getInstance().drawCoverGridHome(*coverGridUi);
    // Front Left/Right walk the tabs, so their hints read Left/Right; the
    // side page buttons (unhinted) walk the covers.
    const auto labels = mappedInput.mapLabels(hasContinueReading ? tr(STR_RESUME) : "", tr(STR_SELECT),
                                              tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(!firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
    // Slot heights are recorded during the draw above; a change (first layout
    // pass, orientation switch) means the paths must point at those sizes and
    // any missing thumbs must be generated. Refreshing the paths right away
    // lets the next pass draw already-cached thumbs before generation runs.
    const bool coverSpecChanged = coverGridUi->takeThumbHeightChanged();
    if (coverSpecChanged) {
      coverGridUi->refreshCoverPaths();
      recentsLoaded = false;
    }
    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    } else if (!recentsLoaded && !recentsLoading) {
      loadRecentCovers(CoverGridHomeUi::THUMB_HEIGHT);
      coverGridUi->refreshCoverPaths();
      requestUpdate();
    }
    return;
  }
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  if (isCarousel) {
    drawHeader();
  } else {
    // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
    // homeTopPadding, so the height must shrink by topPadding or the band (and a
    // centered title, e.g. RoundedRaff's book title) sinks into the tile.
    // Home is the stack root: no back button in its header.
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                   metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr,
                   nullptr, false);
  }

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  drawMenu();

  if (!isCarousel) {
    const auto labels = mappedInput.mapLabels(SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "",
                                              tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    const int themeThumbHeight = GUI.homeCoverThumbHeight(renderer);
    loadRecentCovers(themeThumbHeight > 0 ? themeThumbHeight : metrics.homeCoverHeight);
  }
}

#if FREEINK_DEVICE_READPICO
void HomeActivity::renderPaperReadHome() {
  using namespace paperread_home;
  const int pageWidth = renderer.getScreenWidth();
  const int contentWidth = pageWidth - kSideMargin * 2;

  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  (void)top;
  (void)right;
  (void)bottom;
  (void)left;

  // -- S-1.1 masthead: 「小纸」21px + 「· 纯阅读」15px gray + battery, 1px gray rule
  // px->pt is /(4/3): 21px ~ 16pt, 15px ~ 12pt (spec R-8).
  const int brandFont = NOTOSERIF_16_FONT_ID;
  const int brandSubFont = NOTOSERIF_12_FONT_ID;
  const int brandBaseline = kMastheadTop + 20;
  renderer.drawText(brandFont, kSideMargin, brandBaseline, "小纸", true);
  const int brandWidth = renderer.getTextWidth(brandFont, "小纸");
  renderer.drawText(brandSubFont, kSideMargin + brandWidth + 8, brandBaseline + 5, "· 纯阅读");
  GUI.drawBatteryRight(
      renderer,
      Rect{pageWidth - kSideMargin - InxMetrics::values.batteryWidth, brandBaseline,
           InxMetrics::values.batteryWidth, InxMetrics::values.batteryHeight},
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS);
  renderer.drawLine(kSideMargin, kMastheadTop + kMastheadHeight, pageWidth - kSideMargin - 1,
                    kMastheadTop + kMastheadHeight, false);

  // -- S-1.2 hero book: 200x283 cover + metadata column (title 26px -> 18pt) --
  const bool hasHero = !recentBooks.empty();
  const RecentBook* hero = hasHero ? &recentBooks[0] : nullptr;
  const ReadingBookStats* heroStats =
      hasHero ? READING_STATS.findMatchingBookForPath(hero->path, hero->title, hero->author) : nullptr;
  const int heroPct = heroStats ? heroStats->lastProgressPercent : 0;
  const char* heroChapter = heroStats && !heroStats->chapterTitle.empty() ? heroStats->chapterTitle.c_str() : nullptr;

  const Rect heroCover{kSideMargin, kHeroTop, kHeroCoverWidth, kHeroCoverHeight};
  if (hero) drawPaperReadCover(*hero, heroCover, 20);

  const int textX = kSideMargin + kHeroCoverWidth + kHeroGap;
  const int textWidth = pageWidth - kSideMargin - textX;
  const int heroTextY = kHeroTop + 10;  // right column padding-top 10
  if (hero) {
    const std::string title = renderer.truncatedText(NOTOSERIF_18_FONT_ID, hero->title.c_str(), textWidth,
                                                     EpdFontFamily::BOLD);
    renderer.drawText(NOTOSERIF_18_FONT_ID, textX, heroTextY, title.c_str(), true, EpdFontFamily::BOLD);
    if (!hero->author.empty()) {
      // mt12 (12px ~ 9pt) below the title.
      const std::string author = renderer.truncatedText(NOTOSERIF_12_FONT_ID, hero->author.c_str(), textWidth);
      renderer.drawText(NOTOSERIF_12_FONT_ID, textX, heroTextY + 36, author.c_str());
    }
    if (heroChapter) {
      // mt44 from the author line: chapter sits lower in the column.
      const std::string chap = renderer.truncatedText(NOTOSERIF_12_FONT_ID, heroChapter, textWidth);
      renderer.drawText(NOTOSERIF_12_FONT_ID, textX, heroTextY + 92, chap.c_str());
    }
    // Dots (10) + 「N%」, mt16 below the chapter line.
    const int dotsY = heroTextY + 124;
    const int dotsWidth = paperreadDots(renderer, textX, dotsY, heroPct, 10);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", heroPct);
    renderer.drawText(SMALL_FONT_ID, textX + dotsWidth + 12, dotsY - 4, pct);
  }

  // -- S-1.3 「继　续　阅　读」 inverse button (24px -> 18pt, tracking) --------
  const int buttonY = kButtonTop;
  if (hasHero) {
    renderer.fillRect(kSideMargin, buttonY, contentWidth, kButtonHeight, true);
    // Spec letter-spacing 6px is realised with full-width spaces between glyphs.
    const char* label = "继　续　阅　读";
    const int labelWidth = renderer.getTextWidth(NOTOSERIF_18_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(NOTOSERIF_18_FONT_ID, kSideMargin + (contentWidth - labelWidth) / 2,
                      buttonY + (kButtonHeight - renderer.getLineHeight(NOTOSERIF_18_FONT_ID)) / 2, label, false,
                      EpdFontFamily::BOLD);
  }
  heroButtonRect = hasHero ? Rect{kSideMargin, buttonY, contentWidth, kButtonHeight} : Rect{};

  // -- S-1.4 divider --------------------------------------------------------
  renderer.drawLine(kSideMargin, kDividerY, pageWidth - kSideMargin - 1, kDividerY, false);

  // -- S-1.5 「最近翻过」 three cards ---------------------------------------
  renderer.drawText(NOTOSERIF_12_FONT_ID, kSideMargin, kRecentLabelY, tr(STR_TAB_RECENT));
  const int cardCount = std::min<int>(3, static_cast<int>(recentBooks.size()));
  cardRects.clear();
  cardRects.reserve(cardCount);
  if (cardCount > 0) {
    const int cardsTotal = kCardCoverWidth * 3;
    const int gap = std::max(0, (contentWidth - cardsTotal) / 2);
    for (int i = 0; i < cardCount; ++i) {
      const RecentBook& book = recentBooks[i];
      const int cardX = kSideMargin + i * (kCardCoverWidth + gap);
      const Rect cover{cardX, kCardRowY, kCardCoverWidth, kCardCoverHeight};
      drawPaperReadCover(book, cover, 16);
      cardRects.push_back(cover);
      const ReadingBookStats* stats = READING_STATS.findMatchingBookForPath(book.path, book.title, book.author);
      const std::string cardTitle =
          renderer.truncatedText(NOTOSERIF_12_FONT_ID, book.title.c_str(), kCardCoverWidth + 10);
      renderer.drawText(SMALL_FONT_ID, cardX, kCardRowY + kCardCoverHeight + 10, cardTitle.c_str());
      if (stats && stats->completed) {
        renderer.drawText(SMALL_FONT_ID, cardX, kCardRowY + kCardCoverHeight + 36, tr(STR_BOOKS_FINISHED));
      } else {
        paperreadDots(renderer, cardX, kCardRowY + kCardCoverHeight + 44,
                      stats ? stats->lastProgressPercent : 0, 8);
      }
    }
  }

  // -- S-1.6 stats (two 16px gray lines under a 1px rule) --------------------
  renderer.drawLine(kSideMargin, kStatsDividerY, pageWidth - kSideMargin - 1, kStatsDividerY, false);
  const uint64_t totalMs = READING_STATS.getTotalReadingMs();
  const uint64_t totalMinutes = totalMs / 60000ULL;
  char line1[64];
  snprintf(line1, sizeof(line1), tr(STR_STATS_DURATION_HM_FMT), static_cast<unsigned>(totalMinutes / 60ULL),
           static_cast<unsigned>(totalMinutes % 60ULL));
  renderer.drawText(NOTOSERIF_12_FONT_ID, kSideMargin, kStatLine1Y, line1);

  // Average session = total reading time / total recorded sessions.
  uint64_t totalSessions = 0;
  for (const ReadingBookStats& b : READING_STATS.getBooks()) totalSessions += b.sessions;
  const unsigned avgMinutes =
      totalSessions > 0 ? static_cast<unsigned>(totalMinutes / totalSessions) : 0;
  char seg[48];
  char line2[160];
  line2[0] = '\0';
  snprintf(seg, sizeof(seg), tr(STR_STATS_READ_FMT), static_cast<unsigned>(READING_STATS.getBooksStartedCount()));
  snprintf(line2, sizeof(line2), "%s", seg);
  snprintf(seg, sizeof(seg), tr(STR_STATS_FINISHED_FMT),
           static_cast<unsigned>(READING_STATS.getBooksFinishedCount()));
  snprintf(line2 + strlen(line2), sizeof(line2) - strlen(line2), " · %s", seg);
  snprintf(seg, sizeof(seg), tr(STR_STATS_AVG_SESSION_FMT), avgMinutes);
  snprintf(line2 + strlen(line2), sizeof(line2) - strlen(line2), " · %s", seg);
  renderer.drawText(NOTOSERIF_12_FONT_ID, kSideMargin, kStatLine2Y, line2);
}

int HomeActivity::paperreadDots(const GfxRenderer& r, const int x, const int y, const int percent, const int count) {
  constexpr int kDot = 6;
  constexpr int kGap = 5;
  const int filled = std::clamp(percent * count / 100, 0, count);
  for (int i = 0; i < count; ++i) {
    const int cx = x + i * (kDot + kGap);
    if (i < filled)
      r.fillRect(cx, y, kDot, kDot, true);
    else
      r.drawRect(cx, y, kDot, kDot, true);
  }
  return count * (kDot + kGap) - kGap;
}

void HomeActivity::drawPaperReadCover(const RecentBook& book, const Rect& rect, int fontSize) {
  // Deterministic generated cover (spec §4): 1px frame, 1px inner liner inset
  // 5px, dither base, vertical title, publisher line. A real thumbnail, when
  // present and cached, is drawn crop-filled instead.
  std::string coverPath;
  if (!book.coverBmpPath.empty()) {
    const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, rect.height);
    if (Storage.exists(thumb.c_str())) coverPath = thumb;
  }
  if (!coverPath.empty()) {
    HalFile file;
    if (Storage.openFileForRead("HOME", coverPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && renderer.drawBitmapCropToFill(bitmap, rect.x, rect.y,
                                                                                       rect.width, rect.height)) {
        renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
        return;
      }
    }
  }
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
  renderer.fillRectDither(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height, true);
  renderer.drawRect(rect.x + 5, rect.y + 5, rect.width - 10, rect.height - 10, true);
  // Vertical title, centered.
  const std::string title = book.title.empty() ? book.path : book.title;
  renderer.drawTextRotated90CW(fontSize, rect.x + rect.width / 2 + fontSize / 2, rect.y + rect.height / 2 +
                                                                                    (int)title.size() * fontSize / 2,
                               title.c_str());
}
#endif  // FREEINK_DEVICE_READPICO

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

