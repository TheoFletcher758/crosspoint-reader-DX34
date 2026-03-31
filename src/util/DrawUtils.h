#pragma once

#include <GfxRenderer.h>

namespace DrawUtils {

// Draw a dotted (alternating pixel) rectangle outline.
inline void drawDottedRect(const GfxRenderer& renderer, int x, int y, int w, int h) {
  const int x2 = x + w - 1;
  const int y2 = y + h - 1;
  for (int px = x; px <= x2; px += 2) {
    renderer.drawPixel(px, y);
    renderer.drawPixel(px, y2);
  }
  for (int py = y + 1; py < y2; py += 2) {
    renderer.drawPixel(x, py);
    renderer.drawPixel(x2, py);
  }
}

}  // namespace DrawUtils
