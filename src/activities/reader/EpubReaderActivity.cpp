#include "EpubReaderActivity.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <Epub/Page.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <algorithm>
#include <climits>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ReadingThemeStore.h"
#include "ReadingThemesActivity.h"
#include "ReaderLayoutSafety.h"
#include "RecentBooksStore.h"
#include "activities/boot_sleep/LastSleepWallpaperActivity.h"
#include "activities/util/ConfirmDialogActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DrawUtils.h"
#include "util/StatusPopup.h"
#include "util/TransitionFeedback.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long confirmDoubleTapMs = 350;
constexpr unsigned long progressSaveDebounceMs = 800;
constexpr int progressBarMarginTop = 1;
constexpr int statusTextTopPadding = 4;
constexpr int statusTextLineGap = 1;
constexpr int statusTextToBarsGap = 0;
int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

void finishLoadingBar(GfxRenderer& renderer) {
  TransitionFeedback::dismiss(renderer);
}

// Use shared DrawUtils::drawDottedRect instead of local copy

void drawStyledProgressBar(const GfxRenderer &renderer,
                           const size_t progressPercent, const int y,
                           const int height) {
  int vieweableMarginTop, vieweableMarginRight, vieweableMarginBottom,
      vieweableMarginLeft;
  renderer.getOrientedViewableTRBL(&vieweableMarginTop, &vieweableMarginRight,
                                   &vieweableMarginBottom,
                                   &vieweableMarginLeft);
  const int maxWidth =
      renderer.getScreenWidth() - vieweableMarginLeft - vieweableMarginRight;
  const int startX = vieweableMarginLeft;
  const int barWidth =
      maxWidth * static_cast<int>(progressPercent) / 100;
  renderer.fillRect(startX, y, barWidth, height, true);
}

void normalizeReaderMargins(int *top, int *right, int *bottom, int *left) {
  const int vertical = std::max(*top, *bottom);
  const int horizontal = std::max(*left, *right);
  *top = vertical;
  *bottom = vertical;
  *left = horizontal;
  *right = horizontal;
}

int getStatusBottomInset(const GfxRenderer &renderer) {
  int baseTop, baseRight, baseBottom, baseLeft;
  renderer.getOrientedViewableTRBL(&baseTop, &baseRight, &baseBottom,
                                   &baseLeft);
  return baseBottom;
}

int getStatusTopInset(const GfxRenderer &renderer) {
  int baseTop, baseRight, baseBottom, baseLeft;
  renderer.getOrientedViewableTRBL(&baseTop, &baseRight, &baseBottom,
                                   &baseLeft);
  return baseTop;
}

bool statusBarItemIsTop(const uint8_t position) {
  return position == CrossPointSettings::STATUS_AT_TOP;
}

bool statusTextPositionIsTop(const uint8_t position) {
  return position <= CrossPointSettings::STATUS_TEXT_TOP_RIGHT;
}


int statusTextPositionHorizontalSlot(const uint8_t position) {
  switch (position) {
    case CrossPointSettings::STATUS_TEXT_TOP_LEFT:
    case CrossPointSettings::STATUS_TEXT_BOTTOM_LEFT:
      return 0;
    case CrossPointSettings::STATUS_TEXT_TOP_RIGHT:
    case CrossPointSettings::STATUS_TEXT_BOTTOM_RIGHT:
      return 2;
    case CrossPointSettings::STATUS_TEXT_TOP_CENTER:
    case CrossPointSettings::STATUS_TEXT_BOTTOM_CENTER:
    default:
      return 1;
  }
}

std::string formatPageCounterText(const uint8_t mode, const int currentPage,
                                  const int chapterPageCount,
                                  const float bookProgressPercent) {
  (void)bookProgressPercent;

  const int safeChapterPageCount = std::max(chapterPageCount, 0);
  const int safeCurrentPage = std::max(currentPage, 0);
  int pagesLeft = safeChapterPageCount - (currentPage + 1);
  if (pagesLeft < 0) {
    pagesLeft = 0;
  }

  switch (mode) {
    case CrossPointSettings::STATUS_PAGE_LEFT_TEXT:
      return std::to_string(pagesLeft) + " left";
    default:
      return std::to_string(safeCurrentPage + 1) + "/" +
             std::to_string(safeChapterPageCount);
  }
}

int computeStatusTextBlockHeight(const GfxRenderer &renderer,
                                 const bool showStatusTextRow,
                                 const int titleLineCount) {
  return ReaderLayoutSafety::computeStatusTextBlockHeight(
      renderer, showStatusTextRow, titleLineCount);
}

int computeStatusBarsHeight(const bool showBookProgressBar,
                            const bool showChapterProgressBar,
                            const int statusBarProgressHeight,
                            const bool includeTopMargin) {
  return ReaderLayoutSafety::computeStatusBarsHeight(
      showBookProgressBar, showChapterProgressBar, statusBarProgressHeight,
      includeTopMargin);
}

std::vector<std::string> wrapStatusText(const GfxRenderer &renderer,
                                        const int fontId,
                                        const std::string &text,
                                        const int maxWidth) {
  return ReaderLayoutSafety::wrapText(renderer, fontId, text, maxWidth);
}

int computeStatusBarReservedHeight(const GfxRenderer &renderer,
                                   const bool showStatusTextRow,
                                   const bool showBookProgressBar,
                                   const bool showChapterProgressBar,
                                   const int titleLineCount) {
  return ReaderLayoutSafety::computeReservedHeight(
      renderer, showStatusTextRow, showBookProgressBar, showChapterProgressBar,
      titleLineCount, SETTINGS.getStatusBarProgressBarHeight());
}

int resolveCurrentTocIndex(const std::shared_ptr<Epub>& epub,
                           const Section* section,
                           const int currentSpineIndex) {
  if (!epub) {
    return -1;
  }

  if (section != nullptr) {
    int bestTocIndex = -1;
    int bestPage = -1;
    for (const int tocIndex : epub->getTocIndexesForSpineIndex(currentSpineIndex)) {
      const auto tocItem = epub->getTocItem(tocIndex);
      if (tocItem.spineIndex != currentSpineIndex || tocItem.anchor.empty()) {
        continue;
      }

      const int tocPage = section->getPageForAnchor(tocItem.anchor);
      if (tocPage >= 0 && tocPage <= section->currentPage && tocPage >= bestPage) {
        bestPage = tocPage;
        bestTocIndex = tocIndex;
      }
    }

    if (bestTocIndex >= 0) {
      return bestTocIndex;
    }
  }

  return epub->getTocIndexForSpineIndex(currentSpineIndex);
}

// Apply the logical reader orientation to the renderer.
// This centralizes orientation mapping so we don't duplicate switch logic
// elsewhere.
void applyReaderOrientation(GfxRenderer &renderer, const uint8_t orientation) {
  switch (orientation) {
  case CrossPointSettings::ORIENTATION::PORTRAIT:
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    break;
  case CrossPointSettings::ORIENTATION::LANDSCAPE_CW:
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
    break;
  case CrossPointSettings::ORIENTATION::INVERTED:
    renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
    break;
  case CrossPointSettings::ORIENTATION::LANDSCAPE_CCW:
    renderer.setOrientation(
        GfxRenderer::Orientation::LandscapeCounterClockwise);
    break;
  default:
    break;
  }
}

} // namespace

void EpubReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  applyReaderOrientation(renderer, SETTINGS.orientation);
  EpdFontFamily::setReaderBoldSwapEnabled(SETTINGS.readerBoldSwap != 0);

  epub->setupCacheDir();

  FsFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin",
                              f)) {
    uint8_t data[6];
    int dataSize = f.read(data, 6);
    if (dataSize == 4 || dataSize == 6) {
      currentSpineIndex = data[0] + (data[1] << 8);
      nextPageNumber = data[2] + (data[3] << 8);
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = nextPageNumber;
      lastObservedSpineIndex = currentSpineIndex;
      lastObservedPage = nextPageNumber;
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
    }
    if (dataSize == 6) {
      cachedChapterTotalPageCount = data[4] + (data[5] << 8);
      lastSavedPageCount = cachedChapterTotalPageCount;
      lastObservedPageCount = cachedChapterTotalPageCount;
    }
    f.close();
  }
  const int spineCount = epub->getSpineItemsCount();
  if (spineCount > 0 && currentSpineIndex >= spineCount) {
    currentSpineIndex = spineCount - 1;
    nextPageNumber = UINT16_MAX;
  }

  // We may want a better condition to detect if we are opening for the first
  // time. This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS",
              "Opened for first time, navigating to text reference at index %d",
              textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(),
                       epub->getThumbBmpPath());

  invalidateStatusBarCaches();
  clearPageCache();

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  flushProgressIfNeeded(true);
  pendingMenuOpen = false;
  EpdFontFamily::setReaderBoldSwapEnabled(false);
  ActivityWithSubactivity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  clearPageCache();
  section.reset();
  epub.reset();
  invalidateStatusBarCaches();
}

void EpubReaderActivity::invalidateStatusBarCaches() {
  cachedReserveSpineIndex = -1;
  cachedReserveUsableWidth = -1;
  cachedReserveNoTitleTruncation = false;
  cachedReserveTitleLineCount = 1;
  cachedTitleTocIndex = -2;
  cachedTitleUsableWidth = -1;
  cachedTitleNoTitleTruncation = false;
  cachedTitleMaxLines = -1;
  cachedTitleLines.clear();
}

void EpubReaderActivity::clearPageCache() {
  for (auto& entry : pageCache) {
    entry.pageIndex = -1;
    entry.page.reset();
  }
  pageCacheSpineIndex = -1;
}

std::shared_ptr<Page> EpubReaderActivity::getCachedPage(
    const int pageIndex) const {
  if (pageCacheSpineIndex != currentSpineIndex) {
    return {};
  }

  const auto it = std::find_if(
      pageCache.begin(), pageCache.end(),
      [pageIndex](const PageCacheEntry& entry) {
        return entry.pageIndex == pageIndex;
      });
  if (it != pageCache.end()) {
    return it->page;
  }

  return {};
}

std::shared_ptr<Page> EpubReaderActivity::loadAndCachePage(const int pageIndex) {
  if (!section) {
    return {};
  }

  auto page = std::shared_ptr<Page>(section->loadPageFromSectionFile(pageIndex));
  if (!page) {
    return {};
  }

  pageCacheSpineIndex = currentSpineIndex;
  const auto it = std::find_if(
      pageCache.begin(), pageCache.end(),
      [pageIndex](const PageCacheEntry& entry) {
        return entry.pageIndex == pageIndex;
      });
  if (it != pageCache.end()) {
    it->page = page;
    return page;
  }

  pageCache[0] = pageCache[1];
  pageCache[1] = pageCache[2];
  pageCache[2] =
      PageCacheEntry{.pageIndex = pageIndex, .page = std::move(page)};
  return pageCache[2].page;
}

void EpubReaderActivity::refreshPageCacheWindow(
    const int centerPage, const std::shared_ptr<Page>& currentPage) {
  if (!section || centerPage < 0 || centerPage >= section->pageCount) {
    clearPageCache();
    return;
  }

  std::array<PageCacheEntry, 3> nextWindow{};
  const int targets[3] = {centerPage - 1, centerPage, centerPage + 1};

  for (size_t i = 0; i < nextWindow.size(); i++) {
    const int targetPage = targets[i];
    if (targetPage < 0 || targetPage >= section->pageCount) {
      continue;
    }

    std::shared_ptr<Page> page;
    if (targetPage == centerPage) {
      page = currentPage;
    } else {
      page = getCachedPage(targetPage);
      if (!page) {
        page = std::shared_ptr<Page>(section->loadPageFromSectionFile(targetPage));
      }
    }

    nextWindow[i] =
        PageCacheEntry{.pageIndex = targetPage, .page = std::move(page)};
  }

  pageCache = std::move(nextWindow);
  pageCacheSpineIndex = currentSpineIndex;
}

int EpubReaderActivity::getWrappedStatusBarReserveLineCount(
    const int usableWidth) {
  if (!epub || usableWidth <= 0) {
    return 1;
  }
  if (cachedReserveSpineIndex == currentSpineIndex &&
      cachedReserveUsableWidth == usableWidth &&
      cachedReserveNoTitleTruncation == SETTINGS.statusBarNoTitleTruncation) {
    return cachedReserveTitleLineCount;
  }

  int maxLines = 1;
  auto tocIndexes = epub->getTocIndexesForSpineIndex(currentSpineIndex);
  if (tocIndexes.empty()) {
    const int fallbackIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (fallbackIndex >= 0) {
      tocIndexes.push_back(fallbackIndex);
    }
  }

  for (const int tocIndex : tocIndexes) {
    const std::string title = epub->formatTocDisplayTitle(tocIndex);
    if (title.empty()) {
      continue;
    }
    const int lineCount = static_cast<int>(
        wrapStatusText(renderer, SMALL_FONT_ID, title, usableWidth).size());
    if (lineCount > maxLines) {
      maxLines = lineCount;
    }
  }

  cachedReserveSpineIndex = currentSpineIndex;
  cachedReserveUsableWidth = usableWidth;
  cachedReserveNoTitleTruncation = SETTINGS.statusBarNoTitleTruncation;
  cachedReserveTitleLineCount = maxLines;
  return cachedReserveTitleLineCount;
}

const std::vector<std::string>& EpubReaderActivity::getStatusBarTitleLines(
    const int tocIndex, const int usableWidth, const bool noTitleTruncation,
    const int maxTitleLineCount) {
  if (cachedTitleTocIndex == tocIndex && cachedTitleUsableWidth == usableWidth &&
      cachedTitleNoTitleTruncation == noTitleTruncation &&
      cachedTitleMaxLines == maxTitleLineCount) {
    return cachedTitleLines;
  }

  std::string titleText = tr(STR_UNNAMED);
  if (tocIndex >= 0 && epub) {
    titleText = epub->formatTocDisplayTitle(tocIndex);
    if (titleText.empty()) {
      titleText = tr(STR_UNNAMED);
    }
  }

  cachedTitleLines = ReaderLayoutSafety::buildTitleLines(
      renderer, SMALL_FONT_ID, titleText, usableWidth, noTitleTruncation,
      maxTitleLineCount);

  cachedTitleTocIndex = tocIndex;
  cachedTitleUsableWidth = usableWidth;
  cachedTitleNoTitleTruncation = noTitleTruncation;
  cachedTitleMaxLines = maxTitleLineCount;
  return cachedTitleLines;
}

EpubReaderActivity::StatusBarLayout EpubReaderActivity::buildStatusBarLayout(
    const int usableWidth, const int topReservedHeight,
    const int bottomReservedHeight, const int maxTitleLineCount) {
  StatusBarLayout layout;
  layout.usableWidth = ReaderLayoutSafety::clampViewportDimension(
      usableWidth, ReaderLayoutSafety::kMinViewportWidth, "ERS",
      "status width");
  layout.topReservedHeight = topReservedHeight;
  layout.bottomReservedHeight = bottomReservedHeight;
  if (!SETTINGS.statusBarEnabled || !section) {
    return layout;
  }

  const float sectionChapterProg =
      (section->pageCount > 0)
          ? static_cast<float>(section->currentPage) / section->pageCount
          : 0.0f;
  layout.bookProgress =
      epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100.0f;
  layout.chapterProgress =
      (section->pageCount > 0)
          ? (static_cast<float>(section->currentPage + 1) / section->pageCount) *
                100.0f
          : 0.0f;

  if (SETTINGS.statusBarShowPageCounter) {
    layout.pageCounterText = formatPageCounterText(
        SETTINGS.statusBarPageCounterMode, section->currentPage,
        section->pageCount, layout.bookProgress);
    layout.pageCounterTextWidth =
        renderer.getTextWidth(SMALL_FONT_ID, layout.pageCounterText.c_str());
  }
  if (SETTINGS.statusBarShowBookPercentage) {
    char bookPercentageStr[16] = {0};
    snprintf(bookPercentageStr, sizeof(bookPercentageStr), "B:%.0f%%",
             layout.bookProgress);
    layout.bookPercentageText = bookPercentageStr;
    layout.bookPercentageTextWidth = renderer.getTextWidth(
        SMALL_FONT_ID, layout.bookPercentageText.c_str());
  }
  if (SETTINGS.statusBarShowChapterPercentage) {
    char chapterPercentageStr[16] = {0};
    snprintf(chapterPercentageStr, sizeof(chapterPercentageStr), "C:%.0f%%",
             layout.chapterProgress);
    layout.chapterPercentageText = chapterPercentageStr;
    layout.chapterPercentageTextWidth = renderer.getTextWidth(
        SMALL_FONT_ID, layout.chapterPercentageText.c_str());
  }

  if (SETTINGS.statusBarShowChapterTitle) {
    const int tocIndex = section->getTocIndexForPage(section->currentPage);
    layout.titleLines = getStatusBarTitleLines(
        tocIndex, layout.usableWidth, SETTINGS.statusBarNoTitleTruncation,
        maxTitleLineCount);
    layout.titleLineWidths.reserve(layout.titleLines.size());
    for (const auto& line : layout.titleLines) {
      layout.titleLineWidths.push_back(
          renderer.getTextWidth(SMALL_FONT_ID, line.c_str()));
    }
  }

  return layout;
}

void EpubReaderActivity::loop() {
  flushProgressIfNeeded(false);

  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    subActivity->loop();
    // Deferred exit: process after subActivity->loop() returns to avoid
    // use-after-free
    if (pendingSubactivityExit) {
      pendingSubactivityExit = false;
      exitActivity();
      requestUpdate();
      skipNextButtonCheck =
          true; // Skip button processing to ignore stale events
    }
    // Deferred go home: process after subActivity->loop() returns to avoid race
    // condition
    if (pendingGoHome) {
      pendingGoHome = false;
      exitActivity();
      if (onGoHome) {
        onGoHome();
      }
      return; // Don't access 'this' after callback
    }
    return;
  }

  // Handle pending go home when no subactivity (e.g., from long press back)
  if (pendingGoHome) {
    pendingGoHome = false;
    if (onGoHome) {
      onGoHome();
    }
    return; // Don't access 'this' after callback
  }
  if (pendingGoLibrary) {
    pendingGoLibrary = false;
    if (onGoBack) {
      onGoBack();
    }
    return; // Don't access 'this' after callback
  }

  if (pendingMenuOpen &&
      !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      millis() - lastConfirmReleaseMs > confirmDoubleTapMs) {
    pendingMenuOpen = false;
    openReaderMenu();
    return;
  }

  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    confirmLongPressHandled = false;
  }

  // Skip button processing after returning from subactivity
  // This prevents stale button release events from triggering actions
  // We wait until: (1) all relevant buttons are released, AND (2) wasReleased
  // events have been cleared
  if (skipNextButtonCheck) {
    const bool confirmCleared =
        !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    const bool backCleared =
        !mappedInput.isPressed(MappedInputManager::Button::Back) &&
        !mappedInput.wasReleased(MappedInputManager::Button::Back);
    if (confirmCleared && backCleared) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // Single tap opens menu; double tap toggles text render mode (Dark/Crisp).
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (suppressNextConfirmRelease) {
      suppressNextConfirmRelease = false;
      pendingMenuOpen = false;
      return;
    }
    const unsigned long now = millis();
    if (pendingMenuOpen && now - lastConfirmReleaseMs <= confirmDoubleTapMs) {
      pendingMenuOpen = false;
      toggleTextRenderMode();
      return;
    }
    pendingMenuOpen = true;
    lastConfirmReleaseMs = now;
    return;
  }

  // Long press CONFIRM (1s+) toggles orientation: Portrait <-> Landscape CCW.
  if (!confirmLongPressHandled &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= goHomeMs) {
    confirmLongPressHandled = true;
    suppressNextConfirmRelease = true;
    const uint8_t nextOrientation =
        (SETTINGS.orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CCW)
            ? CrossPointSettings::ORIENTATION::PORTRAIT
            : CrossPointSettings::ORIENTATION::LANDSCAPE_CCW;
    applyOrientation(nextOrientation);
    requestUpdate();
    return;
  }

  // BACK: go home immediately on press for snappier response.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  // When long-press chapter skip is disabled, turn pages on press instead of
  // release.
  const bool usePressForPageTurn = !SETTINGS.longPressChapterSkip;
  const bool prevTriggered =
      usePressForPageTurn
          ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
             mappedInput.wasPressed(MappedInputManager::Button::Left))
          : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
             mappedInput.wasReleased(MappedInputManager::Button::Left));
  const bool powerPageTurn =
      SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
      mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered =
      usePressForPageTurn
          ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
             powerPageTurn ||
             mappedInput.wasPressed(MappedInputManager::Button::Right))
          : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
             powerPageTurn ||
             mappedInput.wasReleased(MappedInputManager::Button::Right));

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // any botton press when at end of the book goes back to the last page
  if (currentSpineIndex > 0 &&
      currentSpineIndex >= epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = UINT16_MAX;
    requestUpdate();
    return;
  }

  const bool skipChapter = SETTINGS.longPressChapterSkip &&
                           mappedInput.getHeldTime() > skipChapterMs;

  if (skipChapter) {
    TransitionFeedback::show(renderer, tr(STR_LOADING));
    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      currentSpineIndex =
          nextTriggered ? currentSpineIndex + 1 : currentSpineIndex - 1;
      saveProgress(currentSpineIndex, nextPageNumber, 1);
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = nextPageNumber;
      lastSavedPageCount = 1;
      progressDirty = false;
      clearPageCache();
      section.reset();
    }
    requestUpdate();
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered) {
    if (section->currentPage > 0) {
      section->currentPage--;
      progressDirty = true;
      lastProgressChangeMs = millis();
      flushProgressIfNeeded(true);
    } else if (currentSpineIndex > 0) {
      TransitionFeedback::show(renderer, tr(STR_LOADING));
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
        saveProgress(currentSpineIndex, nextPageNumber, 1);
        lastSavedSpineIndex = currentSpineIndex;
        lastSavedPage = nextPageNumber;
        lastSavedPageCount = 1;
        progressDirty = false;
        clearPageCache();
        section.reset();
      }
    }
    requestUpdate();
  } else {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
      addSessionPagesRead();
      progressDirty = true;
      lastProgressChangeMs = millis();
      flushProgressIfNeeded(true);
    } else {
      TransitionFeedback::show(renderer, tr(STR_LOADING));
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        const bool hasNextSection =
            epub && currentSpineIndex + 1 < epub->getSpineItemsCount();
        currentSpineIndex++;
        if (hasNextSection) {
          addSessionPagesRead();
        }
        saveProgress(currentSpineIndex, nextPageNumber, 1);
        lastSavedSpineIndex = currentSpineIndex;
        lastSavedPage = nextPageNumber;
        lastSavedPageCount = 1;
        progressDirty = false;
        clearPageCache();
        section.reset();
      }
    }
    requestUpdate();
  }
}

void EpubReaderActivity::openReaderMenu() {
  const int currentPage = section ? section->currentPage + 1 : 0;
  const int totalPages = section ? section->pageCount : 0;
  float bookProgress = 0.0f;
  if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
    const float chapterProgress = static_cast<float>(section->currentPage) /
                                  static_cast<float>(section->pageCount);
    bookProgress =
        epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
  }
  const int bookProgressPercent =
      clampPercent(static_cast<int>(bookProgress + 0.5f));
  exitActivity();
  enterNewActivity(new EpubReaderMenuActivity(
      this->renderer, this->mappedInput, epub->getTitle(), currentPage,
      totalPages, bookProgressPercent, SETTINGS.orientation,
      [this](const uint8_t orientation) { onReaderMenuBack(orientation); },
      [this](EpubReaderMenuActivity::MenuAction action) {
        onReaderMenuConfirm(action);
      }));
}

void EpubReaderActivity::toggleTextRenderMode() {
  TransitionFeedback::show(renderer, tr(STR_LOADING));
  flushProgressIfNeeded(true);
  SETTINGS.textRenderMode =
      (SETTINGS.textRenderMode == CrossPointSettings::TEXT_RENDER_DARK)
          ? CrossPointSettings::TEXT_RENDER_CRISP
          : CrossPointSettings::TEXT_RENDER_DARK;
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("ERS", "Failed to save settings after text render mode toggle");
  }

  uint16_t backupSpine = 0;
  uint16_t backupPage = 0;
  uint16_t backupPageCount = 1;
  if (epub) {
    const uint16_t spineCount = epub->getSpineItemsCount();
    if (section && section->pageCount > 0) {
      backupSpine = currentSpineIndex;
      backupPage = section->currentPage;
      backupPageCount = section->pageCount;
    } else if (spineCount > 0) {
      if (currentSpineIndex >= spineCount) {
        backupSpine = spineCount - 1;
        backupPage = UINT16_MAX;
      } else {
        backupSpine = currentSpineIndex;
      }
    }
  }

  {
    RenderLock lock(*this);
    clearPageCache();
    section.reset();
    saveProgress(backupSpine, backupPage, backupPageCount);
    lastSavedSpineIndex = backupSpine;
    lastSavedPage = backupPage;
    lastSavedPageCount = backupPageCount;
    lastObservedSpineIndex = backupSpine;
    lastObservedPage = backupPage;
    lastObservedPageCount = backupPageCount;
    progressDirty = false;
    nextPageNumber = backupPage;
    cachedSpineIndex = backupSpine;
    cachedChapterTotalPageCount = backupPageCount;
  }
  requestUpdate();
}

void EpubReaderActivity::onReaderMenuBack(const uint8_t orientation) {
  exitActivity();
  // Apply the user-selected orientation when the menu is dismissed.
  // This ensures the menu can be navigated without immediately rotating the
  // screen.
  applyOrientation(orientation);
  requestUpdate();
}

// Translate an absolute percent into a spine index plus a normalized position
// within that spine so we can jump after the section is loaded.
void EpubReaderActivity::jumpToPercent(int percent) {
  if (!epub) {
    return;
  }

  const size_t bookSize = epub->getBookSize();
  if (bookSize == 0) {
    return;
  }

  // Normalize input to 0-100 to avoid invalid jumps.
  percent = clampPercent(percent);

  // Convert percent into a byte-like absolute position across the spine sizes.
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize %
  // 100) * percent / 100
  size_t targetSize = (bookSize / 100) * static_cast<size_t>(percent) +
                      (bookSize % 100) * static_cast<size_t>(percent) / 100;
  if (percent >= 100) {
    // Ensure the final percent lands inside the last spine item.
    targetSize = bookSize - 1;
  }

  const int spineCount = epub->getSpineItemsCount();
  if (spineCount == 0) {
    return;
  }

  int targetSpineIndex = spineCount - 1;
  size_t prevCumulative = 0;

  for (int i = 0; i < spineCount; i++) {
    const size_t cumulative = epub->getCumulativeSpineItemSize(i);
    if (targetSize <= cumulative) {
      // Found the spine item containing the absolute position.
      targetSpineIndex = i;
      prevCumulative = (i > 0) ? epub->getCumulativeSpineItemSize(i - 1) : 0;
      break;
    }
  }

  const size_t cumulative = epub->getCumulativeSpineItemSize(targetSpineIndex);
  const size_t spineSize =
      (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once
  // loaded.
  pendingSpineProgress = (spineSize == 0)
                             ? 0.0f
                             : static_cast<float>(targetSize - prevCumulative) /
                                   static_cast<float>(spineSize);
  if (pendingSpineProgress < 0.0f) {
    pendingSpineProgress = 0.0f;
  } else if (pendingSpineProgress > 1.0f) {
    pendingSpineProgress = 1.0f;
  }

  // Reset state so render() reloads and repositions on the target spine.
  TransitionFeedback::show(renderer, tr(STR_LOADING));
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
    clearPageCache();
    section.reset();
  }
}

void EpubReaderActivity::onReaderMenuConfirm(
    EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
  case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
    // Calculate values BEFORE we start destroying things
    const int spineIdx = currentSpineIndex;
    const int currentTocIndex =
        section ? section->getTocIndexForPage(section->currentPage)
                : resolveCurrentTocIndex(epub, section.get(), currentSpineIndex);
    const std::string path = epub->getPath();

    // 1. Close the menu
    exitActivity();

    // 2. Open the Chapter Selector
    enterNewActivity(new EpubReaderChapterSelectionActivity(
        this->renderer, this->mappedInput, epub, path, spineIdx,
        currentTocIndex,
        [this] {
          exitActivity();
          requestUpdate();
        },
        [this](const int tocIndex) {
          const auto tocItem = epub->getTocItem(tocIndex);
          const int newSpineIndex = tocItem.spineIndex;
          if (newSpineIndex < 0) {
            exitActivity();
            requestUpdate();
            return;
          }

          pendingAnchor = tocItem.anchor;
          if (currentSpineIndex != newSpineIndex || section) {
            TransitionFeedback::show(renderer, tr(STR_LOADING));
            currentSpineIndex = newSpineIndex;
            nextPageNumber = 0;
            clearPageCache();
            section.reset();
          }
          exitActivity();
          requestUpdate();
        },
        [this](const int newSpineIndex, const int newPage) {
          if (currentSpineIndex != newSpineIndex ||
              (section && section->currentPage != newPage)) {
            TransitionFeedback::show(renderer, tr(STR_LOADING));
            currentSpineIndex = newSpineIndex;
            nextPageNumber = newPage;
            clearPageCache();
            section.reset();
          }
          exitActivity();
          requestUpdate();
        }));

    break;
  }
  case EpubReaderMenuActivity::MenuAction::GO_HOME: {
    // Defer go home to avoid race condition with display task
    pendingGoHome = true;
    break;
  }
  case EpubReaderMenuActivity::MenuAction::THEMES_MENU: {
    exitActivity();
    enterNewActivity(new ReadingThemesActivity(
        renderer, mappedInput, epub ? epub->getCachePath() : std::string(),
        [this](const bool changed) {
          exitActivity();
          pendingMenuOpen = false;
          skipNextButtonCheck = true;
          if (changed) {
            reloadCurrentSectionForDisplaySettings();
          } else {
            requestUpdate();
          }
        }));
    break;
  }
  case EpubReaderMenuActivity::MenuAction::REVERT_THEME: {
    exitActivity();
    pendingMenuOpen = false;
    skipNextButtonCheck = true;
    const std::string cachePath = epub ? epub->getCachePath() : std::string();
    if (!READING_THEMES.canRevertTheme(cachePath)) {
      StatusPopup::showBlocking(renderer, tr(STR_NO_THEME_TO_REVERT));
      delay(500);
      requestUpdate();
      break;
    }
    if (!READING_THEMES.revertThemeChange(cachePath)) {
      StatusPopup::showBlocking(renderer, tr(STR_THEME_REVERT_FAILED));
      delay(500);
      requestUpdate();
      break;
    }
    reloadCurrentSectionForDisplaySettings();
    break;
  }
  case EpubReaderMenuActivity::MenuAction::LAST_SLEEP_WALLPAPER: {
    exitActivity();
    enterNewActivity(new LastSleepWallpaperActivity(
        renderer, mappedInput, [this]() {
          exitActivity();
          pendingMenuOpen = false;
          skipNextButtonCheck = true;
          requestUpdate();
        }));
    break;
  }
  case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
    exitActivity();
    enterNewActivity(new ConfirmDialogActivity(
        renderer, mappedInput,
        "Clear cached pages and reset reading progress to page 1?",
        [this]() {
          // Confirmed — clear cache and reset progress.
          exitActivity();
          StatusPopup::showBlocking(renderer, "Clearing book cache");
          {
            RenderLock lock(*this);
            if (epub) {
              const uint16_t resetSpine = 0;
              const uint16_t resetPage = 0;
              const uint16_t resetPageCount = 1;

              section.reset();
              clearPageCache();
              epub->clearCache();
              epub->setupCacheDir();
              saveProgress(resetSpine, resetPage, resetPageCount);

              currentSpineIndex = resetSpine;
              nextPageNumber = resetPage;
              cachedSpineIndex = resetSpine;
              cachedChapterTotalPageCount = resetPageCount;
              lastSavedSpineIndex = resetSpine;
              lastSavedPage = resetPage;
              lastSavedPageCount = resetPageCount;
              lastObservedSpineIndex = resetSpine;
              lastObservedPage = resetPage;
              lastObservedPageCount = resetPageCount;
              progressDirty = false;
            }
          }
          pendingGoHome = true;
        },
        [this]() {
          // Cancelled — return to reader.
          exitActivity();
          requestUpdate();
        }));
    break;
  }
  case EpubReaderMenuActivity::MenuAction::SYNC: {
    if (KOREADER_STORE.hasCredentials()) {
      const int currentPage = section ? section->currentPage : 0;
      const int totalPages = section ? section->pageCount : 0;
      exitActivity();
      enterNewActivity(new KOReaderSyncActivity(
          renderer, mappedInput, epub, epub->getPath(), currentSpineIndex,
          currentPage, totalPages,
          [this]() {
            // On cancel - defer exit to avoid use-after-free
            pendingSubactivityExit = true;
          },
          [this](int newSpineIndex, int newPage) {
            // On sync complete - update position and defer exit
            if (currentSpineIndex != newSpineIndex ||
                (section && section->currentPage != newPage)) {
              TransitionFeedback::show(renderer, tr(STR_LOADING));
              currentSpineIndex = newSpineIndex;
              nextPageNumber = newPage;
              clearPageCache();
              section.reset();
            }
            pendingSubactivityExit = true;
          }));
    }
    break;
  }
  case EpubReaderMenuActivity::MenuAction::DELETE_BOOK: {
    std::string deletingPath;
    StatusPopup::showBlocking(renderer, "Deleting book");
    {
      RenderLock lock(*this);
      if (epub) {
        deletingPath = epub->getPath();
        clearPageCache();
        section.reset();
        epub->clearCache();
      }
    }

    if (!deletingPath.empty()) {
      RECENT_BOOKS.removeBook(deletingPath);
      if (APP_STATE.openEpubPath == deletingPath) {
        APP_STATE.openEpubPath = "";
        APP_STATE.saveToFile();
      }
      const bool removed = Storage.remove(deletingPath.c_str());
      LOG_DBG("ERS", "Delete book '%s': %s", deletingPath.c_str(),
              removed ? "ok" : "failed");
    }
    pendingGoLibrary = true;
    break;
  }
  case EpubReaderMenuActivity::MenuAction::REMOVE_FROM_RECENT: {
    if (epub) {
      RECENT_BOOKS.removeBook(epub->getPath());
      if (APP_STATE.openEpubPath == epub->getPath()) {
        APP_STATE.openEpubPath = "";
        APP_STATE.saveToFile();
      }
    }
    pendingGoHome = true;
    break;
  }
  }
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // No-op if the selected orientation matches current settings.
  if (SETTINGS.orientation == orientation) {
    return;
  }

  TransitionFeedback::show(renderer, tr(STR_LOADING));

  // Preserve current reading position so we can restore after reflow.
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }

    // Persist the selection so the reader keeps the new orientation on next
    // launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    applyReaderOrientation(renderer, SETTINGS.orientation);

    invalidateStatusBarCaches();

    // Reset section to force re-layout in the new orientation.
    clearPageCache();
    section.reset();
  }
}

void EpubReaderActivity::reloadCurrentSectionForDisplaySettings() {
  TransitionFeedback::show(renderer, tr(STR_LOADING));
  flushProgressIfNeeded(true);
  if (epub && SETTINGS.readerStyleMode == CrossPointSettings::READER_STYLE_HYBRID) {
    const bool showCssProgress =
        epub->getCssParser() == nullptr || !epub->getCssParser()->hasCache();
    const auto progressCallback = std::function<void(int)>();
    if (!epub->ensureCssCache(progressCallback)) {
      LOG_ERR("ERS", "Failed to prepare CSS cache for hybrid reader style");
    } else if (showCssProgress) {
      finishLoadingBar(renderer);
    }
  }
  {
    RenderLock lock(*this);
    if (section) {
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
      saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
      lastSavedSpineIndex = currentSpineIndex;
      lastSavedPage = section->currentPage;
      lastSavedPageCount = section->pageCount;
      lastObservedSpineIndex = currentSpineIndex;
      lastObservedPage = section->currentPage;
      lastObservedPageCount = section->pageCount;
      progressDirty = false;
    }
    invalidateStatusBarCaches();
    clearPageCache();
    section.reset();
  }
  requestUpdate();
}

void EpubReaderActivity::render(Activity::RenderLock &&lock) {
  if (!epub) {
    return;
  }

  // edge case handling for sub-zero spine index
  if (currentSpineIndex < 0) {
    currentSpineIndex = 0;
  }
  // based bounds of book, show end of book screen
  if (currentSpineIndex > epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount();
  }

  // Show end of book screen
  if (currentSpineIndex == epub->getSpineItemsCount()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true,
                              EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom,
      orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight,
                                   &orientedMarginBottom, &orientedMarginLeft);
  normalizeReaderMargins(&orientedMarginTop, &orientedMarginRight,
                         &orientedMarginBottom, &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMarginTop;
  orientedMarginLeft += SETTINGS.screenMarginHorizontal;
  orientedMarginRight += SETTINGS.screenMarginHorizontal;
  orientedMarginBottom += SETTINGS.screenMarginBottom;
  const int minContentHeight = std::max(
      ReaderLayoutSafety::kMinViewportHeight,
      renderer.getLineHeight(SETTINGS.getReaderFontId()) * 2);

  const int usableWidth =
      ReaderLayoutSafety::clampViewportDimension(
          renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight,
          ReaderLayoutSafety::kMinViewportWidth, "ERS", "usable width");
  int statusBarTopReserved = 0;
  int statusBarBottomReserved = 0;
  int resolvedTitleLineCount = SETTINGS.statusBarShowChapterTitle ? 1 : 0;
  if (SETTINGS.statusBarEnabled) {
    const bool showTopStatusTextRow =
        (SETTINGS.statusBarShowBattery &&
         statusBarItemIsTop(SETTINGS.statusBarBatteryPosition)) ||
        (SETTINGS.statusBarShowPageCounter &&
         statusTextPositionIsTop(SETTINGS.statusBarPageCounterPosition)) ||
        (SETTINGS.statusBarShowBookPercentage &&
         statusTextPositionIsTop(SETTINGS.statusBarBookPercentagePosition)) ||
        (SETTINGS.statusBarShowChapterPercentage &&
         statusTextPositionIsTop(
             SETTINGS.statusBarChapterPercentagePosition));
    const bool showBottomStatusTextRow =
        (SETTINGS.statusBarShowBattery &&
         !statusBarItemIsTop(SETTINGS.statusBarBatteryPosition)) ||
        (SETTINGS.statusBarShowPageCounter &&
         !statusTextPositionIsTop(SETTINGS.statusBarPageCounterPosition)) ||
        (SETTINGS.statusBarShowBookPercentage &&
         !statusTextPositionIsTop(SETTINGS.statusBarBookPercentagePosition)) ||
        (SETTINGS.statusBarShowChapterPercentage &&
         !statusTextPositionIsTop(
             SETTINGS.statusBarChapterPercentagePosition));
    int titleLineCount = SETTINGS.statusBarShowChapterTitle ? 1 : 0;
    if (SETTINGS.statusBarShowChapterTitle &&
        SETTINGS.statusBarNoTitleTruncation) {
      titleLineCount = getWrappedStatusBarReserveLineCount(usableWidth);
    }
    const int topTitleLineCount =
        (SETTINGS.statusBarShowChapterTitle &&
         statusBarItemIsTop(SETTINGS.statusBarTitlePosition))
            ? titleLineCount
            : 0;
    const int bottomTitleLineCount =
        (SETTINGS.statusBarShowChapterTitle &&
         !statusBarItemIsTop(SETTINGS.statusBarTitlePosition))
            ? titleLineCount
            : 0;
    const auto budget = ReaderLayoutSafety::resolveStatusBarBudget(
        renderer, "ERS", renderer.getScreenHeight(), getStatusTopInset(renderer),
        getStatusBottomInset(renderer), SETTINGS.screenMarginTop,
        SETTINGS.screenMarginBottom, minContentHeight,
        SETTINGS.getStatusBarProgressBarHeight(),
        ReaderLayoutSafety::StatusBarBandConfig{
            .showStatusTextRow = showTopStatusTextRow,
            .showBookProgressBar =
                SETTINGS.statusBarShowBookBar &&
                statusBarItemIsTop(SETTINGS.statusBarBookBarPosition),
            .showChapterProgressBar =
                SETTINGS.statusBarShowChapterBar &&
                statusBarItemIsTop(SETTINGS.statusBarChapterBarPosition),
            .desiredTitleLineCount = topTitleLineCount,
        },
        ReaderLayoutSafety::StatusBarBandConfig{
            .showStatusTextRow = showBottomStatusTextRow,
            .showBookProgressBar =
                SETTINGS.statusBarShowBookBar &&
                !statusBarItemIsTop(SETTINGS.statusBarBookBarPosition),
            .showChapterProgressBar =
                SETTINGS.statusBarShowChapterBar &&
                !statusBarItemIsTop(SETTINGS.statusBarChapterBarPosition),
            .desiredTitleLineCount = bottomTitleLineCount,
        });
    statusBarTopReserved = budget.top.reservedHeight;
    statusBarBottomReserved = budget.bottom.reservedHeight;
    resolvedTitleLineCount =
        statusBarItemIsTop(SETTINGS.statusBarTitlePosition)
            ? budget.top.titleLineCount
            : budget.bottom.titleLineCount;
    if (statusBarTopReserved > 0) {
      orientedMarginTop =
          getStatusTopInset(renderer) + SETTINGS.screenMarginTop +
          statusBarTopReserved;
    }
    if (statusBarBottomReserved > 0) {
      // When the status bar is present it handles the display bottom inset
      // itself. Use only the display inset + user margin so the gap equals
      // exactly screenMarginBottom (0 = text flush against the status bar).
      orientedMarginBottom =
          getStatusBottomInset(renderer) + SETTINGS.screenMarginBottom +
          statusBarBottomReserved;
    }
  }

  const uint16_t viewportWidth =
      static_cast<uint16_t>(ReaderLayoutSafety::clampViewportDimension(
          renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight,
          ReaderLayoutSafety::kMinViewportWidth, "ERS", "viewport width"));
  const uint16_t viewportHeight =
      static_cast<uint16_t>(ReaderLayoutSafety::clampViewportDimension(
          renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom,
          minContentHeight, "ERS", "viewport height"));

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(),
            currentSpineIndex);
    section = std::unique_ptr<Section>(
        new Section(epub, currentSpineIndex, renderer));
    bool builtSection = false;
    clearPageCache();

    const uint8_t sectionTextRenderMode =
        SETTINGS.textRenderMode;

    if (!section->loadSectionFile(
            SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
            SETTINGS.extraParagraphSpacingLevel, SETTINGS.paragraphAlignment,
            viewportWidth, viewportHeight, false,
            SETTINGS.wordSpacingPercent, SETTINGS.firstLineIndentMode,
            SETTINGS.readerStyleMode, sectionTextRenderMode,
            SETTINGS.readerBoldSwap != 0)) {
      LOG_DBG("ERS", "Cache not found, building...");
      builtSection = true;

      if (!section->createSectionFile(
              SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
              SETTINGS.extraParagraphSpacingLevel, SETTINGS.paragraphAlignment,
              viewportWidth, viewportHeight, false,
              SETTINGS.wordSpacingPercent, SETTINGS.firstLineIndentMode,
              SETTINGS.readerStyleMode, sectionTextRenderMode,
              SETTINGS.readerBoldSwap != 0, std::function<void(int)>())) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
        clearPageCache();
        section.reset();
        return;
      }
    } else {
      LOG_DBG("ERS", "Cache found, skipping build...");
    }

    if (nextPageNumber == UINT16_MAX) {
      section->currentPage = section->pageCount - 1;
    } else {
      section->currentPage = nextPageNumber;
    }

    // handles changes in reader settings and reset to approximate position
    // based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex &&
          section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) /
                         static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount =
          0; // resets to 0 to prevent reading cached progress again
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page
      // count.
      int newPage = static_cast<int>(pendingSpineProgress *
                                     static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }

    if (!pendingAnchor.empty()) {
      const int anchorPage = section->getPageForAnchor(pendingAnchor);
      if (anchorPage >= 0 && anchorPage < section->pageCount) {
        section->currentPage = anchorPage;
      }
      pendingAnchor.clear();
    }

    finishLoadingBar(renderer);
  }

  renderer.clearScreen();
  const StatusBarLayout statusBarLayout =
      buildStatusBarLayout(usableWidth, statusBarTopReserved,
                           statusBarBottomReserved, resolvedTitleLineCount);

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true,
                              EpdFontFamily::REGULAR);
    renderStatusBar(statusBarLayout, orientedMarginRight, orientedMarginBottom,
                    orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d), clamping",
            section->currentPage, section->pageCount);
    if (section->currentPage < 0) {
      section->currentPage = 0;
    } else {
      section->currentPage = section->pageCount - 1;
    }
  }

  {
    auto p = getCachedPage(section->currentPage);
    if (!p) {
      p = loadAndCachePage(section->currentPage);
    }
    if (!p) {
      pageLoadFailCount++;
      LOG_ERR(
          "ERS",
          "Failed to load page from SD - clearing section cache (attempt %d)",
          pageLoadFailCount);
      section->clearCache();
      clearPageCache();
      section.reset();
      if (pageLoadFailCount < 3) {
        requestUpdate(); // Try again after clearing cache
      } else {
        LOG_ERR("ERS",
                "Page load failed %d times, giving up to prevent infinite loop",
                pageLoadFailCount);
      }
      return;
    }
    pageLoadFailCount = 0;
    refreshPageCacheWindow(section->currentPage, p);
    const auto start = millis();
    renderContents(*p, orientedMarginTop, orientedMarginRight,
                   orientedMarginBottom, orientedMarginLeft, statusBarLayout);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  silentIndexNextChapterIfNeeded(viewportWidth, viewportHeight);
  if (lastObservedSpineIndex != currentSpineIndex ||
      lastObservedPage != section->currentPage ||
      lastObservedPageCount != section->pageCount) {
    lastObservedSpineIndex = currentSpineIndex;
    lastObservedPage = section->currentPage;
    lastObservedPageCount = section->pageCount;
    if (lastSavedSpineIndex != currentSpineIndex ||
        lastSavedPage != section->currentPage ||
        lastSavedPageCount != section->pageCount) {
      progressDirty = true;
      lastProgressChangeMs = millis();
    }
  }

  flushProgressIfNeeded(false);
}

void EpubReaderActivity::silentIndexNextChapterIfNeeded(const uint16_t viewportWidth, const uint16_t viewportHeight) {
  if (!epub || !section || section->pageCount < 2) {
    return;
  }

  // Build the next chapter cache while the penultimate page is on screen.
  if (section->currentPage != section->pageCount - 2) {
    return;
  }

  const int nextSpineIndex = currentSpineIndex + 1;
  if (nextSpineIndex < 0 || nextSpineIndex >= epub->getSpineItemsCount()) {
    return;
  }

  const uint8_t sectionTextRenderMode = SETTINGS.textRenderMode;

  Section nextSection(epub, nextSpineIndex, renderer);
  if (nextSection.loadSectionFile(
          SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
          SETTINGS.extraParagraphSpacingLevel, SETTINGS.paragraphAlignment,
          viewportWidth, viewportHeight, false,
          SETTINGS.wordSpacingPercent, SETTINGS.firstLineIndentMode,
          SETTINGS.readerStyleMode, sectionTextRenderMode,
          SETTINGS.readerBoldSwap != 0)) {
    return;  // Already cached
  }

  LOG_DBG("ERS", "Silently indexing next chapter: %d", nextSpineIndex);
  if (!nextSection.createSectionFile(
          SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
          SETTINGS.extraParagraphSpacingLevel, SETTINGS.paragraphAlignment,
          viewportWidth, viewportHeight, false,
          SETTINGS.wordSpacingPercent, SETTINGS.firstLineIndentMode,
          SETTINGS.readerStyleMode, sectionTextRenderMode,
          SETTINGS.readerBoldSwap != 0)) {
    LOG_ERR("ERS", "Failed silent indexing for chapter: %d", nextSpineIndex);
  }
}

void EpubReaderActivity::saveProgress(int spineIndex, int currentPage,
                                      int pageCount) {
  if (!epub) {
    return;
  }
  const int spineCount = epub->getSpineItemsCount();
  if (spineCount <= 0) {
    return;
  }
  if (spineIndex < 0) {
    spineIndex = 0;
  } else if (spineIndex >= spineCount) {
    spineIndex = spineCount - 1;
    currentPage = UINT16_MAX;
  }
  if (pageCount <= 0) {
    pageCount = 1;
  }

  const std::string progPath = epub->getCachePath() + "/progress.bin";
  const std::string tmpPath = epub->getCachePath() + "/progress_tmp.bin";

  if (Storage.exists(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
  }

  FsFile f;
  if (Storage.openFileForWrite("ERS", tmpPath.c_str(), f)) {
    uint8_t data[6];
    data[0] = spineIndex & 0xFF;
    data[1] = (spineIndex >> 8) & 0xFF;
    data[2] = currentPage & 0xFF;
    data[3] = (currentPage >> 8) & 0xFF;
    data[4] = pageCount & 0xFF;
    data[5] = (pageCount >> 8) & 0xFF;
    f.write(data, 6);
    f.close();

    if (Storage.exists(progPath.c_str())) {
      Storage.remove(progPath.c_str());
    }
    Storage.rename(tmpPath.c_str(), progPath.c_str());

    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d", spineIndex,
            currentPage);
  } else {
    LOG_ERR("ERS", "Could not save progress!");
  }
}
void EpubReaderActivity::flushProgressIfNeeded(const bool force) {
  if (!epub || !section || section->pageCount == 0) {
    return;
  }
  if (!progressDirty) {
    return;
  }

  const auto now = millis();
  if (!force && now - lastProgressChangeMs < progressSaveDebounceMs) {
    return;
  }

  saveProgress(currentSpineIndex, section->currentPage, section->pageCount);
  lastSavedSpineIndex = currentSpineIndex;
  lastSavedPage = section->currentPage;
  lastSavedPageCount = section->pageCount;
  progressDirty = false;
}

void EpubReaderActivity::addSessionPagesRead(const uint32_t amount) {
  APP_STATE.sessionPagesRead += amount;
}

void EpubReaderActivity::renderContents(const Page& page,
                                        const int orientedMarginTop,
                                        const int orientedMarginRight,
                                        const int orientedMarginBottom,
                                        const int orientedMarginLeft,
                                        const StatusBarLayout& statusBarLayout) {
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.setTextRenderStyle(SETTINGS.textRenderMode);

  const int viewportHeight =
      renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int contentY = orientedMarginTop;

  // Two-pass font prewarm: scan pass collects text, then decompress needed glyphs.
  // The actual render must happen inside the scope so page buffers stay alive.
  auto* fcm = renderer.getFontCacheManager();
  if (fcm) {
    auto scope = fcm->createPrewarmScope();
    page.render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, contentY);  // scan pass
    scope.endScanAndPrewarm();
    page.render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, contentY);  // actual render
  } else {
    page.render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, contentY);
  }
  if (SETTINGS.debugBorders) {
    DrawUtils::drawDottedRect(renderer, orientedMarginLeft, orientedMarginTop,
                   renderer.getScreenWidth() - orientedMarginLeft -
                       orientedMarginRight,
                   viewportHeight);
  }

  renderStatusBar(statusBarLayout, orientedMarginRight, orientedMarginBottom,
                  orientedMarginLeft);

  const bool pageHasImages = page.hasImages();

  if (pagesUntilFullRefresh <= 1 || pageHasImages) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Apply hardware grayscale overlay for pages with images.
  // This uses the same LSB/MSB technique as sleep wallpapers to render
  // true 4-level grayscale, making photographs much more visible on e-ink.
  if (pageHasImages && renderer.storeBwBuffer()) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page.renderImages(renderer, orientedMarginLeft, contentY);
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page.renderImages(renderer, orientedMarginLeft, contentY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.restoreBwBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  renderer.setTextRenderStyle(0);
}

void EpubReaderActivity::renderStatusBar(const StatusBarLayout& statusBarLayout,
                                         const int orientedMarginRight,
                                         const int orientedMarginBottom,
                                         const int orientedMarginLeft) {
  auto metrics = UITheme::getInstance().getMetrics();
  (void)orientedMarginRight;
  (void)orientedMarginBottom;

  if (!SETTINGS.statusBarEnabled) {
    return;
  }
  const bool showBattery = SETTINGS.statusBarShowBattery;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage ==
      CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;
  constexpr int statusItemGap = 12;
  const auto screenHeight = renderer.getScreenHeight();
  const int usableWidth = statusBarLayout.usableWidth;
  const int statusTopInset = getStatusTopInset(renderer);
  const int statusBottomInset = getStatusBottomInset(renderer);
  const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
  const int progressBarHeight = SETTINGS.getStatusBarProgressBarHeight();

  const auto renderBand = [&](const int bandTopY, const int reservedHeight,
                              const bool renderTopBand) {
    if (reservedHeight <= 0) {
      return;
    }

    const bool showBandBattery =
        showBattery &&
        showBatteryPercentage &&
        (statusBarItemIsTop(SETTINGS.statusBarBatteryPosition) ==
         renderTopBand);
    const bool showBandPageCounter =
        !statusBarLayout.pageCounterText.empty() &&
        (statusTextPositionIsTop(SETTINGS.statusBarPageCounterPosition) ==
         renderTopBand);
    const bool showBandBookPercentage =
        !statusBarLayout.bookPercentageText.empty() &&
        (statusTextPositionIsTop(SETTINGS.statusBarBookPercentagePosition) ==
         renderTopBand);
    const bool showBandChapterPercentage =
        !statusBarLayout.chapterPercentageText.empty() &&
        (statusTextPositionIsTop(
             SETTINGS.statusBarChapterPercentagePosition) == renderTopBand);
    const bool showBandProgressText = showBandPageCounter ||
                                      showBandBookPercentage ||
                                      showBandChapterPercentage;
    const bool showBandTitle =
        SETTINGS.statusBarShowChapterTitle &&
        !statusBarLayout.titleLines.empty() &&
        (statusBarItemIsTop(SETTINGS.statusBarTitlePosition) == renderTopBand);
    const bool showBandBookBar =
        SETTINGS.statusBarShowBookBar &&
        (statusBarItemIsTop(SETTINGS.statusBarBookBarPosition) ==
         renderTopBand);
    const bool showBandChapterBar =
        SETTINGS.statusBarShowChapterBar &&
        (statusBarItemIsTop(SETTINGS.statusBarChapterBarPosition) ==
         renderTopBand);
    const bool showStatusTextRow = showBandBattery || showBandProgressText;
    const int titleLineCount =
        showBandTitle ? static_cast<int>(statusBarLayout.titleLines.size()) : 0;
    const int textBlockHeight = computeStatusTextBlockHeight(
        renderer, showStatusTextRow, titleLineCount);
    const int activeBars =
        (showBandBookBar ? 1 : 0) + (showBandChapterBar ? 1 : 0);
    const int barsHeight = computeStatusBarsHeight(
        showBandBookBar, showBandChapterBar, progressBarHeight,
        textBlockHeight > 0);
    const int renderedBarsHeight = activeBars * progressBarHeight;

    if (SETTINGS.debugBorders) {
      DrawUtils::drawDottedRect(renderer, orientedMarginLeft, bandTopY,
                     renderer.getScreenWidth() - orientedMarginLeft -
                         orientedMarginRight,
                     reservedHeight);
    }

    int currentTextY = bandTopY;
    if (textBlockHeight > 0) {
      currentTextY += statusTextTopPadding;
      if (renderTopBand && barsHeight > 0) {
        currentTextY += barsHeight;
      }
    }

    const int batteryWidth =
        showBandBattery
            ? renderer.getTextWidth(SMALL_FONT_ID, "100%")
            : 0;
    int currentX =
        orientedMarginLeft + std::max(0, (usableWidth - batteryWidth) / 2);

    if (showBandBattery) {
      GUI.drawBatteryLeft(
          renderer,
          Rect{currentX, currentTextY, metrics.batteryWidth,
               metrics.batteryHeight},
          showBatteryPercentage);
      currentX += batteryWidth + statusItemGap;
    }

    if (showBandProgressText) {
      struct TextEntry {
        const std::string* text;
        int width;
      };
      std::vector<TextEntry> leftItems;
      std::vector<TextEntry> centerItems;
      std::vector<TextEntry> rightItems;
      const auto addItem = [&](const bool enabled, const std::string& text,
                               const int width, const uint8_t position) {
        if (!enabled) {
          return;
        }
        TextEntry entry{&text, width};
        switch (statusTextPositionHorizontalSlot(position)) {
          case 0:
            leftItems.push_back(entry);
            break;
          case 2:
            rightItems.push_back(entry);
            break;
          case 1:
          default:
            centerItems.push_back(entry);
            break;
        }
      };
      addItem(showBandPageCounter, statusBarLayout.pageCounterText,
              statusBarLayout.pageCounterTextWidth,
              SETTINGS.statusBarPageCounterPosition);
      addItem(showBandBookPercentage, statusBarLayout.bookPercentageText,
              statusBarLayout.bookPercentageTextWidth,
              SETTINGS.statusBarBookPercentagePosition);
      addItem(showBandChapterPercentage, statusBarLayout.chapterPercentageText,
              statusBarLayout.chapterPercentageTextWidth,
              SETTINGS.statusBarChapterPercentagePosition);

      const auto drawGroup = [&](const std::vector<TextEntry>& items,
                                 const int startX) {
        int x = startX;
        for (size_t i = 0; i < items.size(); i++) {
          if (i > 0) {
            x += statusItemGap;
          }
          renderer.drawText(SMALL_FONT_ID, x, currentTextY,
                            items[i].text->c_str());
          x += items[i].width;
        }
      };
      const auto groupWidth = [&](const std::vector<TextEntry>& items) {
        int width = 0;
        for (size_t i = 0; i < items.size(); i++) {
          if (i > 0) {
            width += statusItemGap;
          }
          width += items[i].width;
        }
        return width;
      };

      const int leftGroupWidth = groupWidth(leftItems);
      const int centerGroupWidth = groupWidth(centerItems);
      const int rightGroupWidth = groupWidth(rightItems);
      if (leftGroupWidth > 0) {
        drawGroup(leftItems, orientedMarginLeft);
      }
      if (centerGroupWidth > 0) {
        drawGroup(centerItems,
                  orientedMarginLeft +
                      std::max(0, (usableWidth - centerGroupWidth) / 2));
      }
      if (rightGroupWidth > 0) {
        drawGroup(
            rightItems,
            orientedMarginLeft + std::max(0, usableWidth - rightGroupWidth));
      }
    }

    int titleY = currentTextY;
    if (showStatusTextRow) {
      titleY += textHeight + statusTextLineGap;
    }
    if (showBandTitle) {
      const int lineStep = textHeight + statusTextLineGap;
      for (size_t i = 0; i < statusBarLayout.titleLines.size(); i++) {
        const int titleWidth =
            (i < statusBarLayout.titleLineWidths.size())
                ? statusBarLayout.titleLineWidths[i]
                : renderer.getTextWidth(SMALL_FONT_ID,
                                        statusBarLayout.titleLines[i].c_str());
        const int titleX =
            orientedMarginLeft + std::max(0, (usableWidth - titleWidth) / 2);
        renderer.drawText(SMALL_FONT_ID, titleX,
                          titleY + static_cast<int>(i) * lineStep,
                          statusBarLayout.titleLines[i].c_str());
      }
    }

    if (barsHeight <= 0) {
      return;
    }

    int barIndex = 0;
    int currentBarY = renderTopBand ? bandTopY
                                    : bandTopY + reservedHeight -
                                          renderedBarsHeight;
    const auto drawBandBar = [&](const size_t progressPercent) {
      const bool isFirstBar = barIndex == 0;
      const bool isLastBar = barIndex == activeBars - 1;
      int barY = currentBarY + barIndex * progressBarHeight;
      int barDrawHeight = progressBarHeight;
      if (renderTopBand && isFirstBar) {
        barY -= statusTopInset;
        barDrawHeight += statusTopInset;
      }
      if (!renderTopBand && isLastBar) {
        barDrawHeight += statusBottomInset;
      }
      drawStyledProgressBar(renderer, progressPercent, barY, barDrawHeight);
      barIndex++;
    };

    if (showBandBookBar) {
      drawBandBar(static_cast<size_t>(statusBarLayout.bookProgress));
    }
    if (showBandChapterBar) {
      drawBandBar(static_cast<size_t>(statusBarLayout.chapterProgress));
    }
  };

  renderBand(statusTopInset, statusBarLayout.topReservedHeight, true);
  renderBand(screenHeight - statusBottomInset - statusBarLayout.bottomReservedHeight,
             statusBarLayout.bottomReservedHeight, false);
}
