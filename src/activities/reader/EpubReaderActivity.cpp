#include "EpubReaderActivity.h"

#include <Epub/Page.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <climits>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// pagesPerRefresh now comes from SETTINGS.getRefreshFrequency()
constexpr unsigned long skipChapterMs = 700;
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long progressSaveDebounceMs = 800;
constexpr int progressBarMarginTop = 1;
constexpr int recentSwitcherRows = 8;
constexpr int statusTextTopPadding = 1;
constexpr int statusTextToBarsGap = 1;

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

void drawStyledProgressBar(const GfxRenderer& renderer, const size_t progressPercent, const int levelFromBottom) {
  int vieweableMarginTop, vieweableMarginRight, vieweableMarginBottom, vieweableMarginLeft;
  renderer.getOrientedViewableTRBL(&vieweableMarginTop, &vieweableMarginRight, &vieweableMarginBottom,
                                   &vieweableMarginLeft);

  const int progressBarMaxWidth = renderer.getScreenWidth() - vieweableMarginLeft - vieweableMarginRight;
  const int barHeight = SETTINGS.getStatusBarProgressBarHeight();
  constexpr int barGap = 0;
  const int stackedOffset = levelFromBottom * (barHeight + barGap);
  const int progressBarY = renderer.getScreenHeight() - vieweableMarginBottom - barHeight - stackedOffset;
  const int progressBarHeight = (levelFromBottom == 0) ? (barHeight + vieweableMarginBottom) : barHeight;
  const int barWidth = progressBarMaxWidth * static_cast<int>(progressPercent) / 100;

  renderer.fillRect(vieweableMarginLeft, progressBarY, barWidth, progressBarHeight, true);
}

void normalizeReaderMargins(int* top, int* right, int* bottom, int* left) {
  const int vertical = std::max(*top, *bottom);
  const int horizontal = std::max(*left, *right);
  *top = vertical;
  *bottom = vertical;
  *left = horizontal;
  *right = horizontal;
}

int getStatusBottomInset(const GfxRenderer& renderer) {
  int baseTop, baseRight, baseBottom, baseLeft;
  renderer.getOrientedViewableTRBL(&baseTop, &baseRight, &baseBottom, &baseLeft);
  return baseBottom;
}

int computeStatusBarsHeight(const bool showBookProgressBar, const bool showChapterProgressBar,
                            const int statusBarProgressHeight) {
  const int activeBars = (showBookProgressBar ? 1 : 0) + (showChapterProgressBar ? 1 : 0);
  if (activeBars == 0) {
    return 0;
  }
  constexpr int barGap = 0;
  return activeBars * statusBarProgressHeight + (activeBars - 1) * barGap + progressBarMarginTop;
}

int computeStatusBarReservedHeight(const GfxRenderer& renderer, const bool showBookProgressBar,
                                   const bool showChapterProgressBar) {
  const int statusTextHeight = renderer.getTextHeight(SMALL_FONT_ID);
  const int barsHeight = computeStatusBarsHeight(showBookProgressBar, showChapterProgressBar,
                                                 SETTINGS.getStatusBarProgressBarHeight());
  return statusTextTopPadding + statusTextHeight + statusTextToBarsGap + barsHeight;
}

int computeEpubPageVerticalOffset(const Page& page, const GfxRenderer& renderer, const int fontId,
                                  const int viewportHeight) {
  if (page.elements.empty() || viewportHeight <= 0) {
    return 0;
  }

  const int lineHeight = renderer.getLineHeight(fontId);
  int minY = INT_MAX;
  int maxY = INT_MIN;

  for (const auto& element : page.elements) {
    if (!element) {
      continue;
    }

    const int yTop = element->yPos;
    int yBottom = yTop + lineHeight;
    if (element->getTag() == TAG_PageImage) {
      const auto* image = static_cast<PageImage*>(element.get());
      yBottom = yTop + image->getHeight();
    }

    minY = std::min(minY, yTop);
    maxY = std::max(maxY, yBottom);
  }

  if (minY == INT_MAX || maxY <= minY) {
    return 0;
  }

  const int contentHeight = maxY - minY;
  if (contentHeight >= viewportHeight) {
    return 0;
  }

  const int desiredTop = (viewportHeight - contentHeight) / 2;
  return desiredTop - minY;
}

// Apply the logical reader orientation to the renderer.
// This centralizes orientation mapping so we don't duplicate switch logic elsewhere.
void applyReaderOrientation(GfxRenderer& renderer, const uint8_t orientation) {
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
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

}  // namespace

void EpubReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!epub) {
    return;
  }

  // Configure screen orientation based on settings
  // NOTE: This affects layout math and must be applied before any render calls.
  applyReaderOrientation(renderer, SETTINGS.orientation);

  epub->setupCacheDir();

  FsFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
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
  // We may want a better condition to detect if we are opening for the first time.
  // This will trigger if the book is re-opened at Chapter 0.
  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  // Save current epub as last opened epub and add to recent books
  APP_STATE.openEpubPath = epub->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), epub->getThumbBmpPath());

  // Trigger first update
  requestUpdate();
}

void EpubReaderActivity::onExit() {
  flushProgressIfNeeded(true);
  ActivityWithSubactivity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  section.reset();
  epub.reset();
}

void EpubReaderActivity::loop() {
  flushProgressIfNeeded(false);

  // Pass input responsibility to sub activity if exists
  if (subActivity) {
    subActivity->loop();
    // Deferred exit: process after subActivity->loop() returns to avoid use-after-free
    if (pendingSubactivityExit) {
      pendingSubactivityExit = false;
      exitActivity();
      requestUpdate();
      skipNextButtonCheck = true;  // Skip button processing to ignore stale events
    }
    // Deferred go home: process after subActivity->loop() returns to avoid race condition
    if (pendingGoHome) {
      pendingGoHome = false;
      exitActivity();
      if (onGoHome) {
        onGoHome();
      }
      return;  // Don't access 'this' after callback
    }
    return;
  }

  // Handle pending go home when no subactivity (e.g., from long press back)
  if (pendingGoHome) {
    pendingGoHome = false;
    if (onGoHome) {
      onGoHome();
    }
    return;  // Don't access 'this' after callback
  }
  if (pendingGoLibrary) {
    pendingGoLibrary = false;
    if (onGoBack) {
      onGoBack();
    }
    return;  // Don't access 'this' after callback
  }

  if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    confirmLongPressHandled = false;
  }

  if (recentSwitcherOpen) {
    const bool prevTriggered = mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                               mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool nextTriggered = mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
                               mappedInput.wasReleased(MappedInputManager::Button::Right);
    if (prevTriggered && !recentSwitcherBooks.empty()) {
      recentSwitcherSelection =
          (recentSwitcherSelection + static_cast<int>(recentSwitcherBooks.size()) - 1) % recentSwitcherBooks.size();
      requestUpdate();
      return;
    }
    if (nextTriggered && !recentSwitcherBooks.empty()) {
      recentSwitcherSelection = (recentSwitcherSelection + 1) % recentSwitcherBooks.size();
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !recentSwitcherBooks.empty()) {
      const std::string selectedPath = recentSwitcherBooks[recentSwitcherSelection].path;
      recentSwitcherOpen = false;
      if (!selectedPath.empty()) {
        onOpenBook(selectedPath);
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
      recentSwitcherOpen = false;
      requestUpdate();
      return;
    }
    return;
  }

  // Skip button processing after returning from subactivity
  // This prevents stale button release events from triggering actions
  // We wait until: (1) all relevant buttons are released, AND (2) wasReleased events have been cleared
  if (skipNextButtonCheck) {
    const bool confirmCleared = !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                                !mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    const bool backCleared = !mappedInput.isPressed(MappedInputManager::Button::Back) &&
                             !mappedInput.wasReleased(MappedInputManager::Button::Back);
    if (confirmCleared && backCleared) {
      skipNextButtonCheck = false;
    }
    return;
  }

  // Enter reader menu activity.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (suppressNextConfirmRelease) {
      suppressNextConfirmRelease = false;
      return;
    }
    const int currentPage = section ? section->currentPage + 1 : 0;
    const int totalPages = section ? section->pageCount : 0;
    float bookProgress = 0.0f;
    if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
      bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
    }
    const int bookProgressPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
    exitActivity();
    enterNewActivity(new EpubReaderMenuActivity(
        this->renderer, this->mappedInput, epub->getTitle(), currentPage, totalPages, bookProgressPercent,
        SETTINGS.orientation, [this](const uint8_t orientation) { onReaderMenuBack(orientation); },
        [this](EpubReaderMenuActivity::MenuAction action) { onReaderMenuConfirm(action); }));
  }

  // Long press CONFIRM (1s+) toggles orientation: Portrait <-> Landscape CCW.
  if (!confirmLongPressHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= goHomeMs) {
    confirmLongPressHandled = true;
    suppressNextConfirmRelease = true;
    const uint8_t nextOrientation = (SETTINGS.orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CCW)
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

  // When long-press chapter skip is disabled, turn pages on press instead of release.
  const bool usePressForPageTurn = !SETTINGS.longPressChapterSkip;
  const bool prevTriggered = usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasReleased(MappedInputManager::Button::Left));
  const bool powerPageTurn = SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered = usePressForPageTurn
                                 ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Right))
                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasReleased(MappedInputManager::Button::Right));

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // any botton press when at end of the book goes back to the last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = UINT16_MAX;
    requestUpdate();
    return;
  }

  const bool skipChapter = SETTINGS.longPressChapterSkip && mappedInput.getHeldTime() > skipChapterMs;

  if (skipChapter) {
    // We don't want to delete the section mid-render, so grab the semaphore
    {
      RenderLock lock(*this);
      nextPageNumber = 0;
      currentSpineIndex = nextTriggered ? currentSpineIndex + 1 : currentSpineIndex - 1;
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
    } else {
      // We don't want to delete the section mid-render, so grab the semaphore
      {
        RenderLock lock(*this);
        nextPageNumber = UINT16_MAX;
        currentSpineIndex--;
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
        section.reset();
      }
    }
    requestUpdate();
  }
}

void EpubReaderActivity::onReaderMenuBack(const uint8_t orientation) {
  exitActivity();
  // Apply the user-selected orientation when the menu is dismissed.
  // This ensures the menu can be navigated without immediately rotating the screen.
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
  // Use an overflow-safe computation: (bookSize / 100) * percent + (bookSize % 100) * percent / 100
  size_t targetSize =
      (bookSize / 100) * static_cast<size_t>(percent) + (bookSize % 100) * static_cast<size_t>(percent) / 100;
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
  const size_t spineSize = (cumulative > prevCumulative) ? (cumulative - prevCumulative) : 0;
  // Store a normalized position within the spine so it can be applied once loaded.
  pendingSpineProgress =
      (spineSize == 0) ? 0.0f : static_cast<float>(targetSize - prevCumulative) / static_cast<float>(spineSize);
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

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      // Calculate values BEFORE we start destroying things
      const int currentP = section ? section->currentPage : 0;
      const int totalP = section ? section->pageCount : 0;
      const int spineIdx = currentSpineIndex;
      const std::string path = epub->getPath();

      // 1. Close the menu
      exitActivity();

      // 2. Open the Chapter Selector
      enterNewActivity(new EpubReaderChapterSelectionActivity(
          this->renderer, this->mappedInput, epub, path, spineIdx, currentP, totalP,
          [this] {
            exitActivity();
            requestUpdate();
          },
          [this](const int newSpineIndex) {
            if (currentSpineIndex != newSpineIndex) {
              currentSpineIndex = newSpineIndex;
              nextPageNumber = 0;
              section.reset();
            }
            exitActivity();
            requestUpdate();
          },
          [this](const int newSpineIndex, const int newPage) {
            if (currentSpineIndex != newSpineIndex || (section && section->currentPage != newPage)) {
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
      // Launch the slider-based percent selector and return here on confirm/cancel.
      float bookProgress = 0.0f;
      if (epub && epub->getBookSize() > 0 && section && section->pageCount > 0) {
        const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(section->pageCount);
        bookProgress = epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f;
      }
      const int initialPercent = clampPercent(static_cast<int>(bookProgress + 0.5f));
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
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock(*this);
        if (epub) {
          // 2. BACKUP: Read current progress
          // We use the current variables that track our position
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;

          section.reset();
          // 3. WIPE: Clear the cache directory
          epub->clearCache();

          // 4. RESTORE: Re-setup the directory and rewrite the progress file
          epub->setupCacheDir();

          saveProgress(backupSpine, backupPage, backupPageCount);
          lastSavedSpineIndex = backupSpine;
          lastSavedPage = backupPage;
          lastSavedPageCount = backupPageCount;
          lastObservedSpineIndex = backupSpine;
          lastObservedPage = backupPage;
          lastObservedPageCount = backupPageCount;
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
            renderer, mappedInput, epub, epub->getPath(), currentSpineIndex, currentPage, totalPages,
            [this]() {
              // On cancel - defer exit to avoid use-after-free
              pendingSubactivityExit = true;
            },
            [this](int newSpineIndex, int newPage) {
              // On sync complete - update position and defer exit
              if (currentSpineIndex != newSpineIndex || (section && section->currentPage != newPage)) {
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
        LOG_DBG("ERS", "Delete book '%s': %s", deletingPath.c_str(), removed ? "ok" : "failed");
      }
      pendingGoLibrary = true;
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

    // Persist the selection so the reader keeps the new orientation on next launch.
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();

    // Update renderer orientation to match the new logical coordinate system.
    applyReaderOrientation(renderer, SETTINGS.orientation);

    // Reset section to force re-layout in the new orientation.
    section.reset();
  }
}

// TODO: Failure handling
void EpubReaderActivity::render(Activity::RenderLock&& lock) {
  if (!epub) {
    return;
  }

  if (recentSwitcherOpen) {
    renderRecentSwitcher();
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
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_END_OF_BOOK), true, EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  // Apply screen viewable areas and additional padding
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  normalizeReaderMargins(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom, &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMarginTop;
  orientedMarginLeft += SETTINGS.screenMarginHorizontal;
  orientedMarginRight += SETTINGS.screenMarginHorizontal;
  orientedMarginBottom += SETTINGS.screenMarginBottom;

  int statusBarReserved = 0;
  if (SETTINGS.statusBarEnabled) {
    statusBarReserved = computeStatusBarReservedHeight(renderer, SETTINGS.statusBarShowBookBar,
                                                       SETTINGS.statusBarShowChapterBar);
  }

  if (!section) {
    const auto filepath = epub->getSpineItem(currentSpineIndex).href;
    LOG_DBG("ERS", "Loading file: %s, index: %d", filepath.c_str(), currentSpineIndex);
    section = std::unique_ptr<Section>(new Section(epub, currentSpineIndex, renderer));

    const uint16_t viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
    const uint16_t viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom - statusBarReserved;

    if (!section->loadSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                  SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                  viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle)) {
      LOG_DBG("ERS", "Cache not found, building...");

      const auto popupFn = [this]() { GUI.drawPopup(renderer, tr(STR_INDEXING)); };

      if (!section->createSectionFile(SETTINGS.getReaderFontId(), SETTINGS.getReaderLineCompression(),
                                      SETTINGS.extraParagraphSpacing, SETTINGS.paragraphAlignment, viewportWidth,
                                      viewportHeight, SETTINGS.hyphenationEnabled, SETTINGS.embeddedStyle, popupFn)) {
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

    // handles changes in reader settings and reset to approximate position based on cached progress
    if (cachedChapterTotalPageCount > 0) {
      // only goes to relative position if spine index matches cached value
      if (currentSpineIndex == cachedSpineIndex && section->pageCount != cachedChapterTotalPageCount) {
        float progress = static_cast<float>(section->currentPage) / static_cast<float>(cachedChapterTotalPageCount);
        int newPage = static_cast<int>(progress * section->pageCount);
        section->currentPage = newPage;
      }
      cachedChapterTotalPageCount = 0;  // resets to 0 to prevent reading cached progress again
    }

    if (pendingPercentJump && section->pageCount > 0) {
      // Apply the pending percent jump now that we know the new section's page count.
      int newPage = static_cast<int>(pendingSpineProgress * static_cast<float>(section->pageCount));
      if (newPage >= section->pageCount) {
        newPage = section->pageCount - 1;
      }
      section->currentPage = newPage;
      pendingPercentJump = false;
    }
  }

  renderer.clearScreen();

  if (section->pageCount == 0) {
    LOG_DBG("ERS", "No pages to render");
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::REGULAR);
    renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    renderer.displayBuffer();
    return;
  }

  if (section->currentPage < 0 || section->currentPage >= section->pageCount) {
    LOG_DBG("ERS", "Page out of bounds: %d (max %d), clamping", section->currentPage, section->pageCount);
    if (section->currentPage < 0) {
      section->currentPage = 0;
    } else {
      section->currentPage = section->pageCount - 1;
    }
  }

  {
    auto p = section->loadPageFromSectionFile();
    if (!p) {
      LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
      section->clearCache();
      section.reset();
      requestUpdate();  // Try again after clearing cache
      // TODO: prevent infinite loop if the page keeps failing to load for some reason
      return;
    }
    const auto start = millis();
    renderContents(std::move(p), orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
    LOG_DBG("ERS", "Rendered page in %dms", millis() - start);
  }
  if (lastObservedSpineIndex != currentSpineIndex || lastObservedPage != section->currentPage ||
      lastObservedPageCount != section->pageCount) {
    lastObservedSpineIndex = currentSpineIndex;
    lastObservedPage = section->currentPage;
    lastObservedPageCount = section->pageCount;
    if (lastSavedSpineIndex != currentSpineIndex || lastSavedPage != section->currentPage ||
        lastSavedPageCount != section->pageCount) {
      progressDirty = true;
      lastProgressChangeMs = millis();
    }
  }

  flushProgressIfNeeded(false);
}

void EpubReaderActivity::loadRecentSwitcherBooks() {
  recentSwitcherBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  for (const auto& book : books) {
    if (recentSwitcherBooks.size() >= recentSwitcherRows) {
      break;
    }
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    recentSwitcherBooks.push_back(book);
  }
  recentSwitcherSelection = 0;
}

void EpubReaderActivity::renderRecentSwitcher() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int popupX = 18;
  const int popupY = 24;
  const int popupW = screenW - popupX * 2;
  const int popupH = screenH - popupY * 2;
  const int titleY = popupY + 8;
  const int rowsY = popupY + 30;
  const int rowsH = popupH - 40;
  const int rowH = rowsH / recentSwitcherRows;

  renderer.clearScreen();
  renderer.drawRect(popupX, popupY, popupW, popupH, true);
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, tr(STR_MENU_RECENT_BOOKS), true, EpdFontFamily::REGULAR);

  for (int i = 0; i < recentSwitcherRows; i++) {
    const int rowY = rowsY + i * rowH;
    const bool hasBook = i < static_cast<int>(recentSwitcherBooks.size());
    const bool selected = hasBook && i == recentSwitcherSelection;

    if (selected) {
      renderer.fillRect(popupX + 8, rowY, popupW - 16, rowH - 2, true);
      renderer.drawRect(popupX + 10, rowY + 2, popupW - 20, rowH - 6, false);
    } else {
      renderer.drawRect(popupX + 8, rowY, popupW - 16, rowH - 2, true);
    }

    std::string title = " ";
    if (hasBook) {
      title = recentSwitcherBooks[i].title;
      if (title.empty()) {
        const size_t lastSlash = recentSwitcherBooks[i].path.find_last_of('/');
        title = (lastSlash == std::string::npos) ? recentSwitcherBooks[i].path
                                                 : recentSwitcherBooks[i].path.substr(lastSlash + 1);
      }
      title = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), popupW - 28);
    }
    renderer.drawText(UI_10_FONT_ID, popupX + 14, rowY + 3, title.c_str(), !selected);
  }

  renderer.displayBuffer();
}

void EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  FsFile f;
  if (Storage.openFileForWrite("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[6];
    data[0] = spineIndex & 0xFF;
    data[1] = (spineIndex >> 8) & 0xFF;
    data[2] = currentPage & 0xFF;
    data[3] = (currentPage >> 8) & 0xFF;
    data[4] = pageCount & 0xFF;
    data[5] = (pageCount >> 8) & 0xFF;
    f.write(data, 6);
    f.close();
    LOG_DBG("ERS", "Progress saved: Chapter %d, Page %d", spineIndex, currentPage);
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
void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  // Reader text AA is intentionally disabled: render EPUB pages in BW only.
  bool forceFullRefresh = false;
  const int statusBarReserved = SETTINGS.statusBarEnabled
                                    ? computeStatusBarReservedHeight(renderer, SETTINGS.statusBarShowBookBar,
                                                                     SETTINGS.statusBarShowChapterBar)
                                    : 0;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom - statusBarReserved;
  const int verticalOffset = computeEpubPageVerticalOffset(*page, renderer, SETTINGS.getReaderFontId(), viewportHeight);
  const int contentY = orientedMarginTop + verticalOffset;

  page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, contentY);
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);
  if (forceFullRefresh || pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Save bw buffer to reset buffer state after grayscale data sync
  renderer.storeBwBuffer();

  // Reader text AA is intentionally disabled (BW only in reader).
  if (false) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, contentY);
    renderer.copyGrayscaleLsbBuffers();

    // Render and copy to MSB buffer
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer, SETTINGS.getReaderFontId(), orientedMarginLeft, contentY);
    renderer.copyGrayscaleMsbBuffers();

    // display grayscale part
    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }

  // restore the bw data
  renderer.restoreBwBuffer();
}

void EpubReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                         const int orientedMarginLeft) {
  auto metrics = UITheme::getInstance().getMetrics();
  (void)orientedMarginBottom;

  if (!SETTINGS.statusBarEnabled) {
    return;
  }
  const bool showBookProgressBar = SETTINGS.statusBarShowBookBar;
  const bool showChapterProgressBar = SETTINGS.statusBarShowChapterBar;
  const bool showStatusTopLine = SETTINGS.statusBarTopLine;
  const bool showPageCounter = SETTINGS.statusBarShowPageCounter;
  const bool showBookPercentage = SETTINGS.statusBarShowBookPercentage;
  const bool showChapterPercentage = SETTINGS.statusBarShowChapterPercentage;
  const bool showBattery = SETTINGS.statusBarShowBattery;
  const bool showChapterTitle = SETTINGS.statusBarShowChapterTitle;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;
  constexpr int statusItemGap = 12;

  // Position status bar near the bottom of the logical screen, regardless of orientation
  const auto screenHeight = renderer.getScreenHeight();
  const int statusBarProgressHeight = SETTINGS.getStatusBarProgressBarHeight();
  const int barsHeight = computeStatusBarsHeight(showBookProgressBar, showChapterProgressBar, statusBarProgressHeight);
  const int statusBarReserved = computeStatusBarReservedHeight(renderer, showBookProgressBar, showChapterProgressBar);
  const int statusBottomInset = getStatusBottomInset(renderer);
  const int statusTopY = screenHeight - statusBottomInset - statusBarReserved;
  const auto textY = statusTopY + statusTextTopPadding;
  if (showStatusTopLine) {
    renderer.drawLine(orientedMarginLeft, statusTopY, renderer.getScreenWidth() - orientedMarginRight - 1, statusTopY,
                      true);
  }
  std::string progressText;
  int progressTextWidth = 0;

  // Calculate progress in book
  const float sectionChapterProg = static_cast<float>(section->currentPage) / section->pageCount;
  const float bookProgress = epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100;

  const float chapterProgress = (section->pageCount > 0) ? (static_cast<float>(section->currentPage + 1) / section->pageCount) * 100 : 0;

  if (showPageCounter || showBookPercentage || showChapterPercentage) {
    char progressStr[64] = {0};
    int offset = 0;
    if (showPageCounter) {
      offset += snprintf(progressStr + offset, sizeof(progressStr) - offset, "%d/%d", section->currentPage + 1, section->pageCount);
    }
    if (showBookPercentage) {
      offset += snprintf(progressStr + offset, sizeof(progressStr) - offset, "%sB:%.0f%%", (offset > 0) ? "  " : "", bookProgress);
    }
    if (showChapterPercentage) {
      snprintf(progressStr + offset, sizeof(progressStr) - offset, "%sC:%.0f%%", (offset > 0) ? "  " : "", chapterProgress);
    }
    progressText = progressStr;
    progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str());
  }

  std::string titleText;
  int titleWidth = 0;
  if (showChapterTitle) {
    const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex == -1) {
      titleText = tr(STR_UNNAMED);
    } else {
      titleText = epub->getTocItem(tocIndex).title;
    }
    titleWidth = renderer.getTextWidth(SMALL_FONT_ID, titleText.c_str());
  }

  const int batteryWidth = showBattery
                               ? (metrics.batteryWidth + (showBatteryPercentage
                                                              ? (4 + renderer.getTextWidth(SMALL_FONT_ID, "100%"))
                                                              : 0))
                               : 0;
  const int progressWidth = progressText.empty() ? 0 : progressTextWidth;
  int visibleItems = 0;
  visibleItems += showBattery ? 1 : 0;
  visibleItems += showChapterTitle ? 1 : 0;
  visibleItems += progressText.empty() ? 0 : 1;
  const int groupGaps = std::max(0, visibleItems - 1) * statusItemGap;
  const int usableWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  if (showChapterTitle) {
    const int fixedWidth = batteryWidth + progressWidth + groupGaps;
    const int maxTitleWidth = std::max(0, usableWidth - fixedWidth);
    if (titleWidth > maxTitleWidth) {
      titleText = renderer.truncatedText(SMALL_FONT_ID, titleText.c_str(), maxTitleWidth);
      titleWidth = renderer.getTextWidth(SMALL_FONT_ID, titleText.c_str());
    }
  }

  const int totalGroupWidth = batteryWidth + titleWidth + progressWidth + groupGaps;
  int currentX = orientedMarginLeft + std::max(0, (usableWidth - totalGroupWidth) / 2);

  if (showBattery) {
    GUI.drawBatteryLeft(renderer, Rect{currentX, textY, metrics.batteryWidth, metrics.batteryHeight}, showBatteryPercentage);
    currentX += batteryWidth + statusItemGap;
  }

  if (showChapterTitle && !titleText.empty()) {
    renderer.drawText(SMALL_FONT_ID, currentX, textY, titleText.c_str());
    currentX += titleWidth + statusItemGap;
  }

  if (!progressText.empty()) {
    renderer.drawText(SMALL_FONT_ID, currentX, textY, progressText.c_str());
  }

  if (showBookProgressBar) {
    drawStyledProgressBar(renderer, static_cast<size_t>(bookProgress), 0);
  }

  if (showChapterProgressBar) {
    const int chapterLevel = showBookProgressBar ? 1 : 0;
    drawStyledProgressBar(renderer, static_cast<size_t>(chapterProgress), chapterLevel);
  }

}
