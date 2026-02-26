# Differences from Upstream CrossPoint

This repository is **CrossPoint-Mod-DX34**, a DX34-focused fork. It is intentionally divergent from upstream CrossPoint.

## Positioning

- DX34 behavior and stability are prioritized over upstream parity.
- Release decisions are made for this fork first.
- Upstream references are kept for attribution/history, not as the primary support path.

## Current DX34 Differences (Phase A)

- Product identity and release channel are DX34-specific.
- Reader font family options are:
  - `Bookerly`
  - `ChareInk`
  - `Chare`
- Extra paragraph spacing is level-based:
  - `Off`, `S`, `M`, `L`
- Current releases are English-first in shipped UI behavior.
- Hyphenation is **not treated as a supported user-facing DX34 feature**.

## Planned Follow-Up (Deferred Phase B)

Locked scope for later release:
- Full removal of hyphenation feature surface.
- Full removal of non-DX34 language assets.

This is deferred to keep the current release low-risk and production-focused.
