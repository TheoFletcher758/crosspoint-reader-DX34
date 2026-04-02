#pragma once

#include <Txt.h>

#include <vector>

#include "CrossPointSettings.h"
#include "activities/ActivityWithSubactivity.h"

struct RecentBook;

class TxtReaderActivity final : public ActivityWithSubactivity {
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
    float progress = 0.0f;
  };
  struct FlowLine {
    std::string text;
    bool firstInParagraph = false;
    bool lastInParagraph = false;
  };

  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;
  const std::function<void(const std::string&)> onOpenBook;
  bool recentSwitcherOpen = false;
  bool pendingThemesOpen = false;
  bool pendingSubactivityExit = false;
  bool skipNextButtonCheck = false;
  bool confirmLongPressHandled = false;
  unsigned long lastConfirmReleaseMs = 0;
  bool suppressNextConfirmRelease = false;
  bool progressDirty = false;
  unsigned long lastProgressChangeMs = 0;
  int lastObservedPage = -1;
  int lastSavedPage = -1;
  int recentSwitcherSelection = 0;
  std::vector<RecentBook> recentSwitcherBooks;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  std::vector<FlowLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;
  int pendingRelayoutPage = -1;
  int pendingRelayoutPageCount = 0;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  int cachedScreenMarginHorizontal = 0;
  int cachedScreenMarginTop = 0;
  int cachedScreenMarginBottom = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  uint8_t cachedLineSpacingPercent = 110;
  uint8_t cachedWordSpacingPercent = 100;
  uint8_t cachedFirstLineIndentMode = CrossPointSettings::INDENT_BOOK;
  int cachedTitleUsableWidth = -1;
  bool cachedTitleNoTitleTruncation = false;
  int cachedTitleMaxLines = -1;
  std::vector<std::string> cachedTitleLines;

  void renderPage();
  void renderStatusBar(const StatusBarLayout& statusBarLayout, int orientedMarginRight, int orientedMarginBottom,
                       int orientedMarginLeft);
  void renderRecentSwitcher();
  void openReadingThemes();
  void reloadCurrentLayoutForDisplaySettings();
  int getReaderLineHeightPx() const;
  int getTxtWordSpaceWidth() const;
  int getTxtParagraphIndentPx() const;
  int measureFlowLineWidth(const std::string& text) const;
  void drawFlowLine(const FlowLine& line, int x, int y, int contentWidth) const;
  const std::vector<std::string>& getStatusBarTitleLines(int usableWidth,
                                                         bool noTitleTruncation,
                                                         int maxTitleLineCount);
  int getStatusBarReserveTitleLineCount(int usableWidth, bool noTitleTruncation);
  StatusBarLayout buildStatusBarLayout(int usableWidth, int topReservedHeight,
                                       int bottomReservedHeight,
                                       int maxTitleLineCount);

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<FlowLine>& outLines,
                        size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void flushProgressIfNeeded(bool force);
  void loadProgress();
  void toggleTextRenderMode();

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                             const std::function<void()>& onGoBack, const std::function<void()>& onGoHome,
                             const std::function<void(const std::string&)>& onOpenBook)
      : ActivityWithSubactivity("TxtReader", renderer, mappedInput),
        txt(std::move(txt)),
        onGoBack(onGoBack),
        onGoHome(onGoHome),
        onOpenBook(onOpenBook) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
};
