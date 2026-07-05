# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader for Mirror's Edge, the install-time source of truth.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.2`
- Commit: `ab722befd52581a34449b603926cfab476e66b05`
- Asset: `Ultimate-ASI-Loader.zip`
- dinput8.dll SHA-256: `c7277e832f6f07af64903a99ecebab2936260cbf55eda70787c5d7b2d5b9fe60`
- Fetched at: 2026-07-05T13:42:33.0256294+01:00

`dinput8.dll` is extracted from the upstream x86 zip untouched. install.cmd copies it
into the Mirror's Edge exe dir as the ASI proxy slot the game loads.
