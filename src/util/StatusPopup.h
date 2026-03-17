#pragma once

#include <WString.h>

#include <string>

class GfxRenderer;

namespace StatusPopup {

void showBlocking(const GfxRenderer& renderer, const std::string& message);
void showBlocking(const GfxRenderer& renderer, const char* message);
void showBlocking(const GfxRenderer& renderer, const String& message);
void showBottomProgress(GfxRenderer& renderer, const std::string& message,
                        int progress);
void showBottomProgress(GfxRenderer& renderer, const char* message,
                        int progress);
void showBottomProgress(GfxRenderer& renderer, const String& message,
                        int progress);

}  // namespace StatusPopup
