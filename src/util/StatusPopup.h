#pragma once

#include <WString.h>

#include <string>

class GfxRenderer;

namespace StatusPopup {

void showBlocking(GfxRenderer& renderer, const std::string& message);
void showBlocking(GfxRenderer& renderer, const char* message);
void showBlocking(GfxRenderer& renderer, const String& message);

}  // namespace StatusPopup
