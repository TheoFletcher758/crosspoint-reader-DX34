#include "BaseTheme.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>

#include <cstdint>
#include <string>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "I18n.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

// Internal constants
namespace {
constexpr int batteryPercentSpacing = 4;
constexpr int homeMenuMargin = 20;
constexpr int homeMarginTop = 30;

bool endsWithIgnoreCase(const char* text, const char* suffix) {
  if (!text || !suffix) return false;
  size_t textLen = 0;
  while (text[textLen] != '\0') textLen++;
  size_t suffixLen = 0;
  while (suffix[suffixLen] != '\0') suffixLen++;
  if (suffixLen > textLen) return false;

  const size_t start = textLen - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i) {
    char a = text[start + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
    if (a != b) return false;
  }
  return true;
}

bool isBookFile(const char* name) {
  return endsWithIgnoreCase(name, ".epub") || endsWithIgnoreCase(name, ".xtc") || endsWithIgnoreCase(name, ".xtch") ||
         endsWithIgnoreCase(name, ".txt");
}

bool isBmpFile(const char* name) { return endsWithIgnoreCase(name, ".bmp"); }

void countFilesRecursive(FsFile& dir, uint32_t& bookCount, uint32_t& bmpCount, bool countBooks, bool countBmps) {
  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    if (entry.isDirectory()) {
      if (name[0] != '.') {
        countFilesRecursive(entry, bookCount, bmpCount, countBooks, countBmps);
      }
      entry.close();
      continue;
    }

    if (countBooks && isBookFile(name)) {
      ++bookCount;
    }
    if (countBmps && isBmpFile(name)) {
      ++bmpCount;
    }
    entry.close();
  }
}

void drawDashedRect(const GfxRenderer& renderer, int x, int y, int w, int h) {
  constexpr int dash = 5;
  constexpr int gap = 3;
  constexpr int step = dash + gap;
  const int x2 = x + w - 1;
  const int y2 = y + h - 1;

  for (int px = x; px <= x2; px += step) {
    const int end = std::min(px + dash - 1, x2);
    renderer.drawLine(px, y, end, y);
    renderer.drawLine(px, y2, end, y2);
  }
  for (int py = y; py <= y2; py += step) {
    const int end = std::min(py + dash - 1, y2);
    renderer.drawLine(x, py, x, end);
    renderer.drawLine(x2, py, x2, end);
  }
}

struct HomeInfoStats {
  uint32_t bookCount = 0;
  uint32_t sleepBmpCount = 0;
  uint64_t freeBytes = 0;
  bool valid = false;
};

HomeInfoStats gHomeInfoStats;

void scanHomeInfoStats(HomeInfoStats& stats) {
  stats.bookCount = 0;
  stats.sleepBmpCount = 0;
  stats.freeBytes = Storage.freeBytes();

  auto root = Storage.open("/");
  if (root && root.isDirectory()) {
    countFilesRecursive(root, stats.bookCount, stats.sleepBmpCount, true, false);
    root.close();
  }

  auto sleepDir = Storage.open("/sleep");
  if (sleepDir && sleepDir.isDirectory()) {
    countFilesRecursive(sleepDir, stats.bookCount, stats.sleepBmpCount, false, true);
    sleepDir.close();
  }

  stats.valid = true;
}

const HomeInfoStats& getHomeInfoStats() {
  if (gHomeInfoStats.valid) {
    return gHomeInfoStats;
  }

  scanHomeInfoStats(gHomeInfoStats);
  return gHomeInfoStats;
}

// Helper: draw battery icon at given position
void drawBatteryIcon(const GfxRenderer& renderer, int x, int y, int battWidth, int rectHeight, uint16_t percentage) {
  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rectHeight - 1, x + battWidth - 3, y + rectHeight - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rectHeight - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rectHeight - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rectHeight - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rectHeight - 5);

  // The +1 is to round up, so that we always fill at least one pixel
  int filledWidth = percentage * (battWidth - 5) / 100 + 1;
  if (filledWidth > battWidth - 5) {
    filledWidth = battWidth - 5;  // Ensure we don't overflow
  }

  renderer.fillRect(x + 2, y + 2, filledWidth, rectHeight - 4);
}
}  // namespace

void BaseTheme::invalidateHomeInfoStats() { gHomeInfoStats.valid = false; }

void BaseTheme::refreshHomeInfoStats() {
  gHomeInfoStats.valid = false;
  scanHomeInfoStats(gHomeInfoStats);
}

uint64_t BaseTheme::homeInfoStatsSignature() {
  const auto& stats = getHomeInfoStats();
  // Compact signature from the three displayed values.
  return (static_cast<uint64_t>(stats.bookCount) << 40) ^ (static_cast<uint64_t>(stats.sleepBmpCount) << 24) ^
         stats.freeBytes;
}

void BaseTheme::drawBatteryLeft(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Reader/status usage is text-only for a cleaner, more compact layout.
  if (!showPercentage) {
    return;
  }
  const uint16_t percentage = battery.readPercentage();
  const auto percentageText = std::to_string(percentage) + "%";
  const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);
  const int textY = rect.y + std::max(0, (rect.height - textHeight) / 2);
  renderer.drawText(SMALL_FONT_ID, rect.x, textY, percentageText.c_str());
}

void BaseTheme::drawBatteryRight(const GfxRenderer& renderer, Rect rect,
                                 const bool showPercentage,
                                 const int textFont,
                                 const int iconWidth,
                                 const int iconHeight,
                                 const bool showIcon) const {
  // Right aligned: percentage on left, icon on right (UI headers)
  // rect.x is positioned for the icon, or the right text edge when icon is hidden.
  const uint16_t percentage = battery.readPercentage();
  const int y = rect.y + std::max(0, (rect.height - iconHeight) / 2);

  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    const int textWidth = renderer.getTextWidth(textFont, percentageText.c_str());
    const int textHeight = renderer.getTextHeight(textFont);
    const int textX = showIcon ? (rect.x - textWidth - batteryPercentSpacing)
                               : (rect.x - textWidth);
    const int textY = rect.y + std::max(0, (rect.height - textHeight) / 2);
    // Clear the area where we're going to draw the text to prevent ghosting.
    renderer.fillRect(textX, textY, textWidth, textHeight, false);
    // Draw text to the left of the icon
    renderer.drawText(textFont, textX, textY, percentageText.c_str());
  }

  if (showIcon) {
    drawBatteryIcon(renderer, rect.x, y, iconWidth, iconHeight, percentage);
  }
}

void BaseTheme::drawProgressBar(const GfxRenderer& renderer, Rect rect, const size_t current,
                                const size_t total) const {
  if (total == 0) {
    return;
  }

  // Use 64-bit arithmetic to avoid overflow for large files
  const int percent = static_cast<int>((static_cast<uint64_t>(current) * 100) / total);

  // Draw outline
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  // Draw filled portion
  const int fillWidth = (rect.width - 4) * percent / 100;
  if (fillWidth > 0) {
    renderer.fillRect(rect.x + 2, rect.y + 2, fillWidth, rect.height - 4);
  }

  // Draw percentage text centered below bar
  const std::string percentText = std::to_string(percent) + "%";
  renderer.drawCenteredText(UI_10_FONT_ID, rect.y + rect.height + 15, percentText.c_str());
}

void BaseTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = BaseMetrics::values.buttonHintsHeight;
  constexpr int buttonY = BaseMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  constexpr int buttonPositions[] = {25, 130, 245, 350};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    // Only draw if the label is non-empty
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      renderer.fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, false);
      renderer.drawRect(x, pageHeight - buttonY, buttonWidth, buttonHeight);
      const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(UI_10_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void BaseTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = BaseMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 80;                                       // Height on screen (width when rotated)
  constexpr int buttonX = 4;                                             // Distance from right edge
  // Position for the button group - buttons share a border so they're adjacent
  constexpr int topButtonY = 345;  // Top button position

  const char* labels[] = {topBtn, bottomBtn};

  // Draw the shared border for both buttons as one unit
  const int x = screenWidth - buttonX - buttonWidth;

  // Draw top button outline (3 sides, bottom open)
  if (topBtn != nullptr && topBtn[0] != '\0') {
    renderer.drawLine(x, topButtonY, x + buttonWidth - 1, topButtonY);                                       // Top
    renderer.drawLine(x, topButtonY, x, topButtonY + buttonHeight - 1);                                      // Left
    renderer.drawLine(x + buttonWidth - 1, topButtonY, x + buttonWidth - 1, topButtonY + buttonHeight - 1);  // Right
  }

  // Draw shared middle border
  if ((topBtn != nullptr && topBtn[0] != '\0') || (bottomBtn != nullptr && bottomBtn[0] != '\0')) {
    renderer.drawLine(x, topButtonY + buttonHeight, x + buttonWidth - 1, topButtonY + buttonHeight);  // Shared border
  }

  // Draw bottom button outline (3 sides, top is shared)
  if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
    renderer.drawLine(x, topButtonY + buttonHeight, x, topButtonY + 2 * buttonHeight - 1);  // Left
    renderer.drawLine(x + buttonWidth - 1, topButtonY + buttonHeight, x + buttonWidth - 1,
                      topButtonY + 2 * buttonHeight - 1);  // Right
    renderer.drawLine(x, topButtonY + 2 * buttonHeight - 1, x + buttonWidth - 1,
                      topButtonY + 2 * buttonHeight - 1);  // Bottom
  }

  // Draw text for each button
  for (int i = 0; i < 2; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int y = topButtonY + i * buttonHeight;

      // Draw rotated text centered in the button
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      const int textHeight = renderer.getTextHeight(SMALL_FONT_ID);

      // Center the rotated text in the button
      const int textX = x + (buttonWidth - textHeight) / 2;
      const int textY = y + (buttonHeight + textWidth) / 2;

      renderer.drawTextRotated90CW(SMALL_FONT_ID, textX, textY, labels[i]);
    }
  }
}

void BaseTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<std::string(int index)>& rowIcon,
                         const std::function<std::string(int index)>& rowValue) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? BaseMetrics::values.listWithSubtitleRowHeight : BaseMetrics::values.listRowHeight;
  int pageItems = rect.height / rowHeight;
  if (pageItems < 1) {
    pageItems = 1;
  }

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    constexpr int indicatorWidth = 20;
    constexpr int arrowSize = 6;
    constexpr int margin = 15;  // Offset from right edge

    const int centerX = rect.x + rect.width - indicatorWidth / 2 - margin;
    const int indicatorTop = rect.y;  // Offset to avoid overlapping side button hints
    const int indicatorBottom = rect.y + rect.height - arrowSize;

    // Draw up arrow at top (^) - narrow point at top, wide base at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + i * 2;
      const int startX = centerX - i;
      renderer.drawLine(startX, indicatorTop + i, startX + lineWidth - 1, indicatorTop + i);
    }

    // Draw down arrow at bottom (v) - wide base at top, narrow point at bottom
    for (int i = 0; i < arrowSize; ++i) {
      const int lineWidth = 1 + (arrowSize - 1 - i) * 2;
      const int startX = centerX - (arrowSize - 1 - i);
      renderer.drawLine(startX, indicatorBottom - arrowSize + 1 + i, startX + lineWidth - 1,
                        indicatorBottom - arrowSize + 1 + i);
    }
  }

  // Draw selection
  int contentWidth = rect.width - 5;
  if (selectedIndex >= 0) {
    renderer.fillRect(0, rect.y + selectedIndex % pageItems * rowHeight - 2, rect.width, rowHeight);
  }
  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    int textWidth = contentWidth - BaseMetrics::values.contentSidePadding * 2 - (rowValue != nullptr ? 60 : 0);

    // Draw name
    auto itemName = rowTitle(i);
    auto font = (rowSubtitle != nullptr) ? UI_12_FONT_ID : UI_10_FONT_ID;
    auto item = renderer.truncatedText(font, itemName.c_str(), textWidth);
    renderer.drawText(font, rect.x + BaseMetrics::values.contentSidePadding, itemY, item.c_str(), i != selectedIndex);

    if (rowSubtitle != nullptr) {
      // Draw subtitle
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(UI_10_FONT_ID, subtitleText.c_str(), textWidth);
      renderer.drawText(UI_10_FONT_ID, rect.x + BaseMetrics::values.contentSidePadding, itemY + 30, subtitle.c_str(),
                        i != selectedIndex);
    }

    if (rowValue != nullptr) {
      // Draw value as a compact badge (black background, white text)
      std::string valueText = rowValue(i);
      if (!valueText.empty()) {
        const int valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());
        const int valueTextHeight = renderer.getLineHeight(UI_10_FONT_ID);
        constexpr int badgePadX = 4;
        const int badgeWidth = valueTextWidth + badgePadX * 2;
        const int badgeX = rect.x + contentWidth - BaseMetrics::values.contentSidePadding - badgeWidth;
        const int badgeY = itemY;

        renderer.fillRect(badgeX, badgeY, badgeWidth, valueTextHeight, true);
        renderer.drawText(UI_10_FONT_ID, badgeX + badgePadX, badgeY, valueText.c_str(), false);
      }
    }
  }
}

void BaseTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title) const {
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const bool homeStyleHeader = title == nullptr;
  const int batteryTextFont = UI_10_FONT_ID;
  const int batteryIconWidth =
      homeStyleHeader ? BaseMetrics::values.batteryWidth + 2
                      : BaseMetrics::values.batteryWidth;
  const int batteryIconHeight =
      homeStyleHeader ? BaseMetrics::values.batteryHeight + 2
                      : BaseMetrics::values.batteryHeight;
  const bool showBatteryIcon = false;
  // Align percentage text to the right edge.
  const int batteryX = rect.x + rect.width - 12 -
                       (showBatteryIcon ? batteryIconWidth : 0);
  drawBatteryRight(renderer,
                   Rect{batteryX, rect.y + 5, batteryIconWidth, batteryIconHeight},
                   showBatteryPercentage, batteryTextFont, batteryIconWidth,
                   batteryIconHeight, showBatteryIcon);

  if (title) {
    const int padding = 12 +
                        (showBatteryPercentage
                             ? renderer.getTextWidth(batteryTextFont, "100%")
                             : 0);
    auto truncatedTitle = renderer.truncatedText(UI_12_FONT_ID, title,
                                                 rect.width - padding * 2 - BaseMetrics::values.contentSidePadding * 2,
                                                 EpdFontFamily::REGULAR);
    renderer.drawCenteredText(UI_12_FONT_ID, rect.y + 5, truncatedTitle.c_str(), true, EpdFontFamily::REGULAR);
  }
}

void BaseTheme::drawTabBar(const GfxRenderer& renderer, const Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  constexpr int underlineHeight = 2;  // Height of selection underline
  constexpr int underlineGap = 4;     // Gap between text and underline

  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);

  int currentX = rect.x + BaseMetrics::values.contentSidePadding;

  for (const auto& tab : tabs) {
    const int textWidth = renderer.getTextWidth(
        UI_12_FONT_ID, tab.label, EpdFontFamily::REGULAR);

    // Draw underline for selected tab
    if (tab.selected) {
      if (selected) {
        renderer.fillRect(currentX - 3, rect.y, textWidth + 6, lineHeight + underlineGap);
      } else {
        renderer.fillRect(currentX, rect.y + lineHeight + underlineGap, textWidth, underlineHeight);
      }
    }

    // Draw tab label
    renderer.drawText(UI_12_FONT_ID, currentX, rect.y, tab.label, !(tab.selected && selected),
                      EpdFontFamily::REGULAR);

    currentX += textWidth + BaseMetrics::values.tabSpacing;
  }
}

// Draw the "Recent Book" cover card on the home screen
void BaseTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)coverRendered;
  (void)coverBufferStored;
  (void)bufferRestored;
  (void)storeCoverBuffer;

  auto buildAuthorInitials = [](const std::string& author) {
    std::string initials;
    bool newWord = true;
    for (const char ch : author) {
      if (ch == ' ' || ch == '\t') {
        newWord = true;
        continue;
      }
      if (newWord) {
        if (ch >= 'a' && ch <= 'z') {
          initials.push_back(static_cast<char>(ch - ('a' - 'A')));
        } else {
          initials.push_back(ch);
        }
        if (initials.size() >= 4) {
          break;
        }
        newWord = false;
      }
    }
    return initials;
  };

  const int maxRowsCap = std::max(1, UITheme::getInstance().getMetrics().homeRecentBooksCount);
  constexpr const char* placeholderLabel = "Open another book...";
  const int count = std::min(static_cast<int>(recentBooks.size()), maxRowsCap);
  const int visibleRows = std::max(1, count);
  constexpr int rowGap = 4;
  const int rowLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  constexpr int rowsTopInset = 18;
  constexpr int rowsBottomInset = 6;
  const int rowsTopMinY = rect.y + rowsTopInset;
  const int rowsBottomY = rect.y + rect.height - rowsBottomInset;
  const int rowsAvailableHeight = rowsBottomY - rowsTopMinY;
  const int availableRowW = std::max(1, rect.width - BaseMetrics::values.contentSidePadding * 2);
  constexpr int maxRowW = 520;
  const int rowW = std::min(availableRowW, maxRowW);
  const int rowX = rect.x + (rect.width - rowW) / 2;
  const int contentX = rowX + 12;
  const int contentW = std::max(1, rowW - 24);

  std::vector<std::vector<std::string>> rowLines(visibleRows);
  std::vector<int> rowHeights(visibleRows, rowLineHeight + 6);

  auto wrapText = [&](const std::string& input, const int maxWidth) {
    std::vector<std::string> lines;
    if (input.empty()) {
      lines.push_back("");
      return lines;
    }

    size_t i = 0;
    while (i < input.size()) {
      while (i < input.size() && input[i] == ' ') {
        i++;
      }
      if (i >= input.size()) {
        break;
      }

      std::string line;
      size_t lineEndPos = i;
      while (lineEndPos < input.size()) {
        size_t wordEnd = lineEndPos;
        while (wordEnd < input.size() && input[wordEnd] != ' ') {
          wordEnd++;
        }
        const std::string word = input.substr(lineEndPos, wordEnd - lineEndPos);
        const std::string candidate = line.empty() ? word : (line + " " + word);

        if (renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) <= maxWidth) {
          line = candidate;
          lineEndPos = wordEnd;
          while (lineEndPos < input.size() && input[lineEndPos] == ' ') {
            lineEndPos++;
          }
          continue;
        }

        if (line.empty()) {
          size_t fit = 1;
          while (fit < word.size() && renderer.getTextWidth(UI_10_FONT_ID, word.substr(0, fit + 1).c_str()) <= maxWidth) {
            fit++;
          }
          line = word.substr(0, fit);
          lineEndPos += fit;
        }
        break;
      }

      if (line.empty()) {
        line = renderer.truncatedText(UI_10_FONT_ID, input.substr(i).c_str(), maxWidth);
        lines.push_back(line);
        break;
      }

      lines.push_back(line);
      i = lineEndPos;
    }

    if (lines.empty()) {
      lines.push_back(renderer.truncatedText(UI_10_FONT_ID, input.c_str(), maxWidth));
    }
    return lines;
  };

  for (int i = 0; i < visibleRows; i++) {
    const bool hasBook = i < count;
    if (!hasBook) {
      rowLines[i].push_back(renderer.truncatedText(UI_10_FONT_ID, placeholderLabel, contentW));
      continue;
    }

    const std::string initials = buildAuthorInitials(recentBooks[i].author);
    const std::string rowText = initials.empty() ? recentBooks[i].title : (recentBooks[i].title + " by " + initials);
    rowLines[i] = wrapText(rowText, contentW);
    rowHeights[i] = static_cast<int>(rowLines[i].size()) * rowLineHeight + 6;
  }

  if (rowsAvailableHeight > 0) {
    int totalRowsHeight = 0;
    for (int i = 0; i < visibleRows; i++) {
      totalRowsHeight += rowHeights[i];
    }
    if (visibleRows > 1) {
      totalRowsHeight += (visibleRows - 1) * rowGap;
    }

    int rowY = rowsTopMinY;
    if (totalRowsHeight < rowsAvailableHeight) {
      rowY += (rowsAvailableHeight - totalRowsHeight) / 2;
    }

    for (int i = 0; i < visibleRows; i++) {
      const int rowHeight = rowHeights[i];
      if (rowY + rowHeight > rowsBottomY) {
        break;
      }
      const bool selected = (selectorIndex == i);
      const bool textBlack = !selected;

      if (selected) {
        renderer.fillRect(rowX, rowY, rowW, rowHeight, true);
      }

      int baselineY = rowY + 3;
      for (const auto& line : rowLines[i]) {
        renderer.drawText(UI_10_FONT_ID, contentX, baselineY, line.c_str(), textBlack);
        baselineY += rowLineHeight;
      }

      rowY += rowHeight + rowGap;
    }
  }
}

void BaseTheme::drawHomeInfoStatsPopup(const GfxRenderer& renderer) const {
  refreshHomeInfoStats();
  const auto& stats = getHomeInfoStats();

  const int popupW = std::min(renderer.getScreenWidth() - 20, 460);
  const int popupH = 165;
  const int popupX = (renderer.getScreenWidth() - popupW) / 2;
  const int popupY = (renderer.getScreenHeight() - popupH) / 2;
  const int textX = popupX + 12;
  constexpr int textPadY = 10;
  constexpr int lineGap = 6;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineStep = lineHeight + lineGap;
  const int textMaxWidth = popupW - 24;

  renderer.fillRect(popupX - 2, popupY - 2, popupW + 4, popupH + 4, true);
  renderer.fillRect(popupX, popupY, popupW, popupH, false);
  drawDashedRect(renderer, popupX, popupY, popupW, popupH);

  const uint64_t gbScale = 1024ull * 1024ull * 1024ull;
  const uint64_t freeTenthsGb = (stats.freeBytes * 10ull) / gbScale;
  const std::string line1Value = std::to_string(stats.bookCount);
  const std::string line2Value = std::to_string(stats.sleepBmpCount);
  const std::string line3Value = std::to_string(freeTenthsGb / 10ull) + "." + std::to_string(freeTenthsGb % 10ull) + " GB";

  auto drawStatLine = [&](const int y, const char* labelText, const std::string& valueRegular) {
    const std::string labelWithSeparator = std::string(labelText) + "   ";
    const std::string label =
        renderer.truncatedText(UI_12_FONT_ID, labelWithSeparator.c_str(), textMaxWidth, EpdFontFamily::REGULAR);
    const int labelWidth = renderer.getTextWidth(UI_12_FONT_ID, label.c_str(), EpdFontFamily::REGULAR);

    renderer.drawText(UI_12_FONT_ID, textX, y, label.c_str(), true, EpdFontFamily::REGULAR);

    const int remaining = textMaxWidth - labelWidth;
    if (remaining <= 0) return;

    const std::string valueBracketed = "[" + valueRegular + "]";
    const std::string value = renderer.truncatedText(UI_12_FONT_ID, valueBracketed.c_str(), remaining);
    renderer.drawText(UI_12_FONT_ID, textX + labelWidth, y, value.c_str(), true, EpdFontFamily::REGULAR);
  };

  const int textY = popupY + textPadY;
  drawStatLine(textY, "NUMBER OF BOOKS", line1Value);
  drawStatLine(textY + lineStep, "NUMBER OF WALLPAPERS", line2Value);
  drawStatLine(textY + lineStep * 2, "FREE SPACE IN SD CARD", line3Value);
}

void BaseTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<std::string(int index)>& rowIcon) const {
  for (int i = 0; i < buttonCount; ++i) {
    const int tileY = BaseMetrics::values.verticalSpacing + rect.y +
                      static_cast<int>(i) * (BaseMetrics::values.menuRowHeight + BaseMetrics::values.menuSpacing);

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, BaseMetrics::values.menuRowHeight);
    } else {
      renderer.drawRect(rect.x + BaseMetrics::values.contentSidePadding, tileY,
                        rect.width - BaseMetrics::values.contentSidePadding * 2, BaseMetrics::values.menuRowHeight);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, label);
    const int textX = rect.x + (rect.width - textWidth) / 2;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textY =
        tileY + (BaseMetrics::values.menuRowHeight - lineHeight) / 2;  // vertically centered assuming y is top of text
    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_10_FONT_ID, textX, textY, label, selectedIndex != i);
  }
}

Rect BaseTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  constexpr int margin = 15;
  constexpr int y = 60;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + margin * 2;
  const int h = textHeight + margin * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  renderer.fillRect(x - 2, y - 2, w + 4, h + 4, true);  // frame thickness 2
  renderer.fillRect(x, y, w, h, false);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + margin - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, true, EpdFontFamily::REGULAR);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  return Rect{x, y, w, h};
}

void BaseTheme::fillPopupProgress(const GfxRenderer& renderer, const Rect& layout, const int progress) const {
  constexpr int barHeight = 4;
  const int barWidth = layout.width - 30;  // twice the margin in drawPopup to match text width
  const int barX = layout.x + (layout.width - barWidth) / 2;
  const int barY = layout.y + layout.height - 10;

  int fillWidth = barWidth * progress / 100;

  renderer.fillRect(barX, barY, fillWidth, barHeight, true);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void BaseTheme::drawReadingProgressBar(const GfxRenderer& renderer, const size_t bookProgress) const {
  int vieweableMarginTop, vieweableMarginRight, vieweableMarginBottom, vieweableMarginLeft;
  renderer.getOrientedViewableTRBL(&vieweableMarginTop, &vieweableMarginRight, &vieweableMarginBottom,
                                   &vieweableMarginLeft);

  const int progressBarMaxWidth = renderer.getScreenWidth() - vieweableMarginLeft - vieweableMarginRight;
  const int progressBarY =
      renderer.getScreenHeight() - vieweableMarginBottom - BaseMetrics::values.bookProgressBarHeight;
  const int progressBarHeight = BaseMetrics::values.bookProgressBarHeight + vieweableMarginBottom;
  const int barWidth = progressBarMaxWidth * bookProgress / 100;
  renderer.fillRect(vieweableMarginLeft, progressBarY, barWidth, progressBarHeight, true);
}
