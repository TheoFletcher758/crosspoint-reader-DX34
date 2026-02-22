#pragma once

#include <functional>
#include <vector>

#include "components/themes/BaseTheme.h"

class UITheme {
  // Static instance
  static UITheme instance;

 public:
  UITheme();
  static UITheme& getInstance() { return instance; }

  const ThemeMetrics& getMetrics() { return *currentMetrics; }
  const BaseTheme& getTheme() { return *currentTheme; }
  static int getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle);
  static std::string getCoverThumbPath(std::string coverBmpPath, int coverHeight);

 private:
  const ThemeMetrics* currentMetrics;
  const BaseTheme* currentTheme;
};

// Helper macro to access current theme
#define GUI UITheme::getInstance().getTheme()
