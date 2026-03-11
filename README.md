# CrossPoint-Mod-DX34

Firmware for the **Xteink X4 / DX34-class ESP32-C3 e-paper reader**, maintained as a **DX34-focused fork** of CrossPoint.

This repository is not a mirror of upstream CrossPoint. It is a productized fork with its own UX choices, release cadence, defaults, and device priorities.

![](./docs/images/cover.jpg)

## What This Firmware Is

`CrossPoint-Mod-DX34` is a custom firmware build for people who want the X4/DX34 hardware to behave like a practical reading device first:

- faster access to books
- stronger EPUB reading controls
- cleaner DX34-specific UI behavior
- configurable sleep wallpapers
- Wi-Fi transfer and OTA updates
- reader customization without leaving the book

This fork keeps upstream credit and history, but it does **not** aim for strict parity with upstream behavior.

## What Is Different From Mainline CrossPoint

The short version:

- DX34 behavior is prioritized over upstream parity.
- The shipped UX is **English-first**.
- Reader configuration is tuned around DX34 reading use, not generic feature exposure.
- EPUB has an expanded in-book workflow, including:
  - live reader setting changes while inside a book
  - named **Reading Themes**
  - one-off reader changes without saving a preset
- Sleep wallpaper behavior is more opinionated and more managed than upstream.
- This repo is the primary support path for these DX34 changes.

More fork positioning notes live in [docs/differences-from-upstream.md](./docs/differences-from-upstream.md).

## Current Feature Set

### Library and file handling

- Folder-based file browser
- Recent books list on the home screen
- Resume last opened book on boot
- Wireless file upload over Wi-Fi
- File management from browser:
  - upload
  - create folders
  - move
  - rename
  - delete
- OPDS browser support
- Calibre wireless transfer support

### Supported book formats

- **EPUB**: main first-class reading format
- **TXT / MD**: plain-text reader
- **XTC / XTCH**: pre-rendered page format

EPUB is where the advanced DX34 reading work lives. TXT and XTC are supported, but the new reading-theme workflow is intentionally **EPUB-only**.

### EPUB reading

- persistent reading position
- chapter selection
- go to percentage
- configurable orientation
- configurable margins
- configurable line spacing
- configurable paragraph alignment
- configurable extra paragraph spacing
- embedded CSS on/off
- status bar customization
- bold-swap shortcut
- in-book reader settings
- named **Reading Themes**
- remove from recents / delete book / clean cache from inside the book
- KOReader progress sync entry from the in-book menu

### Device and system

- OTA update support
- configurable sleep timeout
- configurable refresh frequency
- short power button action selection
- side button direction swap
- front button remapping
- sunlight fading compensation toggle
- custom boot image (`/boot.bmp`)

## EPUB In-Book Workflow

Pressing `OK` while reading an EPUB opens the **in-book menu**. The current menu includes:

- `Select Chapter`
- `Reading Orientation`
- `Go to %`
- `Reading Themes`
- `Go Home`
- `Sync Progress`
- `Clean Cache + Progress`
- `Delete Book`
- `Remove from Recents`

### Reading Themes

The new EPUB-only **Reading Themes** flow gives you two modes of use:

1. **Adjust Current Settings**
   Change the reader live and keep reading, without saving a preset.

2. **Reading Themes**
   Save the current display setup under a name, then later:
   - apply it
   - rename it
   - overwrite it with the current settings
   - delete it

Applying a theme reflows the current EPUB chapter and restores your position approximately relative to the old pagination, so it should keep you near the same reading spot after font or margin changes.

### Reader shortcuts

While reading an EPUB:

- **Single tap `OK`**: open the in-book menu
- **Double tap `OK`**: toggle reader bold swap
- **Long press `OK`**: toggle between portrait and landscape CCW
- **Back**: return home
- **Side / front page buttons**: previous/next page

## Fonts and Reader Sizes

### User-facing reading fonts

This fork currently exposes **two built-in reader font families**:

- `ChareInk`
- `Bookerly`

### ChareInk sizes

ChareInk supports **7 reader sizes**:

- 13
- 14
- 15
- 16
- 17
- 18
- 19

### Bookerly sizes

Bookerly supports **4 reader sizes**:

- 16
- 17
- 18
- 19

### Notes about size behavior

- The reader stores normalized family-specific sizes internally.
- When you switch font family, the firmware remaps the current size to the nearest valid size for that family.
- Default line spacing is also family-aware:
  - `ChareInk`: 90%
  - `Bookerly`: 85%

### Other font details

- UI text uses built-in bitmap/UI fonts rather than the reader font selection.
- The shipped DX34 experience is English-first.
- Reader rendering still includes broader glyph support through bundled fonts/fallbacks, but the supported DX34 UI behavior is English-first.

## Reader Layout and Appearance Controls

The firmware currently exposes these reader-related controls:

- font family
- font size
- line spacing percentage
- horizontal margin
- top margin
- bottom margin
- paragraph alignment
- extra paragraph spacing level
- embedded style on/off
- hyphenation toggle
- orientation
- status bar toggles and style

Status bar options include:

- show/hide status bar
- battery
- page counter
- book percentage
- chapter percentage
- book progress bar
- chapter progress bar
- chapter title
- no-title-truncation
- text alignment
- progress bar style

## Sleep Screen and Wallpaper System

This firmware has a more developed sleep wallpaper system than the basic upstream behavior.

### Available sleep modes

- `Dark`
- `Light`
- `Custom`
- `Cover`
- `None`
- `Cover + Custom`

### Custom sleep wallpaper locations

The firmware looks for custom BMP sleep wallpapers in this order:

1. `/sleep/` folder images
2. `/sleep_F.bmp`
3. `/sleep.bmp`

If no valid custom bitmap is found, it falls back to the default DX34 sleep screen.

### How random wallpaper selection works

The logic depends on how many BMPs are in `/sleep`:

- **Up to 200 images**
  - the firmware keeps a persisted playlist
  - by default, it follows a stable file-based order
  - using **Randomize Sleep Images** reshuffles the playlist
  - new images inserted into `/sleep` are pushed near the front so they appear quickly

- **More than 200 images**
  - the firmware avoids storing the full playlist in memory
  - **Randomize Sleep Images** chooses a random starting wallpaper
  - after that, the device advances sequentially through the sorted filenames

### Wallpaper protection and trimming

The sleep folder has a practical cap of **200 persisted entries**.

If `/sleep` grows beyond that:

- favorite wallpapers in `/sleep` are protected first
- non-favorites beyond the limit are automatically moved to:
  - `/sleep pause`

This prevents the wallpaper system from growing until it becomes unstable or too memory-heavy.

### Favorites

Sleep wallpapers can be marked as favorites.

- favorite files are stored with an `_F` suffix
- favorites display with a `[F]` prefix in UI labels
- favorites inside `/sleep` are protected from automatic trimming
- the protected favorites count is limited to **200**

### Other wallpaper behavior

- The firmware remembers the last rendered sleep wallpaper path.
- You can show the current sleep wallpaper filename on-screen.
- The Settings UI includes tools for:
  - randomizing sleep images
  - inspecting the last sleep wallpaper
  - moving the last wallpaper to `/sleep pause`
  - favoriting / unfavoriting it
  - deleting it

## Wireless Features

### Built-in web server

The Wi-Fi file-transfer mode includes:

- upload from phone or desktop browser
- browse SD card contents
- create folders
- move / rename / delete files
- basic status page with firmware version and IP address

See [docs/webserver.md](./docs/webserver.md) and [docs/webserver-endpoints.md](./docs/webserver-endpoints.md).

### Calibre wireless transfer

Supported through the CrossPoint Reader Calibre plugin workflow.

### OPDS browser

You can configure an OPDS server URL, username, and password in settings and browse/download directly from the device.

### KOReader sync

The firmware includes KOReader progress sync support, including:

- stored credentials
- server URL
- document matching method
- in-book sync action for EPUB

### OTA updates

The firmware can check for and install updates over Wi-Fi.

## Home Screen and General UX

The home screen is DX34-specific rather than upstream-generic.

It includes:

- a DX34-style header/version label
- recent books with progress percentage
- optional OPDS entry when configured
- file browser
- file transfer
- settings
- sleep favorites capacity warning when the protected wallpaper list is full

## Installation

### Web flasher

1. Connect the device by USB-C.
2. Wake or reboot it if needed.
3. Open [xteink.dve.al](https://xteink.dve.al/).
4. Flash either the latest firmware or a downloaded `.bin`.

### Releases

Release binaries are published here:

- [DX34 releases](https://github.com/diogo7dias/crosspoint-reader-DX34/releases)

### Manual flashing with PlatformIO

```sh
pio run -t upload
```

## Development

### Prerequisites

- PlatformIO Core (`pio`) or VS Code + PlatformIO
- Python 3.8+
- USB-C cable
- Xteink X4 / DX34-compatible device

### Checkout

```sh
git clone --recursive https://github.com/diogo7dias/crosspoint-reader-DX34
cd crosspoint-reader-DX34
git submodule update --init --recursive
```

### Build

```sh
pio run
```

### Flash

```sh
pio run -t upload
```

### Serial monitor / debugging

```sh
pio device monitor
```

Or use the helper:

```sh
python3 scripts/debugging_monitor.py
```

macOS example:

```sh
python3 scripts/debugging_monitor.py /dev/cu.usbmodem101
```

## Storage, Cache, and On-SD State

The firmware stores runtime state and caches under:

- `/.crosspoint/`

This includes settings, state, recent books, reading themes, and book caches.

The device aggressively caches parsed content to reduce RAM pressure and make repeated opens faster.

Useful internals docs:

- [docs/file-formats.md](./docs/file-formats.md)
- [docs/webserver-endpoints.md](./docs/webserver-endpoints.md)
- [docs/release-checklist.md](./docs/release-checklist.md)

## Limitations and Scope Notes

- EPUB is the primary reading target for advanced DX34 features.
- TXT and XTC remain supported, but they do not share the full EPUB in-book settings/theme workflow.
- Current releases are intentionally English-first.
- Hyphenation exists in the codebase, but it is not positioned as a flagship DX34 feature.

## Support and Project Links

- Ideas: [GitHub Discussions](https://github.com/diogo7dias/crosspoint-reader-DX34/discussions/categories/ideas)
- Bugs: [GitHub Issues](https://github.com/diogo7dias/crosspoint-reader-DX34/issues)
- Governance: [GOVERNANCE.md](./GOVERNANCE.md)

## Attribution

This project is independent and unaffiliated with Xteink.

Inspired in part by:

- [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)
