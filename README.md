# CrossPoint-Mod-DX34

Firmware for the **Xteink X4** e-paper reader (unaffiliated with Xteink), built with **PlatformIO** for **ESP32-C3**.

`CrossPoint-Mod-DX34` is a divergent fork of CrossPoint focused on DX34 priorities and behavior. It is not a drop-in mirror of upstream and will continue to differ over time.

![](./docs/images/cover.jpg)

## Differences From Upstream

See [Differences from Upstream](./docs/differences-from-upstream.md) for the exact DX34 fork position, including changed/removed UX behavior.

Highlights:
- Reader font family options are DX34-specific: `Bookerly`, `ChareInk`, `Chare`
- Extra paragraph spacing is level-based: `Off`, `S`, `M`, `L`
- Current releases are English-first in shipped UI behavior
- Hyphenation is not treated as a supported user-facing DX34 feature

## Features

- EPUB parsing and rendering (EPUB 2/3)
- Saved reading position
- File browser with nested folder support
- Wi-Fi transfer and OTA update support
- Configurable reader layout/font/margins/spacing
- Screen rotation

## Install

### Web (latest firmware)
1. Connect Xteink X4 via USB-C and wake/unlock device.
2. Open [xteink.dve.al](https://xteink.dve.al/) and flash firmware.

### Web (specific firmware)
1. Download `firmware.bin` from [DX34 releases](https://github.com/diogo7dias/crosspoint-reader-DX34/releases).
2. Flash from [xteink.dve.al](https://xteink.dve.al/) using OTA fast flash controls.

### Manual
```sh
pio run --target upload
```

## Development

### Prerequisites
- PlatformIO Core (`pio`) or VS Code + PlatformIO
- Python 3.8+
- USB-C cable
- Xteink X4

### Checkout
```sh
git clone --recursive https://github.com/diogo7dias/crosspoint-reader-DX34
# If cloned without submodules:
git submodule update --init --recursive
```

### Debugging
```sh
python3 -m pip install pyserial colorama matplotlib
python3 scripts/debugging_monitor.py
# macOS example port:
python3 scripts/debugging_monitor.py /dev/cu.usbmodem2101
```

## Internals

The firmware aggressively caches parsed content under `/.crosspoint/` on SD card to reduce RAM pressure.

For internal structures and cache formats, see:
- [File Formats](./docs/file-formats.md)
- [Webserver Endpoints](./docs/webserver-endpoints.md)

## Release Process

Use [Release Checklist](./docs/release-checklist.md) before tagging/publishing.

## Contributing

- Ideas: [Discussions](https://github.com/diogo7dias/crosspoint-reader-DX34/discussions/categories/ideas)
- Bugs: [Issues](https://github.com/diogo7dias/crosspoint-reader-DX34/issues)
- Governance: [GOVERNANCE.md](./GOVERNANCE.md)

## Attribution

This project remains independent and unaffiliated with Xteink.

Inspired in part by [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader).
