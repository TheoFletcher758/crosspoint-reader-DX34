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
#include "util/StringUtils.h"

namespace {
constexpr const char *seeMoreLabel = "See all...";

std::string getHomeHeaderVersionLabel() {
  const std::string rawVersion = CROSSPOINT_VERSION;
  const size_t dashPos = rawVersion.find_last_of('-');
  const std::string semver =
      (dashPos != std::string::npos && dashPos + 1 < rawVersion.size())
          ? rawVersion.substr(dashPos + 1)
          : rawVersion;
  return "DX34 [" + semver + "]";
}

} // namespace

int HomeActivity::getMenuItemCount() const {
  const int recentSlots = getRecentSlotCount();
  int count = 3; // Browse files, file transfer, settings
  count += recentSlots;
  if (hasOpdsUrl) {
    count++;
  }
  return count;
}

int HomeActivity::getRecentSlotCount() const {
  return std::max(1, static_cast<int>(recentBooks.size()));
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
    // Skip if file no longer exists
    if (!Storage.exists(book.path.c_str())) {
      continue;
    }
    const auto percent = BookProgress::getPercent(book.path);

    if (!seenPaths.insert(book.path).second) {
      continue;
    }

    eligibleCount++;

    if (recentBooks.size() < maxVisibleBooks) {
      RecentBook bookWithoutCover = book;
      bookWithoutCover.title =
          (percent.has_value() ? std::to_string(percent.value()) : "0") +
          "%  " + book.title;
      // Home screen should never attempt to load/render cover images.
      bookWithoutCover.coverBmpPath.clear();
      recentBooks.push_back(bookWithoutCover);
    }
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

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook &book : recentBooks) {
    if (!book.coverBmpPath.empty()) {
      std::string coverPath =
          UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub, try to load the metadata for title/author and cover
        if (StringUtils::checkFileExtension(book.path, ".epub")) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING));
          }
          GUI.fillPopupProgress(renderer, popupRect,
                                10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(coverHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
                   StringUtils::checkFileExtension(book.path, ".xtc")) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING));
            }
            GUI.fillPopupProgress(renderer, popupRect,
                                  10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(coverHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  // Trim sleep folder once we reach home to avoid delays during boot/sleep entry
  SleepActivity::trimSleepFolderToLimit();

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
    // Calculate dynamic indices based on which options are available
    int idx = 0;
    int menuSelectedIndex = selectorIndex - recentSlots;
    const int myLibraryIdx = idx++;
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx++;

    if (selectorIndex < recentBooks.size()) {
      const std::string &selectedPath = recentBooks[selectorIndex].path;
      if (selectedPath.empty()) {
        onRecentsOpen();
      } else {
        onSelectBook(selectedPath);
      }
    } else if (selectorIndex < recentSlots) {
      onMyLibraryOpen();
    } else if (menuSelectedIndex == myLibraryIdx) {
      onMyLibraryOpen();
    } else if (menuSelectedIndex == opdsLibraryIdx) {
      onOpdsBrowserOpen();
    } else if (menuSelectedIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
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
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding,
                    metrics.topPadding + 5, homeVersionLabel.c_str());

  // Build menu items dynamically
  std::vector<const char *> menuItems = {
      tr(STR_BROWSE_FILES), tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE)};
  if (hasOpdsUrl) {
    // Insert OPDS Browser after Browse Files.
    menuItems.insert(menuItems.begin() + 1, tr(STR_OPDS_BROWSER));
  }

  const int menuCount = static_cast<int>(menuItems.size());
  const int menuBlockHeight =
      metrics.verticalSpacing + menuCount * metrics.menuRowHeight +
      (menuCount > 0 ? (menuCount - 1) * metrics.menuSpacing : 0);
  const int menuBottomGap = 8; // Keep a small gap above bottom button hints.
  const int menuY =
      pageHeight - metrics.buttonHintsHeight - menuBottomGap - menuBlockHeight;

  const int recentAreaBottomGap = 8;
  const int recentAreaY = metrics.homeTopPadding;
  const int recentAreaHeight =
      std::max(0, menuY - recentAreaBottomGap - recentAreaY);
  GUI.drawRecentBookCover(
      renderer, Rect{0, recentAreaY, pageWidth, recentAreaHeight}, recentBooks,
      selectorIndex, coverRendered, coverBufferStored, bufferRestored,
      std::bind(&HomeActivity::storeCoverBuffer, this));

  GUI.drawButtonMenu(
      renderer, Rect{0, menuY, pageWidth, menuBlockHeight}, menuCount,
      selectorIndex - recentSlots,
      [&menuItems](int index) { return std::string(menuItems[index]); },
      nullptr);

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
