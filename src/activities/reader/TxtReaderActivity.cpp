#include "TxtReaderActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StatusPopup.h"
#include "util/StringUtils.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr unsigned long confirmDoubleTapMs = 350;
constexpr unsigned long progressSaveDebounceMs = 800;
constexpr int progressBarMarginTop = 1;
constexpr int recentSwitcherRows = 8;
constexpr size_t CHUNK_SIZE = 8 * 1024; // 8KB chunk for reading
constexpr int statusTextTopPadding = 2;
constexpr int statusTextLineGap = 1;
constexpr int statusTextToBarsGap = 0;

// Cache file magic and version
constexpr uint32_t CACHE_MAGIC = 0x54585449; // "TXTI"
constexpr uint8_t CACHE_VERSION = 3; // Increment when cache format changes

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
                                  const int totalPages) {
  switch (mode) {
    case CrossPointSettings::STATUS_PAGE_LEFT_IN_BOOK:
    case CrossPointSettings::STATUS_PAGE_LEFT_IN_CHAPTER: {
      int pagesLeft = totalPages - (currentPage + 1);
      if (pagesLeft < 0) {
        pagesLeft = 0;
      }
      return std::to_string(pagesLeft) + " left";
    }
    case CrossPointSettings::STATUS_PAGE_CURRENT_TOTAL:
    default:
      return std::to_string(currentPage + 1) + "/" + std::to_string(totalPages);
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
} // namespace

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
    renderer.setOrientation(
        GfxRenderer::Orientation::LandscapeCounterClockwise);
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
  cachedTitleUsableWidth = -1;
  cachedTitleNoTitleTruncation = false;
  cachedTitleLines.clear();

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
  cachedTitleUsableWidth = -1;
  cachedTitleNoTitleTruncation = false;
  cachedTitleLines.clear();
  txt.reset();
}

const std::vector<std::string>& TxtReaderActivity::getStatusBarTitleLines(
    const int usableWidth, const bool noTitleTruncation) {
  if (cachedTitleUsableWidth == usableWidth &&
      cachedTitleNoTitleTruncation == noTitleTruncation) {
    return cachedTitleLines;
  }

  std::string titleText = txt ? txt->getTitle() : "";
  if (titleText.empty()) {
    titleText = tr(STR_UNNAMED);
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

  cachedTitleUsableWidth = usableWidth;
  cachedTitleNoTitleTruncation = noTitleTruncation;
  return cachedTitleLines;
}

int TxtReaderActivity::getStatusBarReserveTitleLineCount(
    const int usableWidth, const bool noTitleTruncation) {
  return static_cast<int>(
      getStatusBarTitleLines(usableWidth, noTitleTruncation).size());
}

TxtReaderActivity::StatusBarLayout TxtReaderActivity::buildStatusBarLayout(
    const int usableWidth, const int topReservedHeight,
    const int bottomReservedHeight) {
  StatusBarLayout layout;
  layout.usableWidth = std::max(0, usableWidth);
  layout.topReservedHeight = topReservedHeight;
  layout.bottomReservedHeight = bottomReservedHeight;
  if (!SETTINGS.statusBarEnabled) {
    return layout;
  }

  layout.progress =
      totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0.0f;

  if (SETTINGS.statusBarShowPageCounter) {
    layout.pageCounterText = formatPageCounterText(
        SETTINGS.statusBarPageCounterMode, currentPage, totalPages);
    layout.pageCounterTextWidth =
        renderer.getTextWidth(SMALL_FONT_ID, layout.pageCounterText.c_str());
  }
  if (SETTINGS.statusBarShowBookPercentage) {
    char bookPercentageStr[16] = {0};
    snprintf(bookPercentageStr, sizeof(bookPercentageStr), "B:%.0f%%",
             layout.progress);
    layout.bookPercentageText = bookPercentageStr;
    layout.bookPercentageTextWidth = renderer.getTextWidth(
        SMALL_FONT_ID, layout.bookPercentageText.c_str());
  }
  if (SETTINGS.statusBarShowChapterPercentage) {
    char chapterPercentageStr[16] = {0};
    snprintf(chapterPercentageStr, sizeof(chapterPercentageStr), "C:%.0f%%",
             layout.progress);
    layout.chapterPercentageText = chapterPercentageStr;
    layout.chapterPercentageTextWidth = renderer.getTextWidth(
        SMALL_FONT_ID, layout.chapterPercentageText.c_str());
  }

  if (SETTINGS.statusBarShowChapterTitle) {
    layout.titleLines = getStatusBarTitleLines(
        layout.usableWidth, SETTINGS.statusBarNoTitleTruncation);
  }

  return layout;
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
    const bool prevTriggered =
        mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool nextTriggered =
        mappedInput.wasReleased(MappedInputManager::Button::PageForward) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right);
    if (prevTriggered && !recentSwitcherBooks.empty()) {
      recentSwitcherSelection =
          (recentSwitcherSelection +
           static_cast<int>(recentSwitcherBooks.size()) - 1) %
          recentSwitcherBooks.size();
      requestUpdate();
      return;
    }
    if (nextTriggered && !recentSwitcherBooks.empty()) {
      recentSwitcherSelection =
          (recentSwitcherSelection + 1) % recentSwitcherBooks.size();
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) &&
        !recentSwitcherBooks.empty()) {
      const std::string selectedPath =
          recentSwitcherBooks[recentSwitcherSelection].path;
      recentSwitcherOpen = false;
      if (!selectedPath.empty()) {
        onOpenBook(selectedPath);
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) &&
        mappedInput.getHeldTime() < goHomeMs) {
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
    if (lastConfirmReleaseMs > 0 &&
        now - lastConfirmReleaseMs <= confirmDoubleTapMs) {
      lastConfirmReleaseMs = 0;
      toggleReaderBoldSwap();
      return;
    }
    lastConfirmReleaseMs = now;
  }

  // Long press CONFIRM (1s+) toggles orientation: Portrait <-> Landscape CCW.
  if (!confirmLongPressHandled &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= goHomeMs) {
    confirmLongPressHandled = true;
    SETTINGS.orientation =
        (SETTINGS.orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CCW)
            ? CrossPointSettings::ORIENTATION::PORTRAIT
            : CrossPointSettings::ORIENTATION::LANDSCAPE_CCW;
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("TRS", "Failed to save settings after orientation change");
    }
    renderer.setOrientation(
        SETTINGS.orientation == CrossPointSettings::ORIENTATION::LANDSCAPE_CCW
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
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("TRS", "Failed to save settings after bold swap toggle");
  }
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
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom,
      orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight,
                                   &orientedMarginBottom, &orientedMarginLeft);
  normalizeReaderMargins(&orientedMarginTop, &orientedMarginRight,
                         &orientedMarginBottom, &orientedMarginLeft);
  orientedMarginTop += cachedScreenMarginTop;
  orientedMarginLeft += cachedScreenMarginHorizontal;
  orientedMarginRight += cachedScreenMarginHorizontal;
  orientedMarginBottom += cachedScreenMarginBottom;

  int statusBarTopReserved = 0;
  int statusBarBottomReserved = 0;
  if (SETTINGS.statusBarEnabled) {
    const int usableWidth =
        renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
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
    const int titleLineCount =
        SETTINGS.statusBarShowChapterTitle
            ? (SETTINGS.statusBarNoTitleTruncation
                   ? getStatusBarReserveTitleLineCount(
                         usableWidth, SETTINGS.statusBarNoTitleTruncation)
                   : 1)
            : 0;
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
          getStatusTopInset(renderer) + cachedScreenMarginTop +
          statusBarTopReserved;
    }
    if (statusBarBottomReserved > 0) {
      orientedMarginBottom =
          getStatusBottomInset(renderer) + cachedScreenMarginBottom +
          statusBarBottomReserved;
    }
  }

  viewportWidth =
      renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const int viewportHeight =
      renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1)
    linesPerPage = 1;

  LOG_DBG("TRS", "Viewport: %dx%d, lines per page: %d", viewportWidth,
          viewportHeight, linesPerPage);

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
  pageOffsets.push_back(0); // First page starts at offset 0

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  LOG_DBG("TRS", "Building page index for %zu bytes...", fileSize);

  StatusPopup::showBlocking(renderer, tr(STR_INDEXING));

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

bool TxtReaderActivity::loadPageAtOffset(size_t offset,
                                         std::vector<std::string> &outLines,
                                         size_t &nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  // Read a chunk from file
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto *buffer = static_cast<uint8_t *>(malloc(chunkSize + 1));
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

    // Calculate the actual length of line content in the buffer (excluding
    // newline)
    size_t lineContentLen = lineEnd - pos;

    // Check for carriage return
    bool hasCR =
        (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    // Extract line content for display (without CR/LF)
    std::string line(reinterpret_cast<char *>(buffer + pos), displayLen);

    // Track position within this source line (in bytes from pos)
    size_t lineBytePos = 0;

    // Word wrap if needed
    while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
      int lineWidth = renderer.getTextWidth(cachedFontId, line.c_str());

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen; // Consumed entire display content
        line.clear();
        break;
      }

      // Find break point
      size_t breakPos = line.length();
      while (breakPos > 0 &&
             renderer.getTextWidth(cachedFontId,
                                   line.substr(0, breakPos).c_str()) >
                 viewportWidth) {
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

void TxtReaderActivity::render(Activity::RenderLock &&) {
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
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_EMPTY_FILE), true,
                              EpdFontFamily::REGULAR);
    renderer.displayBuffer();
    return;
  }

  // Bounds check
  if (currentPage < 0)
    currentPage = 0;
  if (currentPage >= totalPages)
    currentPage = totalPages - 1;

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
  const auto &books = RECENT_BOOKS.getBooks();
  for (const auto &book : books) {
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
  renderer.drawCenteredText(UI_12_FONT_ID, titleY, tr(STR_MENU_RECENT_BOOKS),
                            true, EpdFontFamily::REGULAR);

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
        title = (lastSlash == std::string::npos)
                    ? recentSwitcherBooks[i].path
                    : recentSwitcherBooks[i].path.substr(lastSlash + 1);
      }
      title = renderer.truncatedText(UI_10_FONT_ID, title.c_str(), popupW - 28);
    }
    renderer.drawText(UI_10_FONT_ID, popupX + 14, rowY + 3, title.c_str(),
                      !selected);
  }

  renderer.displayBuffer();
}

void TxtReaderActivity::renderPage() {
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom,
      orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight,
                                   &orientedMarginBottom, &orientedMarginLeft);
  normalizeReaderMargins(&orientedMarginTop, &orientedMarginRight,
                         &orientedMarginBottom, &orientedMarginLeft);
  orientedMarginTop += cachedScreenMarginTop;
  orientedMarginLeft += cachedScreenMarginHorizontal;
  orientedMarginRight += cachedScreenMarginHorizontal;
  orientedMarginBottom += cachedScreenMarginBottom;

  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;
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
    const int titleLineCount =
        SETTINGS.statusBarShowChapterTitle
            ? (SETTINGS.statusBarNoTitleTruncation
                   ? getStatusBarReserveTitleLineCount(
                         usableWidth, SETTINGS.statusBarNoTitleTruncation)
                   : 1)
            : 0;
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
          getStatusTopInset(renderer) + cachedScreenMarginTop +
          statusBarTopReserved;
    }
    if (statusBarBottomReserved > 0) {
      orientedMarginBottom =
          getStatusBottomInset(renderer) + cachedScreenMarginBottom +
          statusBarBottomReserved;
    }
  }
  const StatusBarLayout statusBarLayout =
      buildStatusBarLayout(usableWidth, statusBarTopReserved,
                           statusBarBottomReserved);

  // Render text lines with alignment
  auto renderLines = [&]() {
    int y = orientedMarginTop;
    for (const auto &line : currentPageLines) {
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
  renderStatusBar(statusBarLayout, orientedMarginRight, orientedMarginBottom,
                  orientedMarginLeft);

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

void TxtReaderActivity::renderStatusBar(const StatusBarLayout& statusBarLayout,
                                        const int orientedMarginRight,
                                        const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
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

  auto metrics = UITheme::getInstance().getMetrics();
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
    const int barsHeight = computeStatusBarsHeight(
        showBandBookBar, showBandChapterBar, progressBarHeight,
        textBlockHeight > 0);

    int currentTextY = bandTopY;
    if (textBlockHeight > 0) {
      currentTextY += statusTextTopPadding;
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

    const int activeBars =
        (showBandBookBar ? 1 : 0) + (showBandChapterBar ? 1 : 0);
    int barIndex = 0;
    int currentBarY = bandTopY + reservedHeight - barsHeight +
                      ((textBlockHeight > 0) ? progressBarMarginTop : 0);
    const auto drawBandBar = [&](const size_t progressPercent) {
      const bool isFirstBar = barIndex == 0;
      const bool isLastBar = barIndex == activeBars - 1;
      int barY = currentBarY + barIndex * progressBarHeight;
      int barDrawHeight = progressBarHeight;
      if (renderTopBand && isFirstBar && textBlockHeight == 0) {
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
      drawBandBar(static_cast<size_t>(statusBarLayout.progress));
    }
    if (showBandChapterBar) {
      drawBandBar(static_cast<size_t>(statusBarLayout.progress));
    }
  };

  renderBand(statusTopInset, statusBarLayout.topReservedHeight, true);
  renderBand(screenHeight - statusBottomInset - statusBarLayout.bottomReservedHeight,
             statusBarLayout.bottomReservedHeight, false);
}

void TxtReaderActivity::saveProgress() const {
  const std::string progPath = txt->getCachePath() + "/progress.bin";
  const std::string tmpPath = txt->getCachePath() + "/progress_tmp.bin";

  if (Storage.exists(tmpPath.c_str())) {
    Storage.remove(tmpPath.c_str());
  }

  FsFile f;
  if (Storage.openFileForWrite("TRS", tmpPath.c_str(), f)) {
    uint8_t data[4];
    const uint32_t page =
        currentPage < 0 ? 0u : static_cast<uint32_t>(currentPage);
    data[0] = page & 0xFF;
    data[1] = (page >> 8) & 0xFF;
    data[2] = (page >> 16) & 0xFF;
    data[3] = (page >> 24) & 0xFF;
    f.write(data, 4);
    f.close();

    if (Storage.exists(progPath.c_str())) {
      Storage.remove(progPath.c_str());
    }
    Storage.rename(tmpPath.c_str(), progPath.c_str());
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
  if (Storage.openFileForRead("TRS", txt->getCachePath() + "/progress.bin",
                              f)) {
    uint8_t data[4];
    const int bytesRead = f.read(data, sizeof(data));
    if (bytesRead >= 2) {
      if (bytesRead >= 4) {
        currentPage = static_cast<int>(static_cast<uint32_t>(data[0]) |
                                       (static_cast<uint32_t>(data[1]) << 8) |
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
  // - int32_t: horizontal/top/bottom margins (to invalidate cache on margin
  // changes)
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
    LOG_DBG("TRS", "Cache version mismatch (%d != %d), rebuilding", version,
            CACHE_VERSION);
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
    LOG_DBG("TRS", "Cache font ID mismatch (%d != %d), rebuilding", fontId,
            cachedFontId);
    f.close();
    return false;
  }

  int32_t marginHorizontal;
  serialization::readPod(f, marginHorizontal);
  int32_t marginTop;
  serialization::readPod(f, marginTop);
  int32_t marginBottom;
  serialization::readPod(f, marginBottom);
  if (marginHorizontal != cachedScreenMarginHorizontal ||
      marginTop != cachedScreenMarginTop ||
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
  serialization::writePod(f,
                          static_cast<int32_t>(cachedScreenMarginHorizontal));
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
