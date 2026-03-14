#include "EpubReaderActivity.h"

#include <Arduino.h>
#include <EpdFontFamily.h>
#include <Epub/Page.h>
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
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ReadingThemesActivity.h"
#include "RecentBooksStore.h"
#include "activities/boot_sleep/LastSleepWallpaperActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StatusPopup.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long confirmDoubleTapMs = 350;
constexpr unsigned long progressSaveDebounceMs = 800;
constexpr int progressBarMarginTop = 1;
constexpr int statusTextTopPadding = 2;
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
  StatusPopup::showBottomProgress(renderer, tr(STR_LOADING), 100);
  delay(70);
}

void drawDottedRect(const GfxRenderer &renderer, int x, int y, int w, int h) {
  const int x2 = x + w - 1;
  const int y2 = y + h - 1;
  // Top and bottom edges
  for (int px = x; px <= x2; px += 2) {
    renderer.drawPixel(px, y);
    renderer.drawPixel(px, y2);
  }
  // Left and right edges
  for (int py = y + 1; py < y2; py += 2) {
    renderer.drawPixel(x, py);
    renderer.drawPixel(x2, py);
  }
}

void drawStyledProgressBar(const GfxRenderer &renderer,
                           const size_t progressPercent, const int y,
                           const int height) {
  int vieweableMarginTop, vieweableMarginRight, vieweableMarginBottom,
      vieweableMarginLeft;
  renderer.getOrientedViewableTRBL(&vieweableMarginTop, &vieweableMarginRight,
                                   &vieweableMarginBottom,
                                   &vieweableMarginLeft);
  const int progressBarMaxWidth =
      renderer.getScreenWidth() - vieweableMarginLeft - vieweableMarginRight;
  const int barWidth =
      progressBarMaxWidth * static_cast<int>(progressPercent) / 100;

  renderer.fillRect(vieweableMarginLeft, y, barWidth, height, true);
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
  const int statusTextHeight = renderer.getTextHeight(SMALL_FONT_ID);
  int textBlockHeight = 0;
  if (showStatusTextRow) {
    textBlockHeight += statusTextHeight;
  }
  if (titleLineCount > 0) {
    if (textBlockHeight > 0) {
      textBlockHeight += statusTextLineGap;
    }
    textBlockHeight +=
        titleLineCount * statusTextHeight +
        (titleLineCount - 1) * statusTextLineGap;
  }
  return textBlockHeight;
}

int computeStatusBarsHeight(const bool showBookProgressBar,
                            const bool showChapterProgressBar,
                            const int statusBarProgressHeight,
                            const bool includeTopMargin) {
  const int activeBars =
      (showBookProgressBar ? 1 : 0) + (showChapterProgressBar ? 1 : 0);
  if (activeBars == 0) {
    return 0;
  }
  constexpr int barGap = 0;
  return activeBars * statusBarProgressHeight + (activeBars - 1) * barGap +
         (includeTopMargin ? progressBarMarginTop : 0);
}

std::vector<std::string> wrapStatusText(const GfxRenderer &renderer,
                                        const int fontId,
                                        const std::string &text,
                                        const int maxWidth) {
  if (text.empty()) {
    return {};
  }
  if (maxWidth <= 0) {
    return {text};
  }

  std::vector<std::string> lines;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') {
      i++;
    }
    if (i >= text.size()) {
      break;
    }

    std::string line;
    size_t lineEndPos = i;
    while (lineEndPos < text.size()) {
      size_t wordEnd = lineEndPos;
      while (wordEnd < text.size() && text[wordEnd] != ' ') {
        wordEnd++;
      }
      const std::string word = text.substr(lineEndPos, wordEnd - lineEndPos);
      const std::string candidate = line.empty() ? word : (line + " " + word);

      if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
        line = candidate;
        lineEndPos = wordEnd;
        while (lineEndPos < text.size() && text[lineEndPos] == ' ') {
          lineEndPos++;
        }
        continue;
      }

      if (line.empty()) {
        size_t fit = 1;
        while (fit < word.size() &&
               renderer.getTextWidth(fontId,
                                     word.substr(0, fit + 1).c_str()) <=
                   maxWidth) {
          fit++;
        }
        line = word.substr(0, fit);
        lineEndPos += fit;
      }
      break;
    }

    if (line.empty()) {
      line = renderer.truncatedText(fontId, text.substr(i).c_str(), maxWidth);
      lines.push_back(line);
      break;
    }

    lines.push_back(line);
    i = lineEndPos;
  }

  if (lines.empty()) {
    lines.push_back(renderer.truncatedText(fontId, text.c_str(), maxWidth));
  }
  return lines;
}

int computeStatusBarReservedHeight(const GfxRenderer &renderer,
                                   const bool showStatusTextRow,
                                   const bool showBookProgressBar,
                                   const bool showChapterProgressBar,
                                   const int titleLineCount) {
  const int textBlockHeight = computeStatusTextBlockHeight(
      renderer, showStatusTextRow, titleLineCount);
  const int barsHeight =
      computeStatusBarsHeight(showBookProgressBar, showChapterProgressBar,
                              SETTINGS.getStatusBarProgressBarHeight(),
                              textBlockHeight > 0);
  int reservedHeight = 0;
  if (textBlockHeight > 0) {
    reservedHeight += statusTextTopPadding + textBlockHeight;
  }
  if (barsHeight > 0) {
    if (textBlockHeight > 0) {
      reservedHeight += statusTextToBarsGap;
    }
    reservedHeight += barsHeight;
  }
  return reservedHeight;
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
  cachedTitleLines.clear();
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
    const int tocIndex, const int usableWidth, const bool noTitleTruncation) {
  if (cachedTitleTocIndex == tocIndex && cachedTitleUsableWidth == usableWidth &&
      cachedTitleNoTitleTruncation == noTitleTruncation) {
    return cachedTitleLines;
  }

  std::string titleText = tr(STR_UNNAMED);
  if (tocIndex >= 0 && epub) {
    titleText = epub->formatTocDisplayTitle(tocIndex);
    if (titleText.empty()) {
      titleText = tr(STR_UNNAMED);
    }
  }

  if (noTitleTruncation) {
    cachedTitleLines =
        wrapStatusText(renderer, SMALL_FONT_ID, titleText, usableWidth);
  } else {
    if (renderer.getTextWidth(SMALL_FONT_ID, titleText.c_str()) > usableWidth) {
      titleText = renderer.truncatedText(SMALL_FONT_ID, titleText.c_str(),
                                         usableWidth);
    }
    cachedTitleLines = {titleText};
  }

  cachedTitleTocIndex = tocIndex;
  cachedTitleUsableWidth = usableWidth;
  cachedTitleNoTitleTruncation = noTitleTruncation;
  return cachedTitleLines;
}

EpubReaderActivity::StatusBarLayout EpubReaderActivity::buildStatusBarLayout(
    const int usableWidth, const int topReservedHeight,
    const int bottomReservedHeight) {
  StatusBarLayout layout;
  layout.usableWidth = std::max(0, usableWidth);
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
    const int tocIndex =
        resolveCurrentTocIndex(epub, section.get(), currentSpineIndex);
    layout.titleLines = getStatusBarTitleLines(
        tocIndex, layout.usableWidth, SETTINGS.statusBarNoTitleTruncation);
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

  // Single tap opens menu; double tap toggles reader bold swap mode.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (suppressNextConfirmRelease) {
      suppressNextConfirmRelease = false;
      pendingMenuOpen = false;
      return;
    }
    const unsigned long now = millis();
    if (pendingMenuOpen && now - lastConfirmReleaseMs <= confirmDoubleTapMs) {
      pendingMenuOpen = false;
      toggleReaderBoldSwap();
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
        section.reset();
      }
    }
    requestUpdate();
  } else {
    if (section->currentPage < section->pageCount - 1) {
      section->currentPage++;
      progressDirty = true;
      lastProgressChangeMs = millis();
      flushProgressIfNeeded(true);
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = 0;
        currentSpineIndex++;
        saveProgress(currentSpineIndex, nextPageNumber, 1);
        lastSavedSpineIndex = currentSpineIndex;
        lastSavedPage = nextPageNumber;
        lastSavedPageCount = 1;
        progressDirty = false;
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

void EpubReaderActivity::toggleReaderBoldSwap() {
  flushProgressIfNeeded(true);
  const bool enableSwap = SETTINGS.readerBoldSwap == 0;
  SETTINGS.readerBoldSwap = enableSwap ? 1 : 0;
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("ERS", "Failed to save settings after bold swap toggle");
  }
  EpdFontFamily::setReaderBoldSwapEnabled(enableSwap);

  uint16_t backupSpine = 0;
  uint16_t backupPage = 0;
  uint16_t backupPageCount = 1;
  if (epub) {
    const uint16_t spineCount = epub->getSpineItemsCount();
    if (section && section->pageCount > 0) {
      backupSpine = currentSpineIndex;
      backupPage = section->currentPage;
      backupPageCount = (section->pageCount > 0) ? section->pageCount : 1;
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
  {
    RenderLock lock(*this);
    currentSpineIndex = targetSpineIndex;
    nextPageNumber = 0;
    pendingPercentJump = true;
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
        resolveCurrentTocIndex(epub, section.get(), currentSpineIndex);
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
            currentSpineIndex = newSpineIndex;
            nextPageNumber = 0;
            section.reset();
          }
          exitActivity();
          requestUpdate();
        },
        [this](const int newSpineIndex, const int newPage) {
          if (currentSpineIndex != newSpineIndex ||
              (section && section->currentPage != newPage)) {
            currentSpineIndex = newSpineIndex;
            nextPageNumber = newPage;
            section.reset();
          }
          exitActivity();
          requestUpdate();
        }));

    break;
  }
  case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
    // Launch the slider-based percent selector and return here on
    // confirm/cancel.
    float bookProgress = 0.0f;
    if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) /
                                    static_cast<float>(section->pageCount);
      bookProgress =
          epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
    }
    const int initialPercent =
        clampPercent(static_cast<int>(bookProgress + 0.5f));
    exitActivity();
    enterNewActivity(new EpubReaderPercentSelectionActivity(
        renderer, mappedInput, initialPercent,
        [this](const int percent) {
          // Apply the new position and exit back to the reader.
          jumpToPercent(percent);
          exitActivity();
          requestUpdate();
        },
        [this]() {
          // Cancel selection and return to the reader.
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
  case EpubReaderMenuActivity::MenuAction::READING_THEMES: {
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
    StatusPopup::showBlocking(renderer, "Clearing book cache");
    {
      RenderLock lock(*this);
      if (epub) {
        // Clean cache and reset this book's reading progress to page 1.
        const uint16_t resetSpine = 0;
        const uint16_t resetPage = 0;
        const uint16_t resetPageCount = 1;

        section.reset();
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
    // Defer go home to avoid race condition with display task
    pendingGoHome = true;
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
              currentSpineIndex = newSpineIndex;
              nextPageNumber = newPage;
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
    section.reset();
  }
}

void EpubReaderActivity::reloadCurrentSectionForDisplaySettings() {
  flushProgressIfNeeded(true);
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
    section.reset();
  }
  requestUpdate();
}

// TODO: Failure handling
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

  const int usableWidth =
      renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  int statusBarTopReserved = 0;
  int statusBarBottomReserved = 0;
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
    statusBarTopReserved = computeStatusBarReservedHeight(
        renderer, showTopStatusTextRow,
        SETTINGS.statusBarShowBookBar &&
            statusBarItemIsTop(SETTINGS.statusBarBookBarPosition),
        SETTINGS.statusBarShowChapterBar &&
            statusBarItemIsTop(SETTINGS.statusBarChapterBarPosition),
        topTitleLineCount);
    statusBarBottomReserved = computeStatusBarReservedHeight(
        renderer, showBottomStatusTextRow,
        SETTINGS.statusBarShowBookBar &&
            !statusBarItemIsTop(SETTINGS.statusBarBookBarPosition),
        SETTINGS.statusBarShowChapterBar &&
            !statusBarItemIsTop(SETTINGS.statusBarChapterBarPosition),
        bottomTitleLineCount);
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

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(),
            currentSpineIndex);
    section = std::unique_ptr<Section>(
        new Section(epub, currentSpineIndex, renderer));
    bool builtSection = false;

    const uint16_t viewportWidth =
        renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight =
        renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;

    if (!section->loadSectionFile(
            SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
            SETTINGS.extraParagraphSpacingLevel, SETTINGS.paragraphAlignment,
            viewportWidth, viewportHeight, false,
            SETTINGS.embeddedStyle, SETTINGS.readerBoldSwap != 0)) {
      LOG_DBG("ERS", "Cache not found, building...");
      builtSection = true;

      const auto progressFn = [this](const int progress) {
        StatusPopup::showBottomProgress(renderer, tr(STR_INDEXING_CHAPTER),
                                        progress);
      };

      if (!section->createSectionFile(
              SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
              SETTINGS.extraParagraphSpacingLevel, SETTINGS.paragraphAlignment,
              viewportWidth, viewportHeight, false,
              SETTINGS.embeddedStyle, SETTINGS.readerBoldSwap != 0, progressFn)) {
        LOG_ERR("ERS", "Failed to persist page data to SD");
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

    if (builtSection) {
      finishLoadingBar(renderer);
    }
  }

  renderer.clearScreen();
  const StatusBarLayout statusBarLayout =
      buildStatusBarLayout(usableWidth, statusBarTopReserved,
                           statusBarBottomReserved);

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
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      pageLoadFailCount++;
      LOG_ERR(
          "ERS",
          "Failed to load page from SD - clearing section cache (attempt %d)",
          pageLoadFailCount);
      section->clearCache();
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
    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight,
                   orientedMarginBottom, orientedMarginLeft, statusBarLayout);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
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
  if (!epub || !section || section->pageCount <= 0) {
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

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page,
                                        const int orientedMarginTop,
                                        const int orientedMarginRight,
                                        const int orientedMarginBottom,
                                        const int orientedMarginLeft,
                                        const StatusBarLayout& statusBarLayout) {
  // Reader text AA is intentionally disabled: render EPUB pages in BW only.
  // Force special handling for pages with images when anti-aliasing is on
  bool imagePageWithAA = page->hasImages() && SETTINGS.textAntiAliasing;

  const int viewportHeight =
      renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int contentY = orientedMarginTop;

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft,
               contentY);
  if (SETTINGS.debugBorders) {
    drawDottedRect(renderer, orientedMarginLeft, orientedMarginTop,
                   renderer.getScreenWidth() - orientedMarginLeft -
                       orientedMarginRight,
                   viewportHeight);
  }

  renderStatusBar(statusBarLayout, orientedMarginRight, orientedMarginBottom,
                  orientedMarginLeft);

  if (imagePageWithAA) {
    // Double FAST_REFRESH with selective image blanking
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      renderer.fillRect(imgX + orientedMarginLeft, imgY + contentY, imgW, imgH,
                        false);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

      // Re-render page content to restore images into the blanked area
      page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft,
                   contentY);
      renderStatusBar(statusBarLayout, orientedMarginRight, orientedMarginBottom,
                      orientedMarginLeft);
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  } else if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }
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
      drawDottedRect(renderer, orientedMarginLeft, bandTopY,
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
            ? (metrics.batteryWidth +
               (showBatteryPercentage
                    ? (4 + renderer.getTextWidth(SMALL_FONT_ID, "100%"))
                    : 0))
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
        const int titleWidth = renderer.getTextWidth(
            SMALL_FONT_ID, statusBarLayout.titleLines[i].c_str());
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
