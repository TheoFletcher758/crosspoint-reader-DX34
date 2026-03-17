#include "LibrarySearchActivity.h"

#include <I18n.h>
#include <HalDisplay.h>

#include <algorithm>

#include "LibrarySearchSupport.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

const char* const LibrarySearchActivity::keyboard[NUM_ROWS] = {
    "`1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./",
    "^  _____<OK"};

const char* const LibrarySearchActivity::keyboardShift[NUM_ROWS] = {
    "~!@#$%^&*()_+", "QWERTYUIOP{}|", "ASDFGHJKL:\"", "ZXCVBNM<>?",
    "SPECIAL ROW"};

LibrarySearchActivity::LibrarySearchActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput,
    std::string folderPath, const std::vector<std::string>& entries,
    std::string initialQuery, OnCompleteCallback onComplete,
    OnCancelCallback onCancel)
    : Activity("LibrarySearch", renderer, mappedInput),
      folderPath(std::move(folderPath)),
      entries(entries),
      query(std::move(initialQuery)),
      onComplete(std::move(onComplete)),
      onCancel(std::move(onCancel)) {}

void LibrarySearchActivity::onEnter() {
  Activity::onEnter();
  rebuildPreviewMatches();
  requestUpdate();
}

void LibrarySearchActivity::onExit() { Activity::onExit(); }

int LibrarySearchActivity::getRowLength(const int row) const {
  switch (row) {
    case 0:
      return 13;
    case 1:
      return 13;
    case 2:
      return 11;
    case 3:
      return 10;
    case 4:
      return 10;
    default:
      return 0;
  }
}

char LibrarySearchActivity::getSelectedChar() const {
  const char* const* layout = shiftState ? keyboardShift : keyboard;
  if (selectedRow < 0 || selectedRow >= NUM_ROWS) {
    return '\0';
  }
  if (selectedCol < 0 || selectedCol >= getRowLength(selectedRow)) {
    return '\0';
  }
  return layout[selectedRow][selectedCol];
}

void LibrarySearchActivity::rebuildPreviewMatches() {
  previewMatches = LibrarySearchSupport::rankMatches(entries, query);
}

void LibrarySearchActivity::handleKeyPress() {
  if (selectedRow == SPECIAL_ROW) {
    if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
      shiftState = (shiftState + 1) % 3;
      requestUpdate();
      return;
    }

    if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
      if (query.size() < MAX_QUERY_LENGTH) {
        query += ' ';
        rebuildPreviewMatches();
      }
      requestUpdate();
      return;
    }

    if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
      if (!query.empty()) {
        query.pop_back();
        rebuildPreviewMatches();
      }
      requestUpdate();
      return;
    }

    if (selectedCol >= DONE_COL) {
      if (onComplete) {
        onComplete(query);
      }
      return;
    }
  }

  const char c = getSelectedChar();
  if (c == '\0' || query.size() >= MAX_QUERY_LENGTH) {
    return;
  }

  query += c;
  rebuildPreviewMatches();
  if (shiftState == 1) {
    shiftState = 0;
  }
  requestUpdate();
}

void LibrarySearchActivity::loop() {
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this] {
    selectedRow = ButtonNavigator::previousIndex(selectedRow, NUM_ROWS);
    const int maxCol = getRowLength(selectedRow) - 1;
    if (selectedCol > maxCol) {
      selectedCol = maxCol;
    }
    requestUpdate();
  });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this] {
                                         selectedRow =
                                             ButtonNavigator::nextIndex(
                                                 selectedRow, NUM_ROWS);
                                         const int maxCol =
                                             getRowLength(selectedRow) - 1;
                                         if (selectedCol > maxCol) {
                                           selectedCol = maxCol;
                                         }
                                         requestUpdate();
                                       });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [this] {
                                         const int maxCol =
                                             getRowLength(selectedRow) - 1;
                                         if (selectedRow == SPECIAL_ROW) {
                                           if (selectedCol >= SHIFT_COL &&
                                               selectedCol < SPACE_COL) {
                                             selectedCol = maxCol;
                                           } else if (selectedCol >= SPACE_COL &&
                                                      selectedCol <
                                                          BACKSPACE_COL) {
                                             selectedCol = SHIFT_COL;
                                           } else if (selectedCol >=
                                                          BACKSPACE_COL &&
                                                      selectedCol < DONE_COL) {
                                             selectedCol = SPACE_COL;
                                           } else if (selectedCol >= DONE_COL) {
                                             selectedCol = BACKSPACE_COL;
                                           }
                                         } else {
                                           selectedCol =
                                               ButtonNavigator::previousIndex(
                                                   selectedCol, maxCol + 1);
                                         }
                                         requestUpdate();
                                       });

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [this] {
                                         const int maxCol =
                                             getRowLength(selectedRow) - 1;
                                         if (selectedRow == SPECIAL_ROW) {
                                           if (selectedCol >= SHIFT_COL &&
                                               selectedCol < SPACE_COL) {
                                             selectedCol = SPACE_COL;
                                           } else if (selectedCol >= SPACE_COL &&
                                                      selectedCol <
                                                          BACKSPACE_COL) {
                                             selectedCol = BACKSPACE_COL;
                                           } else if (selectedCol >=
                                                          BACKSPACE_COL &&
                                                      selectedCol < DONE_COL) {
                                             selectedCol = DONE_COL;
                                           } else if (selectedCol >= DONE_COL) {
                                             selectedCol = SHIFT_COL;
                                           }
                                         } else {
                                           selectedCol =
                                               ButtonNavigator::nextIndex(
                                                   selectedCol, maxCol + 1);
                                         }
                                         requestUpdate();
                                       });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleKeyPress();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) && onComplete) {
    onComplete(query);
  }
}

void LibrarySearchActivity::render(Activity::RenderLock&&) {
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.drawCenteredText(UI_10_FONT_ID, 10, "Search current folder");

  const int pathY = 30;
  const int pathWidth = pageWidth - 24;
  const std::string pathLabel =
      renderer.truncatedText(SMALL_FONT_ID, folderPath.c_str(), pathWidth);
  renderer.drawText(SMALL_FONT_ID, 12, pathY, pathLabel.c_str());

  const int queryBoxY = pathY + renderer.getLineHeight(SMALL_FONT_ID) + 8;
  const int queryBoxH = 24;
  renderer.drawRect(10, queryBoxY, pageWidth - 20, queryBoxH, true);
  std::string displayQuery = query.empty() ? "_" : (query + "_");
  displayQuery =
      renderer.truncatedText(UI_10_FONT_ID, displayQuery.c_str(), pageWidth - 34);
  renderer.drawText(UI_10_FONT_ID, 16, queryBoxY + 6, displayQuery.c_str());

  const int previewStartY = queryBoxY + queryBoxH + 8;
  std::string matchSummary =
      query.empty()
          ? "Type to search"
          : (std::to_string(previewMatches.size()) + " matches");
  renderer.drawText(UI_10_FONT_ID, 12, previewStartY, matchSummary.c_str());

  int previewY = previewStartY + renderer.getLineHeight(UI_10_FONT_ID) + 4;
  for (int i = 0;
       i < PREVIEW_ROWS && i < static_cast<int>(previewMatches.size()); ++i) {
    const std::string entry = entries[previewMatches[i]];
    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, entry.c_str(), pageWidth - 24);
    renderer.drawText(UI_10_FONT_ID, 12, previewY, label.c_str());
    previewY += renderer.getLineHeight(UI_10_FONT_ID) + 2;
  }

  constexpr int keyWidth = 18;
  constexpr int keyHeight = 18;
  constexpr int keySpacing = 3;
  constexpr int maxRowWidth = KEYS_PER_ROW * (keyWidth + keySpacing);
  const int leftMargin = (pageWidth - maxRowWidth) / 2;
  const int keyboardStartY =
      pageHeight - UITheme::getInstance().getMetrics().buttonHintsHeight - 8 -
      NUM_ROWS * (keyHeight + keySpacing);
  const char* const* layout = shiftState ? keyboardShift : keyboard;

  for (int row = 0; row < NUM_ROWS; ++row) {
    const int rowY = keyboardStartY + row * (keyHeight + keySpacing);
    int currentX = leftMargin;

    if (row == SPECIAL_ROW) {
      const bool shiftSelected =
          (selectedRow == SPECIAL_ROW && selectedCol >= SHIFT_COL &&
           selectedCol < SPACE_COL);
      renderItemWithSelector(currentX + 2, rowY, "shift", shiftSelected);
      currentX += 2 * (keyWidth + keySpacing);

      const bool spaceSelected =
          (selectedRow == SPECIAL_ROW && selectedCol >= SPACE_COL &&
           selectedCol < BACKSPACE_COL);
      renderItemWithSelector(currentX + 12, rowY, "_____", spaceSelected);
      currentX += 5 * (keyWidth + keySpacing);

      const bool backspaceSelected =
          (selectedRow == SPECIAL_ROW && selectedCol >= BACKSPACE_COL &&
           selectedCol < DONE_COL);
      renderItemWithSelector(currentX + 2, rowY, "<-", backspaceSelected);
      currentX += 2 * (keyWidth + keySpacing);

      const bool okSelected =
          (selectedRow == SPECIAL_ROW && selectedCol >= DONE_COL);
      renderItemWithSelector(currentX + 2, rowY, tr(STR_OK_BUTTON), okSelected);
      continue;
    }

    for (int col = 0; col < getRowLength(row); ++col) {
      const std::string keyLabel(1, layout[row][col]);
      const int charWidth =
          renderer.getTextWidth(UI_10_FONT_ID, keyLabel.c_str());
      const int keyX =
          currentX + col * (keyWidth + keySpacing) + (keyWidth - charWidth) / 2;
      const bool isSelected = row == selectedRow && col == selectedCol;
      renderItemWithSelector(keyX, rowY, keyLabel.c_str(), isSelected);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_OK_BUTTON), tr(STR_SELECT),
                                            tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3,
                      labels.btn4);
  GUI.drawSideButtonHints(renderer, tr(STR_DIR_UP), tr(STR_DIR_DOWN));

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void LibrarySearchActivity::renderItemWithSelector(const int x, const int y,
                                                   const char* item,
                                                   const bool isSelected) const {
  if (isSelected) {
    const int itemWidth = renderer.getTextWidth(UI_10_FONT_ID, item);
    renderer.drawText(UI_10_FONT_ID, x - 6, y, "[");
    renderer.drawText(UI_10_FONT_ID, x + itemWidth, y, "]");
  }
  renderer.drawText(UI_10_FONT_ID, x, y, item);
}
