#include "EpdFont.h"

#include <Utf8.h>

#include <algorithm>

void EpdFont::getTextBounds(const char *string, const int startX,
                            const int startY, int *minX, int *minY, int *maxX,
                            int *maxY) const {
  *minX = startX;
  *minY = startY;
  *maxX = startX;
  *maxY = startY;

  if (*string == '\0') {
    return;
  }

  int cursorX = startX;
  const int cursorY = startY;
  uint32_t cp;
  while (
      (cp = utf8NextCodepoint(reinterpret_cast<const uint8_t **>(&string)))) {
    const EpdGlyph *glyph = getGlyph(cp);

    if (!glyph) {
      glyph = getGlyph(REPLACEMENT_GLYPH);
    }

    if (!glyph) {
      // TODO: Better handle this?
      continue;
    }

    *minX = std::min(*minX, cursorX + glyph->left);
    *maxX = std::max(*maxX, cursorX + glyph->left + glyph->width);
    *minY = std::min(*minY, cursorY + glyph->top - glyph->height);
    *maxY = std::max(*maxY, cursorY + glyph->top);
    cursorX += glyph->advanceX;
  }
}

void EpdFont::getTextDimensions(const char *string, int *w, int *h) const {
  int minX = 0, minY = 0, maxX = 0, maxY = 0;

  getTextBounds(string, 0, 0, &minX, &minY, &maxX, &maxY);

  *w = maxX - minX;
  *h = maxY - minY;
}

bool EpdFont::hasPrintableChars(const char *string) const {
  int w = 0, h = 0;
  getTextDimensions(string, &w, &h);
  return w > 0 || h > 0;
}

const EpdGlyph *EpdFont::getGlyph(const uint32_t cp) const {
  const int count = data->intervalCount;
  if (count == 0)
    return nullptr;

  const EpdUnicodeInterval *intervals = data->intervals;
  const auto *end = intervals + count;

  // upper_bound: range lookup. Finds the first interval with first > cp, so the
  // interval just before it is the last one with first <= cp. That's the only
  // candidate that could contain cp. Then we verify cp <= candidate.last.
  const auto it =
      std::upper_bound(intervals, end, cp,
                       [](uint32_t value, const EpdUnicodeInterval &interval) {
                         return value < interval.first;
                       });

  if (it != intervals) {
    const auto &interval = *(it - 1);
    if (cp <= interval.last) {
      return &data->glyph[interval.offset + (cp - interval.first)];
    }
  }

  if (cp != REPLACEMENT_GLYPH) {
    return getGlyph(REPLACEMENT_GLYPH);
  }
  return nullptr;
}
