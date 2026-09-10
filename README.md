# Mirror's Edge Head Tracking

![Mirror's Edge running with this mod](https://raw.githubusercontent.com/itsloopyo/mirrors-edge-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Mirror's Edge that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

> Status: early development. The ASI loader, networking, hotkeys and config are working; the camera injection is in progress.

## Features

- **Decoupled look and aim** - head tracking moves the camera while the mouse keeps controlling aim.
- **6DOF positional tracking** - lean and peek by moving your head in space.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

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

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets. Use whichever your keyboard has.

| Action               | Nav-cluster | Chord          |
|----------------------|-------------|----------------|
| Toggle tracking      | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode  | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode      | `Page Down` | `Ctrl+Shift+H` |

Cycle tracking mode rotates through **rotation + position (6DOF)** -> **rotation only** -> **position only**, so you can drop the positional lean without losing head-look (or vice versa).

Yaw mode switches head yaw between local (relative to where the camera points) and world (rotates about world up) space.

## Configuration

The mod reads `MirrorsEdgeHeadTracking.ini` from the game's `Binaries` folder (next to `MirrorsEdge.exe`). Any missing key uses the default shown. All sensitivities default to 1.0. Smoothing is two keys picked per connection from the packet source address, covering rotation and position alike: `LocalSmoothing` (default `0.0`) for a tracker running on this PC, and `RemoteSmoothing` (default `0.15`) for a tracker on a remote network device.

```ini
[Network]
; OpenTrack UDP port (OpenTrack "UDP over network" output).
Port=4242

[General]
EnableOnStartup=true
ShowReticle=true
DataFreshnessMs=500

[Performance]
; Disables the game's frame-rate smoothing before camera and graphics setup.
; Set false to leave TdEngine.ini unchanged.
UnlockFrameRate=true

[Rotation]
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=false
InvertPitch=false
InvertRoll=false
; Smoothing is picked per connection from the packet source address, and covers
; rotation and position alike. 0.0 = no smoothing, 1.0 = heavy.
; LocalSmoothing:  tracker runs on this machine (loopback).
; RemoteSmoothing: tracker is a remote device on the network.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
; PositionEnabled sets the startup mode: true = rotation + position, false =
; rotation only. The Cycle Mode hotkey rotates through 6DOF -> rotation only ->
; position only.
PositionEnabled=true
PositionSensitivityX=1.0
PositionSensitivityY=1.0
PositionSensitivityZ=1.0
InvertPositionX=true
InvertPositionY=false
InvertPositionZ=false
; Unreal units per meter of head movement. Larger = bigger lean/peek for the same
; physical motion. Mirror's Edge world scale is ~2 cm per unit.
PositionScaleUU=50.0

[Hotkeys]
; Windows virtual-key codes (hex). Defaults are the nav cluster.
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
- Raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (tracker on the network) in the INI toward 1.0.
- On a wireless or WiFi tracker, expect some latency; a small amount of smoothing settles it.

**View sits off centre**
- Centre it in your tracker app. OpenTrack has a `Center` bind, SteamVR has its own recentre, and Headcam has a CENTER button. The mod applies what the tracker sends and keeps no centre of its own.

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
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Third-party components bundled
with or compiled into the mod are listed with their full licence texts in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Legal

This is an unofficial, fan-made modification. It is not affiliated with,
endorsed by, or sponsored by DICE, Electronic Arts, Epic Games, or any other
rights holder. Mirror's Edge and all related names, logos and marks belong to
their respective owners and are used here only to identify the game this mod
applies to.

The mod ships no game code, no game assets and no proprietary DLLs. It requires
a legitimately purchased copy of the game, it bypasses no DRM or licence check,
and it is single-player only. The engine structure offsets and function
addresses in the source were measured by the authors from a legitimately owned
copy; they are recorded as plain numbers, and no decompiled or disassembled game
code is kept in this repository.

## Credits

- DICE and EA for Mirror's Edge.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, including the
  Hacker Disassembler Engine by Vyacheslav Patkov.
- [OpenTrack](https://github.com/opentrack/opentrack) for the UDP pose protocol.
