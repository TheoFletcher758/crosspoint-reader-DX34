#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "util/ButtonNavigator.h"

class LibrarySearchActivity final : public Activity {
 public:
  using OnCompleteCallback = std::function<void(const std::string&)>;
  using OnCancelCallback = std::function<void()>;

  explicit LibrarySearchActivity(GfxRenderer& renderer,
                                 MappedInputManager& mappedInput,
                                 std::string folderPath,
                                 const std::vector<std::string>& entries,
                                 std::string initialQuery,
                                 OnCompleteCallback onComplete,
                                 OnCancelCallback onCancel);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  std::string folderPath;
  const std::vector<std::string>& entries;
  std::string query;
  ButtonNavigator buttonNavigator;
  int selectedRow = 0;
  int selectedCol = 0;
  int shiftState = 0;
  OnCompleteCallback onComplete;
  OnCancelCallback onCancel;
  std::vector<size_t> previewMatches;

  static constexpr int NUM_ROWS = 5;
  static constexpr int KEYS_PER_ROW = 13;
  static constexpr int SPECIAL_ROW = 4;
  static constexpr int SHIFT_COL = 0;
  static constexpr int SPACE_COL = 2;
  static constexpr int BACKSPACE_COL = 7;
  static constexpr int DONE_COL = 9;
  static constexpr size_t MAX_QUERY_LENGTH = 64;
  static constexpr int MAX_PREVIEW_ROWS = 20;
  static const char* const keyboard[NUM_ROWS];
  static const char* const keyboardShift[NUM_ROWS];

  void rebuildPreviewMatches();
  int getRowLength(int row) const;
  char getSelectedChar() const;
  void handleKeyPress();
  void renderItemWithSelector(int x, int y, const char* item,
                              bool isSelected) const;
};
