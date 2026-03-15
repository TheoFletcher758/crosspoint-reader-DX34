#include "ParsedText.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {
constexpr uint8_t WORD_SPACING_LEVEL_MIN = 8;
constexpr uint8_t WORD_SPACING_LEVEL_MAX = 19;
constexpr uint8_t WORD_SPACING_LEVEL_DEFAULT = 13;

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

uint8_t normalizeWordSpacingSetting(const uint8_t raw) {
  if (raw >= WORD_SPACING_LEVEL_MIN && raw <= WORD_SPACING_LEVEL_MAX) {
    return raw;
  }
  if (raw <= 6) {
    const int migrated = static_cast<int>(raw) + 10;
    return static_cast<uint8_t>(
        std::max(static_cast<int>(WORD_SPACING_LEVEL_MIN),
                 std::min(static_cast<int>(WORD_SPACING_LEVEL_MAX), migrated)));
  }

  const int delta = static_cast<int>(raw) - 100;
  const int roundedDelta = delta >= 0 ? (delta + 5) / 10 : (delta - 5) / 10;
  int level = static_cast<int>(WORD_SPACING_LEVEL_DEFAULT) + roundedDelta;
  if (level < static_cast<int>(WORD_SPACING_LEVEL_MIN)) {
    level = WORD_SPACING_LEVEL_MIN;
  } else if (level > static_cast<int>(WORD_SPACING_LEVEL_MAX)) {
    level = WORD_SPACING_LEVEL_MAX;
  }
  return static_cast<uint8_t>(level);
}

int wordSpacingSettingToPixelDelta(const uint8_t raw) {
  return static_cast<int>(normalizeWordSpacingSetting(raw)) -
         static_cast<int>(WORD_SPACING_LEVEL_DEFAULT);
}

bool containsSoftHyphen(const std::string &word) {
  return word.find(SOFT_HYPHEN_UTF8) != std::string::npos;
}

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string &word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the rendered width for a word while ignoring soft hyphen glyphs and
// optionally appending a visible hyphen.
uint16_t measureWordWidth(const GfxRenderer &renderer, const int fontId,
                          const std::string &word,
                          const EpdFontFamily::Style style,
                          const int letterSpacing,
                          const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextWidthSpaced(fontId, word.c_str(), letterSpacing,
                                       style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextWidthSpaced(fontId, sanitized.c_str(), letterSpacing,
                                     style);
}

float indentMultiplierForMode(const uint8_t indentMode) {
  switch (indentMode) {
  case 2:
    return 0.6f;
  case 3:
    return 1.0f;
  case 4:
    return 1.4f;
  default:
    return 0.0f;
  }
}

} // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle,
                         const bool underline, const bool attachToPrevious) {
  if (word.empty())
    return;

  words.push_back(std::move(word));
  EpdFontFamily::Style combinedStyle = fontStyle;
  if (underline) {
    combinedStyle = static_cast<EpdFontFamily::Style>(combinedStyle |
                                                      EpdFontFamily::UNDERLINE);
  }
  wordStyles.push_back(combinedStyle);
  wordContinues.push_back(attachToPrevious);
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(
    const GfxRenderer &renderer, const int fontId, const uint16_t viewportWidth,
    const std::function<void(std::shared_ptr<TextBlock>)> &processLine,
    const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  applyParagraphIndent(renderer, fontId);

  const int pageWidth = viewportWidth;
  const int baseSpaceWidth = renderer.getSpaceWidth(fontId);
  const int userSpaceWidth =
      std::max(1, baseSpaceWidth + wordSpacingSettingToPixelDelta(
                                      wordSpacingPercent));
  const int spaceWidth = std::max(0, userSpaceWidth + blockStyle.wordSpacing);
  auto wordWidths = calculateWordWidths(renderer, fontId);

  std::vector<size_t> lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, spaceWidth, wordWidths, wordContinues);
  const size_t lineCount =
      includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, spaceWidth, wordWidths, wordContinues,
                lineBreakIndices, processLine);
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(),
                        wordContinues.begin() + consumed);
  }
}

std::vector<uint16_t>
ParsedText::calculateWordWidths(const GfxRenderer &renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(
        measureWordWidth(renderer, fontId, words[i], wordStyles[i],
                         blockStyle.letterSpacing));
  }

  return wordWidths;
}

std::vector<size_t>
ParsedText::computeLineBreaks(const GfxRenderer &renderer, const int fontId,
                              const int pageWidth, const int spaceWidth,
                              std::vector<uint16_t> &wordWidths,
                              std::vector<bool> &continuesVec) {
  (void)renderer;
  (void)fontId;
  if (words.empty()) {
    return {};
  }

  // Calculate first line indent for left/justified text.
  const int firstLineIndent =
      blockStyle.textIndent > 0 &&
              (blockStyle.alignment == CssTextAlign::Justify ||
               blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line
  // starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth =
        i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Add space before word j, unless it's the first word on the line or a
      // continuation
      const int gap =
          j > static_cast<size_t>(i) && !continuesVec[j] ? spaceWidth : 0;
      currlen += wordWidths[j] + gap;

      if (currlen > effectivePageWidth) {
        break;
      }

      // Cannot break after word j if the next word attaches to it (continuation
      // group)
      if (j + 1 < totalWordCount && continuesVec[j + 1]) {
        continue;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0; // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll =
            static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j; // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word
    // line This prevents cascade failure where one oversized word breaks all
    // preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i; // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid
      // configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index +
  // 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

void ParsedText::applyParagraphIndent(const GfxRenderer &renderer,
                                      const int fontId) {
  if (words.empty()) {
    return;
  }

  if (firstLineIndentMode == 1) {
    blockStyle.textIndent = 0;
    blockStyle.textIndentDefined = true;
    return;
  }

  const float forcedIndentMultiplier = indentMultiplierForMode(firstLineIndentMode);
  if (forcedIndentMultiplier > 0.0f) {
    const int emWidth = renderer.getTextAdvanceX(fontId, "\xe2\x80\x83");
    blockStyle.textIndent = static_cast<int16_t>(
        std::lround(static_cast<float>(emWidth) * forcedIndentMultiplier));
    blockStyle.textIndentDefined = true;
    return;
  }

  if (blockStyle.textIndentDefined && usePublisherStyles) {
    // CSS text-indent is explicitly set (even if 0) - don't use fallback
    // EmSpace The actual indent positioning is handled in extractLine()
  } else if (blockStyle.alignment == CssTextAlign::Justify ||
             blockStyle.alignment == CssTextAlign::Left) {
    // No CSS text-indent defined - use EmSpace fallback for visual indent
    words.front().insert(0, "\xe2\x80\x83");
  }
}

void ParsedText::extractLine(
    const size_t breakIndex, const int pageWidth, const int spaceWidth,
    const std::vector<uint16_t> &wordWidths,
    const std::vector<bool> &continuesVec,
    const std::vector<size_t> &lineBreakIndices,
    const std::function<void(std::shared_ptr<TextBlock>)> &processLine) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt =
      breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate first line indent for left/justified text.
  const bool isFirstLine = breakIndex == 0;
  const int firstLineIndent =
      isFirstLine && blockStyle.textIndent > 0 &&
              (blockStyle.alignment == CssTextAlign::Justify ||
               blockStyle.alignment == CssTextAlign::Left)
          ? blockStyle.textIndent
          : 0;

  // Calculate total word width for this line and count actual word gaps
  // (continuation words attach to previous word with no gap)
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += wordWidths[lastBreakAt + wordIdx];
    // Count gaps: each word after the first creates a gap, unless it's a
    // continuation
    if (wordIdx > 0 && !continuesVec[lastBreakAt + wordIdx]) {
      actualGapCount++;
    }
  }

  // Calculate spacing (account for indent reducing effective page width on
  // first line)
  const int effectivePageWidth = pageWidth - firstLineIndent;
  const int spareSpace = effectivePageWidth - lineWordWidthSum;

  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  // For justified text, calculate spacing based on actual gap count
  if (blockStyle.alignment == CssTextAlign::Justify && !isLastLine &&
      actualGapCount >= 1) {
    spacing = spareSpace / static_cast<int>(actualGapCount);
  }

  // Calculate initial x position (first line starts at indent for
  // left/justified text)
  auto xpos = static_cast<uint16_t>(firstLineIndent);
  if (blockStyle.alignment == CssTextAlign::Right) {
    xpos = spareSpace - static_cast<int>(actualGapCount) * spaceWidth;
  } else if (blockStyle.alignment == CssTextAlign::Center) {
    xpos = (spareSpace - static_cast<int>(actualGapCount) * spaceWidth) / 2;
  }

  // Pre-calculate X positions for words
  // Continuation words attach to the previous word with no space before them
  std::vector<uint16_t> lineXPos;
  lineXPos.reserve(lineWordCount);

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    const uint16_t currentWordWidth = wordWidths[lastBreakAt + wordIdx];

    lineXPos.push_back(xpos);

    // Add spacing after this word, unless the next word is a continuation
    const bool nextIsContinuation =
        wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];

    xpos += currentWordWidth + (nextIsContinuation ? 0 : spacing);
  }

  // Build line data by moving from the original vectors using index range
  std::vector<std::string> lineWords(
      std::make_move_iterator(words.begin() + lastBreakAt),
      std::make_move_iterator(words.begin() + lineBreak));
  std::vector<EpdFontFamily::Style> lineWordStyles(
      wordStyles.begin() + lastBreakAt, wordStyles.begin() + lineBreak);

  for (auto &word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  processLine(
      std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos),
                                  std::move(lineWordStyles), blockStyle));
}
