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
- The mod keeps no centre of its own. Every tracker centres itself, so a mod
  centre was a second one in series with the tracker's: pressing Center in
  opentrack left the view parked at the negated drift. The `Home` hotkey, the
  `Ctrl+Shift+T` chord and the `[Hotkeys] Recenter` INI key are gone, and the
  tracker pose is applied as absolute. Centre your view in your tracker app.
- Smoothing is now two INI keys under `[Rotation]`: `LocalSmoothing` (default
  `0.0`) for a tracker running on this machine, and `RemoteSmoothing` (default
  `0.15`) for a tracker on a remote network device. The value is chosen per
  connection from the packet source address and re-evaluated every tick, so
  swapping a local OpenTrack instance for a phone on WiFi needs no restart.
- Removed the `[Rotation] Smoothing` and `[Position] PositionSmoothing` keys;
  both new parameters cover rotation and position.
- The `[udp] pose` heartbeat now writes every 30s instead of every 2s. At the
  old interval a few hours of play added most of a megabyte of identical lines
  to `MirrorsEdgeHeadTracking.log` and buried the startup chain a bug report is
  read for. The interval is wall-clock: counting the loop's ticks assumed an 8ms
  period that the default Windows scheduler never delivers (the real tick is
  15-16ms), and a tick that landed on the boundary during a menu skipped a whole
  period.
- `MirrorsEdgeHeadTracking.log` now keeps one previous generation as
  `MirrorsEdgeHeadTracking.prev.log`. The crash handler writes its report into
  this log and the next launch truncated it, so relaunching before sending the
  file destroyed the session that crashed.
- A retired smoothing key no longer silences the warning for the other one. A
  user upgrading can have both, and a single process-wide latch reported the
  first and dropped the second.
- `uninstall.cmd` removes `MirrorsEdgeHeadTracking.log` and
  `MirrorsEdgeHeadTracking.prev.log`.
- Removed the hidden 0.15 baseline smoothing floor, so a tracker on this
  machine gets zero-latency tracking by default.
- Adopted the shared `HeadTrackingSession` pipeline (interpolation, smoothing,
  per-axis sensitivity/inversion) for both rotation and position, replacing the
  raw receiver pose path.
- `Page Up` / `Ctrl+Shift+G` now **cycles** the tracking mode (rotation + position
  -> rotation only -> position only) instead of toggling position on/off. INI key
  `TogglePosition` is renamed `CycleMode`.
