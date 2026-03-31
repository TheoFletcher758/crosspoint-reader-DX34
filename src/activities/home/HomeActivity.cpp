#include "HomeActivity.h"
#include "activities/boot_sleep/SleepActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <vector>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"
#include "util/BookProgress.h"
#include "util/DrawUtils.h"
#include "util/FavoriteBmp.h"
#include "util/StringUtils.h"

namespace {
constexpr const char *seeMoreLabel = "See all...";

enum class HomeMenuAction : uint8_t {
  BrowseFiles,
  OpdsBrowser,
  FileTransfer,
  Settings,
};

struct HomeMenuItem {
  const char *label;
  HomeMenuAction action;
  bool emphasized;
};

std::string getHomeHeaderVersionLabel() {
  const std::string rawVersion = CROSSPOINT_VERSION;
  const size_t dashPos = rawVersion.find_last_of('-');
  const std::string semver =
      (dashPos != std::string::npos && dashPos + 1 < rawVersion.size())
          ? rawVersion.substr(dashPos + 1)
          : rawVersion;
  return "DX34 [" + semver + "]";
}

std::vector<HomeMenuItem> buildHomeMenuItems(bool hasOpdsUrl) {
  std::vector<HomeMenuItem> items;
  items.reserve(hasOpdsUrl ? 4 : 3);
  items.push_back(
      {tr(STR_BROWSE_FILES), HomeMenuAction::BrowseFiles, false});
  if (hasOpdsUrl) {
    items.push_back(
        {tr(STR_OPDS_BROWSER), HomeMenuAction::OpdsBrowser, false});
  }
  items.push_back(
      {tr(STR_FILE_TRANSFER), HomeMenuAction::FileTransfer, false});
  items.push_back({tr(STR_SETTINGS_TITLE), HomeMenuAction::Settings, false});
  return items;
}

// Use shared DrawUtils::drawDottedRect instead of local copy

} // namespace

int HomeActivity::getMenuItemCount() const {
  return getRecentSlotCount() + 1 +
         static_cast<int>(buildHomeMenuItems(hasOpdsUrl).size());
}

int HomeActivity::getRecentSlotCount() const {
  return std::max(1, static_cast<int>(recentBooks.size()));
}

void HomeActivity::refreshSleepFavoriteWarning() {
  protectedSleepFavoriteCount = FavoriteBmp::countProtectedSleepFavorites();
  sleepFavoritesFull =
      protectedSleepFavoriteCount >= CrossPointState::SLEEP_PLAYLIST_MAX_PERSIST;
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  if (maxBooks <= 0) {
    return;
  }

  const auto &books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(maxBooks);
  size_t eligibleCount = 0;
  const size_t maxVisibleBooks = static_cast<size_t>(maxBooks);
  std::unordered_set<std::string> seenPaths;
  seenPaths.reserve(books.size());

  for (const RecentBook &book : books) {
    if (!seenPaths.insert(book.path).second) {
      continue;
    }

    // Once we have enough visible books, we only need to confirm one more
    // eligible entry to show "See all...", then we can stop iterating.
    if (recentBooks.size() >= maxVisibleBooks) {
      if (Storage.exists(book.path.c_str())) {
        eligibleCount++;
        break;  // Found one more — enough to show "See all..."
      }
      continue;
    }

    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }

    eligibleCount++;
    const auto percent = BookProgress::getPercent(book.path);

    RecentBook bookWithoutCover = book;
    bookWithoutCover.title =
        (percent.has_value() ? std::to_string(percent.value()) : "0") +
        "%  " + book.title;
    // Home screen should never attempt to load/render cover images.
    bookWithoutCover.coverBmpPath.clear();
    recentBooks.push_back(bookWithoutCover);
  }

  if (eligibleCount > maxVisibleBooks && !recentBooks.empty()) {
    // Keep one slot for "See all..." when there are hidden ongoing books.
    if (recentBooks.size() == maxVisibleBooks) {
      recentBooks.pop_back();
    }
    RecentBook seeMoreRow;
    seeMoreRow.path.clear();
    seeMoreRow.title = seeMoreLabel;
    seeMoreRow.author.clear();
    seeMoreRow.coverBmpPath.clear();
    recentBooks.push_back(std::move(seeMoreRow));
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // Trim sleep folder once we reach home to avoid delays during boot/sleep entry
  SleepActivity::trimSleepFolderToLimit(&renderer);
  refreshSleepFavoriteWarning();

  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;

  selectorIndex = 0;

  auto metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);
  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t *frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t *>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t *frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const int recentSlots = getRecentSlotCount();
  const auto menuItems = buildHomeMenuItems(hasOpdsUrl);
  const int menuCount = getMenuItemCount();

  buttonNavigator.onNext([this, menuCount] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  buttonNavigator.onPrevious([this, menuCount] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectorIndex < recentBooks.size()) {
      const std::string &selectedPath = recentBooks[selectorIndex].path;
      if (selectedPath.empty()) {
        onRecentsOpen();
      } else {
        onSelectBook(selectedPath);
      }
    } else if (selectorIndex < recentSlots) {
      onMyLibraryOpen();
    } else if (selectorIndex == recentSlots) {
      APP_STATE.sessionPagesRead = 0;
      APP_STATE.saveToFile();
      requestUpdate();
    } else {
      const int menuSelectedIndex = selectorIndex - recentSlots - 1;
      if (menuSelectedIndex >= static_cast<int>(menuItems.size())) {
        return;
      }

      switch (menuItems[menuSelectedIndex].action) {
      case HomeMenuAction::BrowseFiles:
        onMyLibraryOpen();
        break;
      case HomeMenuAction::OpdsBrowser:
        onOpdsBrowserOpen();
        break;
      case HomeMenuAction::FileTransfer:
        onFileTransferOpen();
        break;
      case HomeMenuAction::Settings:
        onSettingsOpen();
        break;
      }
    }
  }
}

void HomeActivity::render(Activity::RenderLock &&) {
  auto metrics = UITheme::getInstance().getMetrics();
  const int recentSlots = getRecentSlotCount();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer,
                 Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding},
                 nullptr);
  const std::string homeVersionLabel = getHomeHeaderVersionLabel();
  const int versionY = metrics.topPadding + 5;
  renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, versionY,
                    homeVersionLabel.c_str());

  int warningBottomY =
      versionY + renderer.getLineHeight(UI_10_FONT_ID) + 12;
  if (sleepFavoritesFull) {
    const int warningY = warningBottomY;
    const int warningWidth = pageWidth - metrics.contentSidePadding * 2;
    const std::string warningText = renderer.truncatedText(
        SMALL_FONT_ID, FavoriteBmp::limitReachedHomeMessage(), warningWidth);
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, warningY,
                      warningText.c_str());
    warningBottomY = warningY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  }

  const int sessionStatFont = UI_10_FONT_ID;
  const int sessionStatY = warningBottomY;
  const std::string sessionPagesText =
      std::string(tr(STR_SESSION_PAGES)) + ": " +
      std::to_string(APP_STATE.sessionPagesRead);
  const bool pagesSelected = selectorIndex == recentSlots;
  const int pagesTextWidth =
      renderer.getTextWidth(sessionStatFont, sessionPagesText.c_str());
  const int pagesTileX = metrics.contentSidePadding;
  const int pagesTileWidth = pagesTextWidth + 16;
  const int pagesTileHeight = renderer.getLineHeight(sessionStatFont) + 6;
  const int pagesTileY = sessionStatY - 3;
  if (pagesSelected) {
    renderer.fillRect(pagesTileX, pagesTileY, pagesTileWidth, pagesTileHeight);
  } else {
    DrawUtils::drawDottedRect(renderer, pagesTileX, pagesTileY, pagesTileWidth,
                       pagesTileHeight);
  }
  renderer.drawText(sessionStatFont, pagesTileX + 8,
                    pagesTileY + 3, sessionPagesText.c_str(), !pagesSelected);
  warningBottomY =
      pagesTileY + pagesTileHeight + 9;

  const auto menuItems = buildHomeMenuItems(hasOpdsUrl);
  const int menuCount = static_cast<int>(menuItems.size());
  const int menuBlockHeight =
      metrics.verticalSpacing + menuCount * metrics.menuRowHeight +
      (menuCount > 0 ? (menuCount - 1) * metrics.menuSpacing : 0);
  const int menuBottomGap = 8; // Keep a small gap above bottom button hints.
  const int menuY =
      pageHeight - metrics.buttonHintsHeight - menuBottomGap - menuBlockHeight;

  const int recentAreaBottomGap = 8;
  const int recentAreaY = warningBottomY;
  const int recentAreaHeight =
      std::max(0, menuY - recentAreaBottomGap - recentAreaY);
  GUI.drawRecentBookCover(
      renderer, Rect{0, recentAreaY, pageWidth, recentAreaHeight}, recentBooks,
      selectorIndex, coverRendered, coverBufferStored, bufferRestored,
      std::bind(&HomeActivity::storeCoverBuffer, this));

  for (int i = 0; i < menuCount; ++i) {
    const int tileY =
        metrics.verticalSpacing + menuY +
        i * (metrics.menuRowHeight + metrics.menuSpacing);
    const int tileX = metrics.contentSidePadding;
    const int tileWidth = pageWidth - metrics.contentSidePadding * 2;
    const bool selected = selectorIndex - recentSlots - 1 == i;
    const bool emphasized = menuItems[i].emphasized;
    const auto textStyle =
        (emphasized && selected) ? EpdFontFamily::BOLD
                                 : EpdFontFamily::REGULAR;

    if (emphasized) {
      if (selected) {
        renderer.drawRect(tileX, tileY, tileWidth, metrics.menuRowHeight, 2,
                          true);
      } else {
        renderer.fillRect(tileX, tileY, tileWidth, metrics.menuRowHeight);
      }
    } else if (selected) {
      renderer.fillRect(tileX, tileY, tileWidth, metrics.menuRowHeight);
    } else {
      renderer.drawRect(tileX, tileY, tileWidth, metrics.menuRowHeight);
    }

    const int textWidth =
        renderer.getTextWidth(UI_10_FONT_ID, menuItems[i].label, textStyle);
    const int textX = (pageWidth - textWidth) / 2;
    const int textY =
        tileY +
        (metrics.menuRowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    const bool blackText = emphasized ? selected : !selected;
    renderer.drawText(UI_10_FONT_ID, textX, textY, menuItems[i].label,
                      blackText, textStyle);
  }

  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                      labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  }
}
