#pragma once
#include <Epub.h>
#include <Epub/Section.h>

#include <array>

#include "EpubReaderMenuActivity.h"
#include "activities/ActivityWithSubactivity.h"

class EpubReaderActivity final : public ActivityWithSubactivity {
  struct StatusBarLayout {
    int topReservedHeight = 0;
    int bottomReservedHeight = 0;
    int usableWidth = 0;
    std::string pageCounterText;
    int pageCounterTextWidth = 0;
    std::string bookPercentageText;
    int bookPercentageTextWidth = 0;
    std::string chapterPercentageText;
    int chapterPercentageTextWidth = 0;
    std::vector<std::string> titleLines;
    std::vector<int> titleLineWidths;
    float bookProgress = 0.0f;
    float chapterProgress = 0.0f;
  };

  struct PageCacheEntry {
    int pageIndex = -1;
    std::shared_ptr<Page> page;
  };

  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  std::string pendingAnchor;
  bool pendingSubactivityExit = false;  // Defer subactivity exit to avoid use-after-free
  bool pendingGoHome = false;           // Defer go home to avoid race condition with display task
  bool pendingGoLibrary = false;        // Defer go library after destructive actions
  bool skipNextButtonCheck = false;     // Skip button processing for one frame after subactivity exit
  bool pendingMenuOpen = false;
  unsigned long lastConfirmReleaseMs = 0;
  bool confirmLongPressHandled = false;
  bool suppressNextConfirmRelease = false;
  bool progressDirty = false;
  unsigned long lastProgressChangeMs = 0;
  int lastObservedSpineIndex = -1;
  int lastObservedPage = -1;
  int lastObservedPageCount = -1;
  int lastSavedSpineIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;
  int pageLoadFailCount = 0;  // Tracks consecutive page load failures to prevent infinite retry loops
  int cachedReserveSpineIndex = -1;
  int cachedReserveUsableWidth = -1;
  bool cachedReserveNoTitleTruncation = false;
  int cachedReserveTitleLineCount = 1;
  int cachedTitleTocIndex = -2;
  int cachedTitleUsableWidth = -1;
  bool cachedTitleNoTitleTruncation = false;
  int cachedTitleMaxLines = -1;
  std::vector<std::string> cachedTitleLines;
  int pageCacheSpineIndex = -1;
  std::array<PageCacheEntry, 3> pageCache;
  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;
  const std::function<void(const std::string&)> onOpenBook;

  void renderContents(const Page& page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft, const StatusBarLayout& statusBarLayout);
  void renderStatusBar(const StatusBarLayout& statusBarLayout, int orientedMarginRight, int orientedMarginBottom,
                       int orientedMarginLeft);
  void silentIndexNextChapterIfNeeded(uint16_t viewportWidth, uint16_t viewportHeight);
  void saveProgress(int spineIndex, int currentPage, int pageCount);
  void flushProgressIfNeeded(bool force);
  void invalidateStatusBarCaches();
  void clearPageCache();
  std::shared_ptr<Page> getCachedPage(int pageIndex) const;
  std::shared_ptr<Page> loadAndCachePage(int pageIndex);
  void refreshPageCacheWindow(int centerPage, const std::shared_ptr<Page>& currentPage);
  int getWrappedStatusBarReserveLineCount(int usableWidth);
  const std::vector<std::string>& getStatusBarTitleLines(int tocIndex, int usableWidth, bool noTitleTruncation,
                                                         int maxTitleLineCount);
  StatusBarLayout buildStatusBarLayout(int usableWidth, int topReservedHeight,
                                       int bottomReservedHeight, int maxTitleLineCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuBack(uint8_t orientation);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);
  void reloadCurrentSectionForDisplaySettings();
  void openReaderMenu();
  void toggleReaderBoldSwap();
  void addSessionPagesRead(uint32_t amount = 1);

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                              const std::function<void()>& onGoBack, const std::function<void()>& onGoHome,
                              const std::function<void(const std::string&)>& onOpenBook)
      : ActivityWithSubactivity("EpubReader", renderer, mappedInput),
        epub(std::move(epub)),
        onGoBack(onGoBack),
        onGoHome(onGoHome),
        onOpenBook(onOpenBook) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&& lock) override;
};
