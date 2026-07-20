# Mirror's Edge Head Tracking

Move your head to look around Mirror's Edge (2009) while the mouse keeps full control of your aim, driven by an OpenTrack head tracker over UDP with no VR headset required.

<!-- Mod GIF (add assets/readme-clip.gif, then uncomment):
![Mod GIF](https://raw.githubusercontent.com/itsloopyo/mirrors-edge-headtracking/main/assets/readme-clip.gif)
-->

> Status: early development. The ASI loader, networking, hotkeys and config are working; the camera injection is in progress.

## Features

- **Decoupled look and aim** - head tracking moves the camera while the mouse keeps controlling aim.
- **6DOF positional tracking** - lean and peek by moving your head in space.
- **No VR headset needed** - any OpenTrack source (webcam, IR rig, or phone app) works.

## Requirements

- Mirror's Edge on [Steam](https://store.steampowered.com/app/17410/Mirrors_Edge/) (app 17410).
- A head tracking source that outputs the OpenTrack UDP protocol: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or IR rig, a VR headset, or a phone tracker app.
- Windows 10 or 11 (64-bit).

## Installation

1. Download the latest installer ZIP from the [Releases](https://github.com/itsloopyo/mirrors-edge-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`. It auto-detects your Steam install and copies the loader and mod into the game's `Binaries` folder.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder directly. Either set an environment variable before running:

```powershell
$env:MIRRORS_EDGE_PATH = "D:\Games\Mirror's Edge"
```

or pass the path as the first argument:

```powershell
install.cmd "D:\Games\Mirror's Edge"
```

### Manual Installation

To place files by hand, copy `dinput8.dll` (Ultimate ASI Loader), `MirrorsEdgeHeadTracking.asi`, and `MirrorsEdgeHeadTracking.ini` into the game's `Binaries` folder (next to `MirrorsEdge.exe`). The Nexus ZIP contains only these files without the loader if you already run Ultimate ASI Loader.

## Setting Up OpenTrack

Set OpenTrack's output to "UDP over network", host `127.0.0.1`, port `4242` (the mod's default). Map the axes so head yaw turns the view left and right and head pitch looks up and down.

### VR Headset Setup

Connect your headset over Air Link or Virtual Desktop, start SteamVR, then select the SteamVR input in OpenTrack. This feeds your headset's head pose into OpenTrack, which relays it to the mod.

### Webcam Setup

Use OpenTrack's neuralnet tracker as the input. It tracks your face with a plain webcam, no markers or IR hardware required. Set the output to UDP as above.

### Phone App Setup

If your phone tracker app already smooths its output, point it directly at this PC's IP on UDP port `4242`. If you want OpenTrack's curve mapping and filtering, have the app send to OpenTrack instead and let OpenTrack relay to `127.0.0.1:4242`.

## Controls

Two equivalent binding sets. Use whichever your keyboard has.

| Action               | Nav-cluster | Chord          |
|----------------------|-------------|----------------|
| Recenter             | `Home`      | `Ctrl+Shift+T` |
| Toggle tracking      | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode  | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode      | `Page Down` | `Ctrl+Shift+H` |

Cycle tracking mode rotates through **rotation + position (6DOF)** -> **rotation only** -> **position only**, so you can drop the positional lean without losing head-look (or vice versa).

Yaw mode switches head yaw between local (relative to where the camera points) and world (rotates about world up) space.

## Configuration

The mod reads `MirrorsEdgeHeadTracking.ini` from the game's `Binaries` folder (next to `MirrorsEdge.exe`). Any missing key uses the default shown. All sensitivities default to 1.0; smoothing defaults to 0.0 with an internal 0.15 floor that prevents jitter on high-refresh displays.

```ini
[Network]
; OpenTrack UDP port (OpenTrack "UDP over network" output).
Port=4242

[General]
EnableOnStartup=true
; Head moves the view; mouse still aims. Leave on.
AimDecoupling=true
ShowReticle=true
DataFreshnessMs=500

[Performance]
; Removes the game's ~60 fps cap. Mirror's Edge pins itself near 60 via UE3's
; frame-rate smoother, which the in-game V-Sync option does not affect; this
; disables that smoother in the runtime TdEngine.ini so the frame rate is limited
; only by your GPU. Note the game ties some physics/animation timing to frame
; rate, so very high rates can introduce movement jank (that is the game, not the
; mod). Set false to leave the game's cap alone.
UnlockFrameRate=true

[Rotation]
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=false
InvertPitch=false
InvertRoll=false
; 0.0 = responsive (internal 0.15 baseline floor), 1.0 = heavy smoothing.
Smoothing=0.0

[Position]
; PositionEnabled sets the startup mode: true = rotation + position, false =
; rotation only. The Cycle Mode hotkey rotates through 6DOF -> rotation only ->
; position only.
PositionEnabled=true
PositionSensitivityX=1.0
PositionSensitivityY=1.0
PositionSensitivityZ=1.0
PositionSmoothing=0.15
InvertPositionX=true
InvertPositionY=false
InvertPositionZ=false
; Unreal units per meter of head movement. Larger = bigger lean/peek for the same
; physical motion. Mirror's Edge world scale is ~2 cm per unit.
PositionScaleUU=50.0

[Hotkeys]
; Windows virtual-key codes (hex). Defaults are the nav cluster.
Recenter=0x24
ToggleTracking=0x23
; Cycle tracking mode: 6DOF -> rotation only -> position only.
CycleMode=0x21
ToggleYawMode=0x22
```

## Troubleshooting

**Mod not loading**
- Confirm `dinput8.dll` and `MirrorsEdgeHeadTracking.asi` are both in the `Binaries` folder.
- Check `MirrorsEdgeHeadTracking.log` in `Binaries`. It records loader attach, the UDP bind, and tracker connect/disconnect.

**No tracking response**
- Make sure OpenTrack (or your phone app) is running and outputting UDP to `127.0.0.1:4242`.
- If there is no `[udp] tracker CONNECTED` line in the log, verify the tracker's IP and port and that Windows Firewall allows UDP on port 4242.

**Jittery or unstable tracking**
- Raise `Smoothing` in the INI toward 1.0.
- On a wireless or WiFi tracker, expect some latency; a small amount of smoothing settles it.

**Wrong yaw at extreme angles**
- Toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked (default) is horizon-stable; camera-local follows the camera's current up axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio (x86 toolset) and CMake. The build is game-free; it links only the vendored CameraUnlock core, MinHook, and Winsock.

```powershell
git clone --recursive https://github.com/itsloopyo/mirrors-edge-headtracking.git
pixi run build      # configures x86 and builds Release
pixi run package    # builds the installer ZIP
```

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - use your iPhone or Android phone as the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- DICE and EA for Mirror's Edge.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu.
- [OpenTrack](https://github.com/opentrack/opentrack).
