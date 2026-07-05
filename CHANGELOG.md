# Changelog

All notable changes to this project are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/); versions follow SemVer.

## [Unreleased]

### Added
- ASI-loader scaffold (Ultimate ASI Loader v9.7.2, dinput8 proxy, x86).
- DllMain bootstrap: file logger, crash handler, PE-fingerprint build registry.
- OpenTrack UDP receiver and nav-cluster + Ctrl+Shift chord hotkeys.
- Configuration via `MirrorsEdgeHeadTracking.ini`.
- 6DOF positional tracking: the head offset now translates `POV.Location`
  (camera-local lean/peek, rotated by the clean camera orientation), on top of
  the existing rotation injection.
- Position tuning config: `PositionScaleUU`, `InvertPositionX/Y/Z`.

### Changed
- Adopted the shared `HeadTrackingSession` pipeline (interpolation, smoothing,
  per-axis sensitivity/inversion) for both rotation and position, replacing the
  raw receiver pose path.
- `Page Up` / `Ctrl+Shift+G` now **cycles** the tracking mode (rotation + position
  -> rotation only -> position only) instead of toggling position on/off. INI key
  `TogglePosition` is renamed `CycleMode`.
