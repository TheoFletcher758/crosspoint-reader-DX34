#pragma once
#include "EpdFont.h"

class EpdFontFamily {
 public:
  enum Style : uint8_t { REGULAR = 0, BOLD = 1, ITALIC = 2, BOLD_ITALIC = 3, UNDERLINE = 4 };
  static void setReaderBoldSwapEnabled(bool enabled);
  static bool isReaderBoldSwapEnabled();

  explicit EpdFontFamily(const EpdFont* regular, const EpdFont* bold = nullptr, const EpdFont* italic = nullptr,
                         const EpdFont* boldItalic = nullptr, uint8_t syntheticRegularBoldPasses = 0,
                         uint8_t syntheticBoldExtraPasses = 0, bool syntheticItalic = false)
      : regular(regular),
        bold(bold),
        italic(italic),
        boldItalic(boldItalic),
        syntheticRegularBoldPasses(syntheticRegularBoldPasses),
        syntheticBoldExtraPasses(syntheticBoldExtraPasses),
        syntheticItalic(syntheticItalic) {}
  ~EpdFontFamily() = default;
  void getTextDimensions(const char* string, int* w, int* h, Style style = REGULAR) const;
  bool hasPrintableChars(const char* string, Style style = REGULAR) const;
  const EpdFontData* getData(Style style = REGULAR) const;
  const EpdGlyph* getGlyph(uint32_t cp, Style style = REGULAR) const;
  bool hasGlyph(uint32_t cp, Style style = REGULAR) const;
  uint8_t getSyntheticBoldPasses(Style style) const;
  bool shouldSynthesizeItalic(Style style) const;

 private:
  static bool readerBoldSwapEnabled;
  const EpdFont* regular;
  const EpdFont* bold;
  const EpdFont* italic;
  const EpdFont* boldItalic;
  uint8_t syntheticRegularBoldPasses;
  uint8_t syntheticBoldExtraPasses;
  bool syntheticItalic;

  const EpdFont* getFont(Style style) const;
  static Style remapStyleForReaderBoldSwap(Style style);
};
