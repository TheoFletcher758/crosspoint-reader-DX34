#include "BootActivity.h"

#include <algorithm>

#include <HalDisplay.h>
#include <GfxRenderer.h>

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
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - kVersionTextBottom,
                            CROSSPOINT_VERSION);
}

void BootActivity::drawDynamicBootScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int centeredY = (pageHeight - kBootImageHeight) / 2;
  const int y = centeredY + kBootImageVerticalOffset;
  const int barX = (pageWidth - kProgressBarWidth) / 2;
  const int barY = y + kBootImageHeight + kProgressBarTopGap;
  const int innerX = barX + kProgressBarInnerPadding;
  const int innerY = barY + kProgressBarInnerPadding;
  const int innerWidth = kProgressBarWidth - kProgressBarInnerPadding * 2;
  const int innerHeight = kProgressBarHeight - kProgressBarInnerPadding * 2;
  const int fillWidth = innerWidth * progressPercent / 100;
  const int dynamicRegionTop = barY - kDynamicRegionPadding;
  const int dynamicRegionBottom =
      barY + kProgressBarHeight + kDynamicRegionPadding;

  renderer.fillRect(barX - kDynamicRegionPadding, dynamicRegionTop,
                    kProgressBarWidth + kDynamicRegionPadding * 2,
                    dynamicRegionBottom - dynamicRegionTop, false);
  renderer.drawRect(barX, barY, kProgressBarWidth, kProgressBarHeight, true);
  renderer.fillRect(barX + kProgressBarBorderWidth,
                    barY + kProgressBarBorderWidth,
                    kProgressBarWidth - kProgressBarBorderWidth * 2,
                    kProgressBarHeight - kProgressBarBorderWidth * 2, false);

  if (fillWidth > 0) {
    renderer.fillRect(innerX, innerY, fillWidth, innerHeight, true);
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
