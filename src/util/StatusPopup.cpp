#include "StatusPopup.h"

#include <GfxRenderer.h>

#include "components/UITheme.h"

namespace StatusPopup {
namespace {

std::string toUpperAscii(std::string text) {
  for (char& c : text) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - ('a' - 'A'));
    }
  }
  return text;
}

void showBlockingImpl(GfxRenderer& renderer, const std::string& message) {
  if (message.empty()) {
    return;
  }

  const std::string uppercaseMessage = toUpperAscii(message);
  GUI.drawPopup(renderer, uppercaseMessage.c_str());
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

}  // namespace StatusPopup
