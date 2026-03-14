#include "StatusPopup.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>

#include "components/UITheme.h"

namespace StatusPopup {
namespace {

constexpr int kBottomPopupMarginX = 18;
constexpr int kBottomPopupMarginBottom = 18;
constexpr int kBottomPopupBarHeight = 16;
constexpr int kBottomPopupBarInnerMargin = 5;
constexpr int kBottomPopupRegionPadding = 6;
int clampPercent(const int progress) {
  return std::max(0, std::min(100, progress));
}

std::string toUpperAscii(std::string text) {
  for (char& c : text) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - ('a' - 'A'));
    }
  }
  return text;
}

void showBottomProgressImpl(GfxRenderer& renderer, const std::string& message,
                            int progress);

void showBlockingImpl(GfxRenderer& renderer, const std::string& message) {
  if (message.empty()) {
    return;
  }
  const std::string uppercaseMessage = toUpperAscii(message);
  GUI.drawPopup(renderer, uppercaseMessage.c_str());
}

void showBottomProgressImpl(GfxRenderer& renderer, const std::string& message,
                            const int progress) {
  if (message.empty()) {
    return;
  }

  int viewableTop, viewableRight, viewableBottom, viewableLeft;
  renderer.getOrientedViewableTRBL(&viewableTop, &viewableRight, &viewableBottom,
                                   &viewableLeft);
  (void)viewableTop;

  const int clampedProgress = clampPercent(progress);
  const int barWidth =
      renderer.getScreenWidth() - viewableLeft - viewableRight -
      kBottomPopupMarginX * 2;
  const int barX = (renderer.getScreenWidth() - barWidth) / 2;
  const int barY = renderer.getScreenHeight() - viewableBottom -
                   kBottomPopupMarginBottom - kBottomPopupBarHeight;
  const int regionTop = barY - kBottomPopupRegionPadding;
  const int regionBottom =
      barY + kBottomPopupBarHeight + kBottomPopupRegionPadding;
  const int innerX = barX + kBottomPopupBarInnerMargin;
  const int innerY = barY + kBottomPopupBarInnerMargin;
  const int innerWidth = barWidth - kBottomPopupBarInnerMargin * 2;
  const int innerHeight = kBottomPopupBarHeight - kBottomPopupBarInnerMargin * 2;
  const int fillWidth = innerWidth * clampedProgress / 100;

  renderer.fillRect(0, regionTop, renderer.getScreenWidth(),
                    regionBottom - regionTop, false);
  renderer.drawRect(barX, barY, barWidth, kBottomPopupBarHeight, true);
  renderer.fillRect(barX + 1, barY + 1, barWidth - 2, kBottomPopupBarHeight - 2,
                    false);

  if (fillWidth > 0) {
    renderer.fillRect(innerX, innerY, fillWidth, innerHeight, true);
  }

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

}  // namespace

void showBlocking(GfxRenderer& renderer, const std::string& message) {
  showBlockingImpl(renderer, message);
}

void showBlocking(GfxRenderer& renderer, const char* message) {
  showBlockingImpl(renderer, message ? std::string(message) : std::string());
}

void showBlocking(GfxRenderer& renderer, const String& message) {
  showBlockingImpl(renderer, std::string(message.c_str()));
}

void showBottomProgress(GfxRenderer& renderer, const std::string& message,
                        const int progress) {
  showBottomProgressImpl(renderer, message, progress);
}

void showBottomProgress(GfxRenderer& renderer, const char* message,
                        const int progress) {
  showBottomProgressImpl(renderer,
                         message ? std::string(message) : std::string(),
                         progress);
}

void showBottomProgress(GfxRenderer& renderer, const String& message,
                        const int progress) {
  showBottomProgressImpl(renderer, std::string(message.c_str()), progress);
}

}  // namespace StatusPopup
