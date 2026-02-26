# Release Notes Draft: 4.0.3

## Summary

Production-readiness release for `CrossPoint-Mod-DX34` with DX34-first branding, CI fixes, docs alignment, and stability hygiene.

## Changes

- CI push trigger corrected to `main`.
- Added explicit firmware artifact presence check in CI.
- Rebranded docs/metadata to `CrossPoint-Mod-DX34` with fork positioning.
- Added `Differences from Upstream` documentation.
- Added release checklist documentation.
- Updated docs to match current reader behavior:
  - Fonts: `Bookerly`, `ChareInk`, `Chare`
  - Extra paragraph spacing: `Off/S/M/L`
  - JSON-first settings/state migration wording
  - File format docs for `extraParagraphSpacingLevel` (`uint8`)
- Removed unreachable duplicate lines in `CrossPointSettings` migration path.
- Bumped version to `4.0.3`.

## Deferred (Next Phase)

- Full removal of hyphenation surface.
- Full removal of non-DX34 language assets.
