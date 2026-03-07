#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Epub.h"

class Page;
class GfxRenderer;

class Section {
  std::shared_ptr<Epub> epub;
  const int spineIndex;
  GfxRenderer& renderer;
  std::string filePath;
  FsFile file;
  std::vector<uint32_t> pageLut;
  std::vector<std::pair<std::string, uint16_t>> anchorLut;

  void writeSectionFileHeader(int fontId, float lineCompression,
                              uint8_t extraParagraphSpacingLevel, uint8_t paragraphAlignment,
                              uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                              bool embeddedStyle, bool readerBoldSwap);
  uint32_t onPageComplete(std::unique_ptr<Page> page);

 public:
  uint16_t pageCount = 0;
  int currentPage = 0;

  explicit Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
      : epub(epub),
        spineIndex(spineIndex),
        renderer(renderer),
        filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}
  ~Section() = default;
  bool loadSectionFile(int fontId, float lineCompression,
                       uint8_t extraParagraphSpacingLevel, uint8_t paragraphAlignment,
                       uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                       bool readerBoldSwap);
  bool clearCache() const;
  bool createSectionFile(int fontId, float lineCompression,
                         uint8_t extraParagraphSpacingLevel, uint8_t paragraphAlignment,
                         uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                         bool readerBoldSwap, const std::function<void()>& popupFn = nullptr);
  std::unique_ptr<Page> loadPageFromSectionFile();
  int getPageForAnchor(const std::string& anchor) const;
  std::string getCurrentAnchorForPage(int page) const;
};
