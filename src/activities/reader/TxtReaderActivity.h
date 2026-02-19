#pragma once

#include <Txt.h>

#include <vector>

#include "CrossPointSettings.h"
#include "activities/ActivityWithSubactivity.h"

struct RecentBook;

class TxtReaderActivity final : public ActivityWithSubactivity {
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;
  const std::function<void(const std::string&)> onOpenBook;
  bool recentSwitcherOpen = false;
  bool pendingSingleBack = false;
  unsigned long lastBackReleaseMs = 0;
  bool confirmLongPressHandled = false;
  bool progressDirty = false;
  unsigned long lastProgressChangeMs = 0;
  int lastObservedPage = -1;
  int lastSavedPage = -1;
  int recentSwitcherSelection = 0;
  std::vector<RecentBook> recentSwitcherBooks;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  std::vector<std::string> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  int cachedScreenMarginHorizontal = 0;
  int cachedScreenMarginTop = 0;
  int cachedScreenMarginBottom = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;

  void renderPage();
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;
  void loadRecentSwitcherBooks();
  void renderRecentSwitcher();

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void flushProgressIfNeeded(bool force);
  void loadProgress();

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
