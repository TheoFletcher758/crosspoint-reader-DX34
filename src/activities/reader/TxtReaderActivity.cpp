#include "TxtReaderActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Serialization.h>
#include <Utf8.h>
#include <EpdFontFamily.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long confirmDoubleTapMs = 350;
constexpr unsigned long progressSaveDebounceMs = 800;
constexpr int progressBarMarginTop = 1;
constexpr int recentSwitcherRows = 8;
constexpr size_t CHUNK_SIZE = 8 * 1024;  // 8KB chunk for reading
constexpr int statusTextTopPadding = 1;
constexpr int statusTextToBarsGap = 1;

// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449;  // "TXTI"
constexpr uint8_t CACHE_VERSION = 3;          // Increment when cache format changes

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
}  // namespace

void TxtReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!txt) {
    return;
  }

  // Configure screen orientation based on settings
  switch (SETTINGS.orientation) {
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
  EpdFontFamily::setReaderBoldSwapEnabled(SETTINGS.readerBoldSwap != 0);

  txt->setupCacheDir();

  // Save current txt as last opened file and add to recent books
  auto filePath = txt->getPath();
  auto fileName = filePath.substr(filePath.rfind('/') + 1);
  APP_STATE.openEpubPath = filePath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(filePath, fileName, "", "");

  // Trigger first update
  requestUpdate();
}

void TxtReaderActivity::onExit() {
  flushProgressIfNeeded(true);
  EpdFontFamily::setReaderBoldSwapEnabled(false);
  ActivityWithSubactivity::onExit();

  // Reset orientation back to portrait for the rest of the UI
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  pageOffsets.clear();
  currentPageLines.clear();
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();
  txt.reset();
}

void TxtReaderActivity::loop() {
  flushProgressIfNeeded(false);

  if (subActivity) {
    subActivity->loop();
    return;
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

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mappedInput.getHeldTime() >= goHomeMs) {
      return;
    }
    const unsigned long now = millis();
    if (lastConfirmReleaseMs > 0 && now - lastConfirmReleaseMs <= confirmDoubleTapMs) {
      lastConfirmReleaseMs = 0;
      toggleReaderBoldSwap();
      return;
    }
    lastConfirmReleaseMs = now;
  }

  // Long press CONFIRM (1s+) toggles orientation: Portrait <-> Landscape CCW.
  if (!confirmLongPressHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= goHomeMs) {
    confirmLongPressHandled = true;
    SETTINGS.orientation = (SETTINGS.orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CCW)
                               ? CrossPointSettings::ORIENTATION::PORTRAIT
                               : CrossPointSettings::ORIENTATION::LANDSCAPE_CCW;
    renderer.setOrientation(SETTINGS.orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CCW
                                ? GfxRenderer::Orientation::LandscapeCounterClockwise
                                : GfxRenderer::Orientation::Portrait);
    initialized = false;
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

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    progressDirty = true;
    lastProgressChangeMs = millis();
    flushProgressIfNeeded(true);
    requestUpdate();
  } else if (nextTriggered && currentPage < totalPages - 1) {
    currentPage++;
    progressDirty = true;
    lastProgressChangeMs = millis();
    flushProgressIfNeeded(true);
    requestUpdate();
  }
}

void TxtReaderActivity::toggleReaderBoldSwap() {
  flushProgressIfNeeded(true);
  const bool enableSwap = SETTINGS.readerBoldSwap == 0;
  SETTINGS.readerBoldSwap = enableSwap ? 1 : 0;
  SETTINGS.saveToFile();
  EpdFontFamily::setReaderBoldSwapEnabled(enableSwap);

  if (txt) {
    Storage.remove((txt->getCachePath() + "/index.bin").c_str());
  }
  initialized = false;
  pageOffsets.clear();
  currentPageLines.clear();
  requestUpdate();
}

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  // Store current settings for cache validation
  cachedFontId = SETTINGS.getReaderFontId();
  cachedScreenMarginHorizontal = SETTINGS.screenMarginHorizontal;
  cachedScreenMarginTop = SETTINGS.screenMarginTop;
  cachedScreenMarginBottom = SETTINGS.screenMarginBottom;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  // Calculate viewport dimensions
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  normalizeReaderMargins(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom, &orientedMarginLeft);
  orientedMarginTop += cachedScreenMarginTop;
  orientedMarginLeft += cachedScreenMarginHorizontal;
  orientedMarginRight += cachedScreenMarginHorizontal;
  orientedMarginBottom += cachedScreenMarginBottom;

  int statusBarReserved = 0;
  if (SETTINGS.statusBarEnabled) {
    statusBarReserved = computeStatusBarReservedHeight(renderer, SETTINGS.statusBarShowBookBar,
                                                       SETTINGS.statusBarShowChapterBar);
  }

  viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom - statusBarReserved;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth, viewportHeight, linesPerPage);

  // Try to load cached page index first
  if (!loadPageIndexCache()) {
    // Cache not found, build page index
    buildPageIndex();
    // Save to cache for next time
    savePageIndexCache();
  }

  // Load saved progress
  loadProgress();

  initialized = true;
}

void TxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  pageOffsets.push_back(0);  // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  GUI.drawPopup(renderer, tr(STR_INDEXING));

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      // No progress made, avoid infinite loop
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    // Yield to other tasks periodically
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Built page index: %d pages", totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    LOG_ERR("TRS", "Failed to allocate %zu bytes", chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  // Parse lines from buffer
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    // Find end of line
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    // Check if we have a complete line
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      // Incomplete line and we already have some lines, stop here
      break;
    }

    // Calculate the actual length of line content in the buffer (excluding newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    // Word wrap if needed
    while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
      int lineWidth = renderer.getTextWidth(cachedFontId, line.c_str());

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;  // Consumed entire display content
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextWidth(cachedFontId, line.substr(0, breakPos).c_str()) > viewportWidth) {
        // Try to break at space
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          // Break at character boundary for UTF-8
          breakPos--;
          // Make sure we don't break in the middle of a UTF-8 sequence
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      // Skip space at break point
      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    }

    // Determine how much of the source buffer we consumed
    if (line.empty()) {
      // Fully consumed this source line, move past the newline
      pos = lineEnd + 1;
    } else {
      // Partially consumed - page is full mid-line
      // Move pos to where we stopped in the line (NOT past the line)
      pos = pos + lineBytePos;
      break;
    }
  }

  // Ensure we make progress even if calculations go wrong
  if (pos == 0 && !outLines.empty()) {
    // Fallback: at minimum, consume something to avoid infinite loop
    pos = 1;
  }

  nextOffset = offset + pos;

  // Make sure we don't go past the file
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);

  return !outLines.empty();
}

void TxtReaderActivity::render(Activity::RenderLock&&) {
  if (!txt) {
    return;
  }

  if (recentSwitcherOpen) {
    renderRecentSwitcher();
    return;
  }

  // Initialize reader if not done
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true, EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  // Load current page content
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  if (lastObservedPage != currentPage) {
    lastObservedPage = currentPage;
    if (lastSavedPage != currentPage) {
      progressDirty = true;
      lastProgressChangeMs = millis();
    }
  }

  flushProgressIfNeeded(false);
}

void TxtReaderActivity::loadRecentSwitcherBooks() {
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

void TxtReaderActivity::renderRecentSwitcher() {
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

void TxtReaderActivity::renderPage() {
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  normalizeReaderMargins(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom, &orientedMarginLeft);
  orientedMarginTop += cachedScreenMarginTop;
  orientedMarginLeft += cachedScreenMarginHorizontal;
  orientedMarginRight += cachedScreenMarginHorizontal;
  orientedMarginBottom += cachedScreenMarginBottom;

  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;
  const int statusBarReserved = SETTINGS.statusBarEnabled
                                    ? computeStatusBarReservedHeight(renderer, SETTINGS.statusBarShowBookBar,
                                                                     SETTINGS.statusBarShowChapterBar)
                                    : 0;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom - statusBarReserved;
  const int usedContentHeight = static_cast<int>(currentPageLines.size()) * lineHeight;
  const int verticalCenterOffset = std::max(0, (viewportHeight - usedContentHeight) / 2);

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = orientedMarginTop + verticalCenterOffset;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = orientedMarginLeft;

        // Apply text alignment
        switch (cachedParagraphAlignment) {
          case CrossPointSettings::LEFT_ALIGN:
          default:
            // x already set to left margin
            break;
          case CrossPointSettings::CENTER_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = orientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case CrossPointSettings::RIGHT_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = orientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case CrossPointSettings::JUSTIFIED:
            // For plain text, justified is treated as left-aligned
            // (true justification would require word spacing adjustments)
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineHeight;
    }
  };

  // First pass: BW rendering
  renderLines();
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  // Reader text AA is intentionally disabled (BW only in reader).
  if (false) {
    // Save BW buffer for restoration after grayscale pass
    renderer.storeBwBuffer();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderLines();
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderLines();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    // Restore BW buffer
    renderer.restoreBwBuffer();
  }
}

void TxtReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  (void)orientedMarginBottom;
  if (!SETTINGS.statusBarEnabled) {
    return;
  }
  const bool showProgressBar = SETTINGS.statusBarShowBookBar;
  const bool showChapterProgressBar = SETTINGS.statusBarShowChapterBar;
  const bool showStatusTopLine = SETTINGS.statusBarTopLine;
  const bool showPageCounter = SETTINGS.statusBarShowPageCounter;
  const bool showBookPercentage = SETTINGS.statusBarShowBookPercentage;
  const bool showChapterPercentage = SETTINGS.statusBarShowChapterPercentage;
  const bool showBattery = SETTINGS.statusBarShowBattery;
  const bool showTitle = SETTINGS.statusBarShowChapterTitle;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;
  constexpr int statusItemGap = 12;

  auto metrics = UITheme::getInstance().getMetrics();
  const auto screenHeight = renderer.getScreenHeight();
  const int statusBarProgressHeight = SETTINGS.getStatusBarProgressBarHeight();
  const int barsHeight = computeStatusBarsHeight(showProgressBar, showChapterProgressBar, statusBarProgressHeight);
  const int statusBarReserved = computeStatusBarReservedHeight(renderer, showProgressBar, showChapterProgressBar);
  const int statusBottomInset = getStatusBottomInset(renderer);
  const int statusTopY = screenHeight - statusBottomInset - statusBarReserved;
  if (showStatusTopLine) {
    renderer.drawLine(orientedMarginLeft, statusTopY, renderer.getScreenWidth() - orientedMarginRight - 1, statusTopY,
                      true);
  }
  const auto textY = statusTopY + statusTextTopPadding;
  std::string progressText;
  int progressTextWidth = 0;

  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;

  if (showPageCounter || showBookPercentage || showChapterPercentage) {
    char progressStr[64] = {0};
    int offset = 0;
    if (showPageCounter) {
      offset += snprintf(progressStr + offset, sizeof(progressStr) - offset, "%d/%d", currentPage + 1, totalPages);
    }
    if (showBookPercentage) {
      offset += snprintf(progressStr + offset, sizeof(progressStr) - offset, "%sB:%.0f%%", (offset > 0) ? "  " : "", progress);
    }
    if (showChapterPercentage) {
      snprintf(progressStr + offset, sizeof(progressStr) - offset, "%sC:%.0f%%", (offset > 0) ? "  " : "", progress);
    }
    progressText = progressStr;
    progressTextWidth = renderer.getTextWidth(SMALL_FONT_ID, progressText.c_str());
  }

  std::string titleText;
  int titleWidth = 0;
  if (showTitle) {
    titleText = txt->getTitle();
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
  visibleItems += showTitle ? 1 : 0;
  visibleItems += progressText.empty() ? 0 : 1;
  const int groupGaps = std::max(0, visibleItems - 1) * statusItemGap;
  const int usableWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  if (showTitle) {
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

  if (showTitle && !titleText.empty()) {
    renderer.drawText(SMALL_FONT_ID, currentX, textY, titleText.c_str());
    currentX += titleWidth + statusItemGap;
  }

  if (!progressText.empty()) {
    renderer.drawText(SMALL_FONT_ID, currentX, textY, progressText.c_str());
  }

  if (showProgressBar) {
    drawStyledProgressBar(renderer, static_cast<size_t>(progress), 0);
  }

  if (showChapterProgressBar) {
    const int chapterLevel = showProgressBar ? 1 : 0;
    drawStyledProgressBar(renderer, static_cast<size_t>(progress), chapterLevel);
  }

}

void TxtReaderActivity::saveProgress() const {
  FsFile f;
  if (Storage.openFileForWrite("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    const uint32_t page = currentPage < 0 ? 0u : static_cast<uint32_t>(currentPage);
    data[0] = page & 0xFF;
    data[1] = (page >> 8) & 0xFF;
    data[2] = (page >> 16) & 0xFF;
    data[3] = (page >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void TxtReaderActivity::flushProgressIfNeeded(const bool force) {
  if (!txt || !progressDirty) {
    return;
  }

  const auto now = millis();
  if (!force && now - lastProgressChangeMs < progressSaveDebounceMs) {
    return;
  }

  saveProgress();
  lastSavedPage = currentPage;
  progressDirty = false;
}

void TxtReaderActivity::loadProgress() {
  FsFile f;
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    const int bytesRead = f.read(data, sizeof(data));
    if (bytesRead >= 2) {
      if (bytesRead >= 4) {
        currentPage = static_cast<int>(static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                                       (static_cast<uint32_t>(data[2]) << 16) |
                                       (static_cast<uint32_t>(data[3]) << 24));
      } else {
        // Backward compatibility with older 2-byte progress files.
        currentPage = data[0] + (data[1] << 8);
      }
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      LOG_DBG("TRS", "Loaded progress: page %d/%d", currentPage, totalPages);
      lastSavedPage = currentPage;
      lastObservedPage = currentPage;
      progressDirty = false;
    }
    f.close();
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  // Cache file format (using serialization module):
  // - uint32_t: magic "TXTI"
  // - uint8_t: cache version
  // - uint32_t: file size (to validate cache)
  // - int32_t: viewport width
  // - int32_t: lines per page
  // - int32_t: font ID (to invalidate cache on font change)
  // - int32_t: horizontal/top/bottom margins (to invalidate cache on margin changes)
  // - uint8_t: paragraph alignment (to invalidate cache on alignment change)
  // - uint32_t: total pages count
  // - N * uint32_t: page offsets

  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForRead("TRS", cachePath, f)) {
    LOG_DBG("TRS", "No page index cache found");
    return false;
  }

  // Read and validate header using serialization module
  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    LOG_DBG("TRS", "Cache magic mismatch, rebuilding");
    f.close();
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version, CACHE_VERSION);
    f.close();
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    LOG_DBG("TRS", "Cache file size mismatch, rebuilding");
    f.close();
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    LOG_DBG("TRS", "Cache viewport width mismatch, rebuilding");
    f.close();
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    LOG_DBG("TRS", "Cache lines per page mismatch, rebuilding");
    f.close();
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId, cachedFontId);
    f.close();
    return false;
  }

  int32_t marginHorizontal;
  serialization::readPod(f, marginHorizontal);
  int32_t marginTop;
  serialization::readPod(f, marginTop);
  int32_t marginBottom;
  serialization::readPod(f, marginBottom);
  if (marginHorizontal != cachedScreenMarginHorizontal || marginTop != cachedScreenMarginTop ||
      marginBottom != cachedScreenMarginBottom) {
    LOG_DBG("TRS", "Cache screen margins mismatch, rebuilding");
    f.close();
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    LOG_DBG("TRS", "Cache paragraph alignment mismatch, rebuilding");
    f.close();
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  // Read page offsets
  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  f.close();
  totalPages = pageOffsets.size();
  LOG_DBG("TRS", "Loaded page index cache: %d pages", totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!Storage.openFileForWrite("TRS", cachePath, f)) {
    LOG_ERR("TRS", "Failed to save page index cache");
    return;
  }

  // Write header using serialization module
  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMarginHorizontal));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMarginTop));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMarginBottom));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  // Write page offsets
  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  f.close();
  LOG_DBG("TRS", "Saved page index cache: %d pages", totalPages);
}
