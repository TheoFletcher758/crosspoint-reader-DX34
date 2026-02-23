# Upstream Sync Tracker

Last updated: 2026-02-23
Base: `origin/main` (803bfed — Bump version to 2.0.0)
HEAD: `main` (5f06006 — feat: Migrate binary settings to json #920)

## ✅ Applied Commits (in order)

| Commit | Description | Status | Notes |
|--------|-------------|--------|-------|
| `06de744` | fix: Double FAST_REFRESH for large grey images (#957) | ✅ Done | Conflict in EpubReaderActivity.cpp — preserved DX34 flushProgressIfNeeded |
| `8f6accd` | perf: Improve font drawing performance (#978) | ✅ Done | Conflict in GfxRenderer — accepted upstream renderCharImpl w/ style param |
| `fc3c161` | fix: Increase PNGdec buffer size for wide images (#995) | ✅ Done | Clean |
| `32d57b9` | fix: Crash on chars unsupported by bold/italic font (#997) | ✅ Done | Added style param to renderChar; fixed crash bug |
| `c55b974` | fix: Fix dangling pointer (#1010) | ✅ Done | Clean |
| `adb7ee7` | perf: Replace std::list with std::vector in text layout (#1038) | ✅ Done | Conflict in ParsedText.cpp — kept upstream vector approach |
| `9314f9b` | fix: Close leaked file descriptors in SleepActivity + web server (#869) | ✅ Done | Conflict — kept DX34 filename label feature in renderBitmapSleepScreen |
| `82aa824` | feat: Multi-select file deletion (#682) | ✅ Done | .gitignore conflict only |
| `6e4d0e5` | feat: Migrate binary settings to JSON (#920) | ✅ Done | Multiple conflicts — see details below |

### Post-cherry-pick fixes applied
- `3fdeece` — Add missing `getGlyphBitmap()` impl + `style` params to `getSpaceWidth`/`getTextAdvanceX` (needed after #997 port)
- `4625625` — Restore `SleepActivity::renderCustomSleepScreen` body broken during FD-leak conflict resolution

## 🔧 JSON Settings Migration Details (#920)

### Files changed
| File | Strategy |
|------|----------|
| `lib/hal/HalStorage.h/cpp` | Whitespace conflict only — kept HEAD style |
| `src/CrossPointSettings.h` | Removed `writeSettings()` binary helper declaration |
| `src/CrossPointSettings.cpp` | Replaced binary SettingsWriter with JSON save; kept DX34 binary reader for migration path (including all DX34-custom fields: screenMarginH/T/B, statusBar granular, readerBoldSwap, debugBorders) |
| `src/CrossPointState.cpp` | JSON save; kept DX34 STATE_FILE_VERSION=5 (readerActivityLoadCount, lastSleepFromReader) |
| `src/RecentBooksStore.cpp` | JSON save; kept DX34 path-normalization + deduplication helpers and MAX_RECENT_BOOKS=100 |
| `src/JsonSettingsIO.cpp` | Removed `uiTheme`/`LYRA` (not in DX34) |
| `lib/Serialization/ObfuscationUtils.cpp/h` | New files — added clean |
| `lib/KOReaderSync/KOReaderCredentialStore.cpp/h` | Clean cherry-pick |
| `src/WifiCredentialStore.cpp/h` | Clean cherry-pick |
| `src/RecentBooksStore.h` | Clean cherry-pick |
| `src/CrossPointState.h` | Clean cherry-pick |
| `src/CrossPointSettings.h` | Partially clean — removed `writeSettings` |
| `src/JsonSettingsIO.h/cpp` | New files; removed `uiTheme` field |

### Migration behaviour
- **First boot after update**: Device reads existing `settings.bin` → saves to `settings.json` → renames `.bin` to `.bin.bak`
- **Subsequent boots**: Reads `settings.json` only
- **Same for**: `state.bin`, `recent.bin`, `wifi.bin`, `koreader.bin`
- **DX34-custom fields preserved**: All custom settings (screenMarginH/T/B, status bar granular, readerBoldSwap, debugBorders, showSleepImageFilename, fadingFix, embeddedStyle) are in `JsonSettingsIO.cpp`

## ⏳ Pending / Skipped

| Commit | Description | Status | Notes |
|--------|-------------|--------|-------|
| WebDAV server | feat: WebDAV support | ⏳ Pending | Upstream uses `NetworkClient` — needs WiFiClient compat patching |
| Screenshot | feat: Screenshot capture | ⏳ Pending | Large change, complex conflicts |
| BmpViewer Activity | feat: BMP viewer activity | ⏳ Pending | New activity, relatively self-contained |
| Theme system | feat: UI theming / LYRA theme | 🚫 Skipped | Not targeting DX34 hardware |
| Additional i18n | feat: Language packs | 🚫 Skipped | Device-specific exclusion |

## 📊 Build Status

```
RAM:   31.0% (101692 / 327680 bytes)
Flash: 72.7% (4764340 / 6553600 bytes)
```

Tag: `v2.0.0-dx34-upstream-sync`

## Safety Notes

- **Book progress**: Stored in separate `progress.bin` per book inside `/.crosspoint/` — NOT touched by settings migration
- **Backup retention**: Original binary files renamed to `.bak` after migration, not deleted
- **Rollback**: If `settings.json` is corrupt, `loadFromFile()` returns false → firmware uses built-in defaults
