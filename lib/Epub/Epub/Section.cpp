#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include "Page.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 15;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(uint8_t) + sizeof(uint8_t) +
                                 sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(bool) + sizeof(uint16_t) + sizeof(uint32_t);
}  // namespace

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    LOG_ERR("SCT", "File not open for writing page %d", pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    LOG_ERR("SCT", "Failed to serialize page %d", pageCount);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(const int fontId, const float lineCompression,
                                     const uint8_t wordSpacingPercent,
                                     const uint8_t extraParagraphSpacingLevel,
                                     const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                     const uint16_t viewportHeight, const bool hyphenationEnabled,
                                     const bool embeddedStyle, const bool readerBoldSwap) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(wordSpacingPercent) + sizeof(extraParagraphSpacingLevel) +
                                   sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                                   sizeof(viewportHeight) + sizeof(pageCount) + sizeof(hyphenationEnabled) +
                                   sizeof(embeddedStyle) + sizeof(readerBoldSwap) + sizeof(uint32_t),
                "Header size mismatch");
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, wordSpacingPercent);
  serialization::writePod(file, extraParagraphSpacingLevel);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, embeddedStyle);
  serialization::writePod(file, readerBoldSwap);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0 when written)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset
}

bool Section::loadSectionFile(const int fontId, const float lineCompression,
                              const uint8_t wordSpacingPercent,
                              const uint8_t extraParagraphSpacingLevel,
                              const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle,
                              const bool readerBoldSwap) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    uint8_t fileWordSpacingPercent;
    uint8_t fileExtraParagraphSpacingLevel;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    bool fileReaderBoldSwap;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileWordSpacingPercent);
    serialization::readPod(file, fileExtraParagraphSpacingLevel);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileEmbeddedStyle);
    serialization::readPod(file, fileReaderBoldSwap);

    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        wordSpacingPercent != fileWordSpacingPercent ||
        extraParagraphSpacingLevel != fileExtraParagraphSpacingLevel || paragraphAlignment != fileParagraphAlignment ||
        viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
        hyphenationEnabled != fileHyphenationEnabled || embeddedStyle != fileEmbeddedStyle ||
        readerBoldSwap != fileReaderBoldSwap) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  uint32_t lutOffset;
  file.seek(HEADER_SIZE - sizeof(uint32_t));
  serialization::readPod(file, lutOffset);
  pageLut.resize(pageCount);
  file.seek(lutOffset);
  for (uint16_t i = 0; i < pageCount; i++) {
    serialization::readPod(file, pageLut[i]);
  }
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages", pageCount);
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const int fontId, const float lineCompression,
                                const uint8_t wordSpacingPercent,
                                const uint8_t extraParagraphSpacingLevel,
                                const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                const uint16_t viewportHeight, const bool hyphenationEnabled, const bool embeddedStyle,
                                const bool readerBoldSwap, const std::function<void()>& popupFn) {
  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Retry logic for SD card timing issues
  bool success = false;
  uint32_t fileSize = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
      delay(50);  // Brief delay before retry
    }

    // Remove any incomplete file from previous attempt before retrying
    if (Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
    }

    FsFile tmpHtml;
    if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      continue;
    }
    success = epub->readItemContentsToStream(localPath, tmpHtml, 1024);
    fileSize = tmpHtml.size();
    tmpHtml.close();

    // If streaming failed, remove the incomplete file immediately
    if (!success && Storage.exists(tmpHtmlPath.c_str())) {
      Storage.remove(tmpHtmlPath.c_str());
      LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
    }
  }

  if (!success) {
    LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
    return false;
  }

  LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

  if (!Storage.openFileForWrite("SCT", filePath, file)) {
    return false;
  }
  writeSectionFileHeader(fontId, lineCompression, wordSpacingPercent, extraParagraphSpacingLevel, paragraphAlignment,
                         viewportWidth, viewportHeight, hyphenationEnabled, embeddedStyle, readerBoldSwap);
  std::vector<uint32_t> lut = {};

  // Derive the content base directory and image cache path prefix for the parser
  size_t lastSlash = localPath.find_last_of('/');
  std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  std::string imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  CssParser* cssParser = nullptr;
  if (embeddedStyle) {
    cssParser = epub->getCssParser();
    if (cssParser) {
      if (!cssParser->loadFromCache()) {
        LOG_ERR("SCT", "Failed to load CSS from cache");
      }
    }
  }

  ChapterHtmlSlimParser visitor(
      epub, tmpHtmlPath, renderer, fontId, lineCompression, wordSpacingPercent, extraParagraphSpacingLevel,
      paragraphAlignment, viewportWidth, viewportHeight, hyphenationEnabled,
      [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); },
      embeddedStyle, contentBase, imageBasePath, popupFn, cssParser);
  success = visitor.parseAndBuildPages();

  Storage.remove(tmpHtmlPath.c_str());
  if (!success) {
    LOG_ERR("SCT", "Failed to parse XML and build pages");
    file.close();
    Storage.remove(filePath.c_str());
    if (cssParser) {
      cssParser->clear();
    }
    return false;
  }

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;
  // Write LUT
  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
    file.close();
    Storage.remove(filePath.c_str());
    return false;
  }

  // Go back and write LUT offset
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
  if (cssParser) {
    cssParser->clear();
  }
  return true;
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (currentPage < 0 || static_cast<size_t>(currentPage) >= pageLut.size()) return nullptr;
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return nullptr;
  }

  file.seek(pageLut[currentPage]);
  auto page = Page::deserialize(file);
  file.close();
  return page;
}
