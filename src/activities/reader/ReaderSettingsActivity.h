#pragma once

#include <functional>
#include <vector>

#include "activities/Activity.h"
#include "activities/settings/SettingsActivity.h"
#include "util/ButtonNavigator.h"

class ReaderSettingsActivity final : public Activity {
 public:
  explicit ReaderSettingsActivity(GfxRenderer& renderer,
                                  MappedInputManager& mappedInput,
                                  const std::function<void(bool)>& onClose)
      : Activity("ReaderSettings", renderer, mappedInput), onClose(onClose) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  struct FlatSettingRow {
    bool isHeader = false;
    int categoryIndex = 0;
    int settingIndex = -1;
  };

  ButtonNavigator buttonNavigator;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> statusBarSettings;
  std::vector<FlatSettingRow> flatRows;
  int selectedRowIndex = 0;
  bool dirty = false;
  bool fontSizeEditMode = false;
  uint8_t fontSizeEditDraftIndex = 0;
  bool valueEditMode = false;
  int valueEditCategoryIndex = -1;
  int valueEditSettingIndex = -1;
  uint8_t valueEditDraft = 0;
  uint8_t valueEditMin = 0;
  uint8_t valueEditMax = 0;

  const std::function<void(bool)> onClose;

  void buildSettingsList();
  const std::vector<SettingInfo>* settingsForCategory(int categoryIndex) const;
  int findNextEditableRow(int startIndex, int direction) const;
  bool isPopupValueSetting(const SettingInfo& setting) const;
  void startFontSizeEdit();
  void adjustFontSizeEdit(int delta);
  void applyFontSizeEdit();
  void startValueEdit(const SettingInfo& setting, int categoryIndex,
                      int settingIndex);
  void adjustValueEdit(int delta);
  void applyValueEdit();
  void toggleCurrentSetting();
  std::string currentValueEditText() const;
  void persistSettings(const char* context);
};
