#include "StatusPopup.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>

#include "components/UITheme.h"

namespace StatusPopup {
namespace {

std::string toUpperAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](const char c) {
    return (c >= 'a' && c <= 'z')
               ? static_cast<char>(c - ('a' - 'A'))
               : c;
  });
  return text;
}

void showBlockingImpl(const GfxRenderer& renderer, const std::string& message) {
  if (message.empty()) {
    return;
  }
  const std::string uppercaseMessage = toUpperAscii(message);
  GUI.drawPopup(renderer, uppercaseMessage.c_str());
}

}  // namespace

void showBlocking(const GfxRenderer& renderer, const std::string& message) {
  showBlockingImpl(renderer, message);
}

void showBlocking(const GfxRenderer& renderer, const char* message) {
  showBlockingImpl(renderer, message ? std::string(message) : std::string());
}

void showBlocking(const GfxRenderer& renderer, const String& message) {
  showBlockingImpl(renderer, std::string(message.c_str()));
}

}  // namespace StatusPopup
