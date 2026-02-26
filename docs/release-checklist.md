# Release Checklist (DX34)

Use this checklist before publishing a tag/release.

## Required Gate

- [ ] Branch is up to date and reviewed.
- [ ] CI passes on `main`.
- [ ] `pio run` succeeds locally.
- [ ] `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` succeeds.
- [ ] Formatting check is clean (`bin/clang-format-fix` + no diffs).
- [ ] `firmware.bin` artifact exists after build.
- [ ] RAM/Flash usage recorded from build output.

## Product/Docs Consistency

- [ ] README reflects current product name and release flow.
- [ ] User guide reflects current settings behavior (fonts, spacing levels).
- [ ] Differences-from-upstream doc is updated.
- [ ] File format docs match current serialized fields.

## Device Smoke (Minimum)

- [ ] Boot device successfully.
- [ ] Open EPUB and turn pages both directions.
- [ ] Verify `Extra Paragraph Spacing` levels (`Off/S/M/L`) are visibly different.
- [ ] Verify first-line indent remains when spacing is enabled.
- [ ] Verify settings persist after reboot.
- [ ] Verify Wi-Fi upload flow.
- [ ] Verify OTA check path opens.

## Release Execution

- [ ] Version bumped in `platformio.ini`.
- [ ] Release notes drafted.
- [ ] Tag created.
- [ ] GitHub release artifacts uploaded and verified.
