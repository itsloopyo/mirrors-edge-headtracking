# Third-Party Notices

Mirror's Edge Head Tracking bundles or links the following components.

## Ultimate ASI Loader

- **Version:** v9.7.2
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** dinput8 proxy that loads the `.asi` into the game process.
- **Bundled:** yes. Vendored at `vendor/ultimate-asi-loader/dinput8.dll`, shipped in the release ZIP and used as the install-time source.

---

## MinHook

- **Version:** v1.3.3
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** function hooking for the camera and reticle overlay.
- **Bundled:** yes. Statically linked into `MirrorsEdgeHeadTracking.asi` (source vendored under `third_party/minhook`).

---

## OpenTrack

- **Version:** n/a (wire protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** only the UDP wire format is consumed; no OpenTrack code is bundled.
- **Bundled:** no.

---

## CameraUnlock Core

- **Version:** e6f2023937be5e200b64734d95736d21c4c748d9
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** shared head-tracking runtime, included as a submodule and statically linked.
- **Bundled:** yes. Compiled into `MirrorsEdgeHeadTracking.asi`.

---
