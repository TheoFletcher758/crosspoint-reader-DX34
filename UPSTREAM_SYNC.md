# Upstream Sync — crosspoint-reader-DX34

This document tracks which upstream [`crosspoint-reader/crosspoint-reader`](https://github.com/crosspoint-reader/crosspoint-reader) commits have been ported to the DX34 mod, and which are still pending.

**Base diverge point:** `e32d41a` (our fork start)  
**Upstream target:** `upstream/master` (as of 2026-02-23)

---

## ✅ Already Applied

| Commit | PR | Description | Notes |
|--------|----|-------------|-------|
| `c4e3c24` | #944 | **4-bit BMP sleep screen support** | Native palette auto-detection, dither bypass. Fully cherry-picked. |
| `2cc497c` | #957 | **Double FAST_REFRESH for grey image washout** | Manually merged (conflict in `EpubReaderActivity.cpp`). Retains our vertical offset fix. |

---

## 🔄 In Progress

| Commit | PR | Description | Status |
|--------|----|-------------|--------|
| `87d9d1d` | #978 | **Font drawing perf (renderCharImpl refactor)** | Conflict resolution in progress — `GfxRenderer.cpp` and `GfxRenderer.h` |
| `a610568` | #1047 | **WebDAV server** | Staged (new files `WebDAVHandler.cpp/h`). Needs compat patch for Arduino Core 2.0.17 (`WiFiClient` vs `NetworkClient`, `WiFiUDP` vs `NetworkUDP`). |

---

## ⏳ Pending (Planned)

| Commit | PR | Description | Priority | Notes |
|--------|----|-------------|----------|-------|
| `3696794` | #1038 | **Replace std::list with std::vector in text layout** | 🔴 HIGH | Pure perf, no visible risk. Touches `TextBlock.h/cpp`, `ParsedText.h/cpp`. |
| `786b438` | #682 | **Multi-select file deletion** | 🟠 MED | New UI feature — select & bulk-delete files in file browser. |
| `c1fad16` | #759 | **Screenshots** | 🟠 MED | Save e-ink screen to SD card. I18n + EpubReader conflicts expected. |
| `b8e743e` | #995 | **Increase PNGdec buffer for wide images** | 🟡 LOW | Single-line fix, safe. |
| `3e2c518` | #997 | **Crash fix: bold/italic font indexing** | 🔴 HIGH | Load access fault crash fix. Safe to apply. |
| `588984e` | #1010 | **Fix dangling pointer** | 🔴 HIGH | Safety fix. |
| `6e4d0e5` | #920 | **Migrate settings to JSON** | 🔴 HIGH | Replaces binary `.bin` settings with `.json`. Conflicts with our atomic write patches for `CrossPointSettings.cpp` and `RecentBooksStore.cpp`. Requires careful migration. |
| `3cb60aa` | #869 | **Close leaked file descriptors** | 🔴 HIGH | Fixes FD leak in `SleepActivity` and web server. |
| `9e4ef00` | #1039 | **Download links in web UI** | 🟡 LOW | Web server UI enhancement for direct download links. |

---

## ❌ Skipped (by design)

| Commit | PR | Reason |
|--------|----|--------|
| `8853776` | #1006 | Themes Phase 1 — we don't want theming changes |
| `8db3542` | #1017 | Lyra theme cover outlines — theme only |
| `4ccafe5` | #1065 | Ukrainian translation — English only |
| `e44c004` | #1054 | Spanish translation improvements — English only |
| `63002d4` | — | Merge release commit — skip |
| `402e887` | — | Version bump — we manage our own versions |

---

## ⚠️ Conflict Map

Files that commonly conflict when cherry-picking:

- `src/activities/reader/EpubReaderActivity.cpp` — our atomic write `saveProgress` vs upstream changes
- `lib/GfxRenderer/GfxRenderer.cpp` / `.h` — `renderChar` signature changes
- `lib/I18n/translations/*.yaml` — I18n keys added upstream that don't exist in our tree
- `src/CrossPointSettings.cpp` — our atomic + binary-persistence vs JSON migration
- `src/RecentBooksStore.cpp` — our atomic write vs JSON migration

---

## Book Progress Safety Guarantee

> ⚡ **Book progress (`progress.bin`) is safe.** Atomic writes via temp file + rename are implemented in `EpubReaderActivity.cpp`, `XtcReaderActivity.cpp`, and `TxtReaderActivity.cpp`. These survive all upstream merges as they are DX34-specific additions.

---

*Last updated: 2026-02-23 by Antigravity*
