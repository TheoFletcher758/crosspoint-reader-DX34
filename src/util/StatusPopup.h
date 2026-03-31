#pragma once

#include <WString.h>

#include <string>

class GfxRenderer;

namespace StatusPopup {

void showBlocking(const GfxRenderer& renderer, const std::string& message);
void showBlocking(const GfxRenderer& renderer, const char* message);
void showBlocking(const GfxRenderer& renderer, const String& message);

}  // namespace StatusPopup
