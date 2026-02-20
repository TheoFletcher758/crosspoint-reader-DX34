# Font Migration TODO

## 1) UI font spec (to be provided)
- [ ] Select UI font family/families.
- [ ] Define required styles per family (regular, bold, italic, bold-italic).
- [ ] Define exact UI sizes to support (pixel sizes).
- [ ] Map each UI size to use cases:
  - [ ] Headings
  - [ ] Body labels
  - [ ] Small/status text
  - [ ] Buttons/tabs
- [ ] Confirm anti-aliasing preference per UI size.

## 2) Reader font spec (to be provided)
- [ ] Select reader font family/families.
- [ ] Define required styles per reader family.
- [ ] Define reader size scale (S/M/L/XL/...) with exact pixel sizes.
- [ ] Map reader settings options to actual font IDs.
- [ ] Decide if size mapping differs by family.

## 3) Character coverage and language requirements
- [ ] List required languages/scripts.
- [ ] Confirm required punctuation/symbol coverage.
- [ ] Define fallback strategy for missing glyphs.

## 4) Source assets and licensing
- [ ] Provide source font files (TTF/OTF/TTC) for each family/style.
- [ ] Confirm license compatibility for firmware distribution.
- [ ] Record source and version for each font file.

## 5) Technical constraints
- [ ] Set max flash budget for font assets.
- [ ] Set acceptable RAM/runtime performance limits.
- [ ] Decide if all styles are required for all sizes.
- [ ] Decide whether to keep multiple UI families or only one.

## 6) Build integration tasks
- [ ] Recreate generated font headers under `lib/EpdFont/builtinFonts/`.
- [ ] Regenerate `lib/EpdFont/builtinFonts/all.h`.
- [ ] Regenerate `src/fontIds.h`.
- [ ] Rewire font registrations in `src/main.cpp`.
- [ ] Re-enable reader font settings in `src/SettingsList.h`.
- [ ] Verify `CrossPointSettings::getReaderFontId()` mapping in `src/CrossPointSettings.cpp`.

## 7) Validation checklist
- [ ] Build passes (`pio run`).
- [ ] UI screens render correctly with selected UI sizes.
- [ ] Reader renders regular/bold/italic/bold-italic correctly.
- [ ] Line height and pagination are acceptable across sizes.
- [ ] No missing glyph regressions on sample multilingual books.
- [ ] Boot, sleep, home, settings, and reader flows verified.

## 8) Optional cleanup decisions
- [ ] Remove unused legacy font enums/settings fields if no longer needed.
- [ ] Keep or remove compatibility migration for old font settings values.
- [ ] Document chosen fonts and mappings in README/USER_GUIDE.
