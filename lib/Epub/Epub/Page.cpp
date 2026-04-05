#include "Page.h"

#include <Logging.h>
#include <Serialization.h>

void PageLine::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  block->render(renderer, fontId, xPos + xOffset, yPos + yOffset);
}

bool PageLine::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize TextBlock pointed to by PageLine
  return block->serialize(file);
}

std::unique_ptr<PageLine> PageLine::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto tb = TextBlock::deserialize(file);
  if (!tb) return nullptr;  // TextBlock failed (e.g. corrupt word count), don't create a PageLine with null block
  return std::unique_ptr<PageLine>(new PageLine(std::move(tb), xPos, yPos));
}

void PageImage::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) {
  // Images don't use fontId or text rendering
  imageBlock->render(renderer, xPos + xOffset, yPos + yOffset);
}

bool PageImage::serialize(FsFile& file) {
  serialization::writePod(file, xPos);
  serialization::writePod(file, yPos);

  // serialize ImageBlock
  return imageBlock->serialize(file);
}

std::unique_ptr<PageImage> PageImage::deserialize(FsFile& file) {
  int16_t xPos;
  int16_t yPos;
  serialization::readPod(file, xPos);
  serialization::readPod(file, yPos);

  auto ib = ImageBlock::deserialize(file);
  return std::unique_ptr<PageImage>(new PageImage(std::move(ib), xPos, yPos));
}

void Page::render(GfxRenderer& renderer, const int fontId, const int xOffset, const int yOffset) const {
  for (auto& element : elements) {
    element->render(renderer, fontId, xOffset, yOffset);
  }
}

void Page::renderImages(GfxRenderer& renderer, const int xOffset, const int yOffset) const {
  for (const auto& element : elements) {
    if (element->getTag() == TAG_PageImage) {
      element->render(renderer, 0, xOffset, yOffset);
    }
  }
}

bool Page::isTextOnly() const {
  if (elements.empty()) {
    return false;
  }

  return std::all_of(elements.begin(), elements.end(),
                     [](const std::shared_ptr<PageElement>& element) {
                       return element->getTag() == TAG_PageLine;
                     });
}

int Page::getTextLineCount() const {
  return static_cast<int>(std::count_if(
      elements.begin(), elements.end(),
      [](const std::shared_ptr<PageElement>& element) {
        return element->getTag() == TAG_PageLine;
      }));
}

int Page::getFirstLineY() const {
  int firstY = INT16_MAX;
  for (const auto& element : elements) {
    if (element->getTag() == TAG_PageLine) {
      firstY = std::min(firstY, static_cast<int>(element->yPos));
    }
  }
  return firstY == INT16_MAX ? -1 : firstY;
}

int Page::getUsedHeight(const int lineHeight) const {
  if (lineHeight <= 0) {
    return 0;
  }

  int usedHeight = 0;
  for (const auto& element : elements) {
    if (element->getTag() == TAG_PageLine) {
      usedHeight = std::max(usedHeight,
                            static_cast<int>(element->yPos) + lineHeight);
    }
  }
  return usedHeight;
}

bool Page::applyDensePageVerticalFit(const int lineHeight,
                                     const int viewportHeight,
                                     const int minDenseLines,
                                     const int maxFirstLineY) {
  if (lineHeight <= 0 || viewportHeight <= 0 || !isTextOnly()) {
    return false;
  }

  const int lineCount = getTextLineCount();
  if (lineCount < 2 || lineCount < minDenseLines) {
    return false;
  }

  const int firstLineY = getFirstLineY();
  if (firstLineY < 0 || firstLineY > maxFirstLineY) {
    return false;
  }

  const int usedHeight = getUsedHeight(lineHeight);
  if (usedHeight <= 0 || usedHeight >= viewportHeight) {
    return false;
  }

  const int slack = viewportHeight - usedHeight;
  if (slack <= 0 || slack >= lineHeight) {
    return false;
  }

  const int gapCount = lineCount - 1;
  int lineIndex = 0;
  for (const auto& element : elements) {
    const int extraOffset = (lineIndex * slack) / gapCount;
    element->yPos = static_cast<int16_t>(element->yPos + extraOffset);
    lineIndex++;
  }
  return true;
}

bool Page::serialize(FsFile& file) const {
  const uint16_t count = elements.size();
  serialization::writePod(file, count);

  for (const auto& el : elements) {
    // Use getTag() method to determine type
    serialization::writePod(file, static_cast<uint8_t>(el->getTag()));

    if (!el->serialize(file)) {
      return false;
    }
  }

  // Serialize footnotes (clamp to MAX_FOOTNOTES_PER_PAGE to match addFootnote/deserialize limits)
  const uint16_t fnCount = std::min<uint16_t>(footnotes.size(), MAX_FOOTNOTES_PER_PAGE);
  serialization::writePod(file, fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    const auto& fn = footnotes[i];
    if (file.write(fn.number, sizeof(fn.number)) != sizeof(fn.number) ||
        file.write(fn.href, sizeof(fn.href)) != sizeof(fn.href)) {
      LOG_ERR("PGE", "Failed to write footnote");
      return false;
    }
  }

  return true;
}

std::unique_ptr<Page> Page::deserialize(FsFile& file) {
  auto page = std::unique_ptr<Page>(new Page());

  uint16_t count;
  serialization::readPod(file, count);

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tag;
    serialization::readPod(file, tag);

    if (tag == TAG_PageLine) {
      auto pl = PageLine::deserialize(file);
      if (!pl) {
        LOG_ERR("PGE", "Deserialization failed: PageLine at index %u returned null", i);
        return nullptr;
      }
      page->elements.push_back(std::move(pl));
    } else if (tag == TAG_PageImage) {
      auto pi = PageImage::deserialize(file);
      page->elements.push_back(std::move(pi));
    } else {
      LOG_ERR("PGE", "Deserialization failed: Unknown tag %u", tag);
      return nullptr;
    }
  }

  // Deserialize footnotes
  uint16_t fnCount;
  serialization::readPod(file, fnCount);
  if (fnCount > MAX_FOOTNOTES_PER_PAGE) {
    LOG_ERR("PGE", "Invalid footnote count %u", fnCount);
    return nullptr;
  }
  page->footnotes.resize(fnCount);
  for (uint16_t i = 0; i < fnCount; i++) {
    auto& entry = page->footnotes[i];
    if (file.read(entry.number, sizeof(entry.number)) != sizeof(entry.number) ||
        file.read(entry.href, sizeof(entry.href)) != sizeof(entry.href)) {
      LOG_ERR("PGE", "Failed to read footnote %u", i);
      return nullptr;
    }
    entry.number[sizeof(entry.number) - 1] = '\0';
    entry.href[sizeof(entry.href) - 1] = '\0';
  }

  return page;
}
