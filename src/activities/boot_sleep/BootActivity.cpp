#include "BootActivity.h"

#include <algorithm>
#include <string>

#include <HalDisplay.h>
#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "fontIds.h"
#include "images/BootImage.h"

namespace {

constexpr int kBootImageVerticalOffset = -24;
constexpr int kProgressBarWidth = 300;
constexpr int kProgressBarHeight = 18;
constexpr int kProgressBarBorderWidth = 1;
constexpr int kProgressBarInnerPadding = 3;
constexpr int kProgressBarTopGap = 34;
constexpr int kVersionTextBottom = 30;
constexpr int kDynamicRegionPadding = 6;

int clampPercent(const int percent) {
  return std::max(0, std::min(100, percent));
}

std::string bootVersionText() {
  constexpr const char* kPrefix = "CrossPoint-Mod-DX34-";
  std::string version = CROSSPOINT_VERSION;
  if (version.rfind(kPrefix, 0) == 0) {
    version.erase(0, std::char_traits<char>::length(kPrefix));
  }
  return "[MOD] DX34 " + version;
}

}

void BootActivity::onEnter() {
  renderEmbeddedBootScreen(HalDisplay::HALF_REFRESH, true);
}

void BootActivity::setProgress(const int percent, const char* status) {
  progressPercent = clampPercent(percent);
  (void)status;
  renderEmbeddedBootScreen(HalDisplay::FAST_REFRESH, false);
}

void BootActivity::drawStaticBootScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int x = (pageWidth - kBootImageWidth) / 2;
  const int centeredY = (pageHeight - kBootImageHeight) / 2;
  const int y = centeredY + kBootImageVerticalOffset;
  renderer.clearScreen();
  renderer.drawImage(BootImage, x, y, kBootImageWidth, kBootImageHeight);
  const std::string versionText = bootVersionText();
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - kVersionTextBottom,
                            versionText.c_str());
}

void BootActivity::drawDynamicBootScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int centeredY = (pageHeight - kBootImageHeight) / 2;
  const int y = centeredY + kBootImageVerticalOffset;
  const int barX = (pageWidth - kProgressBarWidth) / 2;
  const int barY = y + kBootImageHeight + kProgressBarTopGap;
  const int dynamicRegionTop = barY - kDynamicRegionPadding;
  const int dynamicRegionBottom =
      barY + kProgressBarHeight + kDynamicRegionPadding;

  // Clear the dynamic region
  renderer.fillRect(barX - kDynamicRegionPadding, dynamicRegionTop,
                    kProgressBarWidth + kDynamicRegionPadding * 2,
                    dynamicRegionBottom - dynamicRegionTop, false);

  const uint8_t style = SETTINGS.loadingBarStyle;

  if (style == CrossPointSettings::LOADING_BAR_STRIPED) {
    // Outlined box with vertical stripe fill
    renderer.drawRect(barX, barY, kProgressBarWidth, kProgressBarHeight, true);
    renderer.fillRect(barX + kProgressBarBorderWidth,
                      barY + kProgressBarBorderWidth,
                      kProgressBarWidth - kProgressBarBorderWidth * 2,
                      kProgressBarHeight - kProgressBarBorderWidth * 2, false);
    const int innerX = barX + kProgressBarInnerPadding;
    const int innerY = barY + kProgressBarInnerPadding;
    const int innerWidth = kProgressBarWidth - kProgressBarInnerPadding * 2;
    const int innerHeight = kProgressBarHeight - kProgressBarInnerPadding * 2;
    const int fillWidth = innerWidth * progressPercent / 100;
    constexpr int stripeW = 3;
    constexpr int stripeGap = 3;
    for (int sx = 0; sx < fillWidth; sx += stripeW + stripeGap) {
      const int w = std::min(stripeW, fillWidth - sx);
      renderer.fillRect(innerX + sx, innerY, w, innerHeight, true);
    }
  } else if (style == CrossPointSettings::LOADING_BAR_SEGMENTED) {
    // Discrete blocks with gaps
    constexpr int segCount = 20;
    constexpr int segGap = 4;
    const int segWidth =
        (kProgressBarWidth - (segCount - 1) * segGap) / segCount;
    const int filledSegs = segCount * progressPercent / 100;
    for (int i = 0; i < segCount; ++i) {
      const int sx = barX + i * (segWidth + segGap);
      if (i < filledSegs) {
        renderer.fillRect(sx, barY, segWidth, kProgressBarHeight, true);
      } else {
        renderer.drawRect(sx, barY, segWidth, kProgressBarHeight, true);
      }
    }
  } else {
    // Default: LOADING_BAR_OUTLINED — outlined box with inner fill
    renderer.drawRect(barX, barY, kProgressBarWidth, kProgressBarHeight, true);
    renderer.fillRect(barX + kProgressBarBorderWidth,
                      barY + kProgressBarBorderWidth,
                      kProgressBarWidth - kProgressBarBorderWidth * 2,
                      kProgressBarHeight - kProgressBarBorderWidth * 2, false);
    const int innerX = barX + kProgressBarInnerPadding;
    const int innerY = barY + kProgressBarInnerPadding;
    const int innerWidth = kProgressBarWidth - kProgressBarInnerPadding * 2;
    const int innerHeight = kProgressBarHeight - kProgressBarInnerPadding * 2;
    const int fillWidth = innerWidth * progressPercent / 100;
    if (fillWidth > 0) {
      renderer.fillRect(innerX, innerY, fillWidth, innerHeight, true);
    }
  }
}

void BootActivity::renderEmbeddedBootScreen(
    const HalDisplay::RefreshMode refreshMode, const bool fullRedraw) const {
  if (fullRedraw) {
    drawStaticBootScreen();
  }
  drawDynamicBootScreen();
  renderer.displayBuffer(refreshMode);
}
