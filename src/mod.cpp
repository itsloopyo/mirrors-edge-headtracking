#include "mod.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <string>
#include <thread>

#include "build_profiles.h"
#include "camera_hook.h"
#include "camera_probe.h"
#include "config.h"
#include "framerate.h"
#include "reticle_overlay.h"
#include "version.h"

#include "cameraunlock/diagnostics/crash_handler.h"
#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/memory/pe_fingerprint.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace meht {
namespace {

namespace log = cameraunlock::logging;
namespace input = cameraunlock::input;
namespace mem = cameraunlock::memory;

// Y/G/H from the doctrine chord cluster.
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;

Config g_config;
cameraunlock::UdpReceiver g_receiver;
using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
Session g_session{g_receiver};
// Without this the session would silently report every tracker as local and pin
// smoothing to LocalSmoothing forever, with no compile error.
static_assert(Session::kHasRemoteConnection,
              "receiver must expose IsRemoteConnection() for per-connection smoothing");
input::HotkeyPoller g_hotkeys;

std::atomic<bool> g_trackingEnabled{true};

std::thread g_loopThread;
std::atomic<bool> g_stop{false};

// Directory containing the running EXE, with trailing backslash.
std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash + 1);
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Gameplay vs menu: a mouse-look FPS hides the OS cursor while you're playing and
// shows it in the main menu / pause menu. Cursor hidden => gameplay. Fails open
// (treat as gameplay) so a query failure never silently kills tracking.
bool InGameplay() {
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) return true;
    return (ci.flags & CURSOR_SHOWING) == 0;
}

void OnToggleTracking() {
    bool now = !g_trackingEnabled.load();
    g_trackingEnabled.store(now);
    log::Line("[hotkey] tracking %s", now ? "ENABLED" : "DISABLED");
}

void OnCycleMode() {
    switch (g_session.CycleMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition:
            log::Line("[hotkey] tracking mode = rotation + position (6DOF)");
            break;
        case cameraunlock::TrackingMode::RotationOnly:
            log::Line("[hotkey] tracking mode = rotation only (position disabled)");
            break;
        case cameraunlock::TrackingMode::PositionOnly:
            log::Line("[hotkey] tracking mode = position only (rotation disabled)");
            break;
    }
}

void OnToggleYawMode() {
    bool now = !camera_hook::GetWorldSpaceYaw();
    camera_hook::SetWorldSpaceYaw(now);
    log::Line("[hotkey] yaw mode = %s", now ? "WORLD (horizon-locked)" : "LOCAL (camera-relative)");
}

void RegisterHotkeys() {
    // Nav-cluster keys, guarded so the chord path is the sole trigger for
    // Ctrl+Shift combos.
    g_hotkeys.AddHotkey(g_config.KeyToggleTracking, input::NavGuarded(OnToggleTracking));
    g_hotkeys.AddHotkey(g_config.KeyCycleMode, input::NavGuarded(OnCycleMode));
    g_hotkeys.AddHotkey(g_config.KeyToggleYawMode, input::NavGuarded(OnToggleYawMode));

    // Ctrl+Shift chord alternatives (Y/G/H cluster).
    g_hotkeys.AddHotkey(kVkY, input::ChordGuarded(OnToggleTracking));
    g_hotkeys.AddHotkey(kVkG, input::ChordGuarded(OnCycleMode));
    g_hotkeys.AddHotkey(kVkH, input::ChordGuarded(OnToggleYawMode));

    // Camera-probe dump (development): Insert dumps the live Camera object to the
    // log so the POV.Rotation offset can be identified in-game.
    constexpr int kVkInsert = 0x2D;
    g_hotkeys.AddHotkey(kVkInsert, []() {
        log::Line("[hotkey] camera dump requested");
        camera_probe::RequestDump();
    });

    // Delete arms a one-shot hardware write-watch on CameraCache.Yaw to capture
    // the per-frame POV-committer's instruction address (development RE aid).
    constexpr int kVkDelete = 0x2E;
    g_hotkeys.AddHotkey(kVkDelete, []() {
        log::Line("[hotkey] camera watch requested");
        camera_probe::RequestWatch();
    });
}

void LogFingerprint() {
    HMODULE base = GetModuleHandleW(nullptr);
    mem::PeFingerprint fp{};
    if (!mem::ReadPeFingerprint(base, fp)) {
        log::Line("[profile] failed to read PE fingerprint of the running EXE");
        return;
    }
    log::Line("[profile] running EXE fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
              fp.TimeDateStamp, fp.SizeOfImage, fp.CheckSum);

    const BuildProfile* profile = FindProfile(fp);
    if (profile) {
        log::Line("[profile] matched build profile '%s'", profile->Name);
        auto moduleBase = reinterpret_cast<std::uintptr_t>(base);
        camera_probe::Install(moduleBase, profile->SetViewTargetRVA);
        camera_hook::Install(moduleBase, profile->CopyCompleteValueRVA);
        // Arm the DX9 overlay now (before the game creates its D3D9 device): it
        // hooks IDirect3D9::CreateDevice to capture the real device, so there is
        // no probe device to conflict with the game's fullscreen-exclusive mode.
        reticle_overlay::Install();
    } else {
        log::Line("[profile] no known build profile matches - camera hook stays dormant "
                  "(Phase 1 infra only; this build's fingerprint is logged above for RE)");
    }
}

// Phase 1 stand-in for the per-frame render hook: polls the receiver, services
// deferred actions, and logs connection transitions plus an occasional pose
// sample so loader-presence and UDP both verify from the log.
void Phase1Loop() {
    bool wasConnected = false;
    bool wasInGameplay = false;
    auto lastTick = std::chrono::steady_clock::now();
    auto lastBeat = lastTick;
    while (!g_stop.load()) {
        camera_probe::Poll();

        auto nowTick = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(nowTick - lastTick).count();
        lastTick = nowTick;

        bool connected = g_receiver.IsReceiving();
        if (connected != wasConnected) {
            log::Line("[udp] tracker %s (remote=%s)",
                      connected ? "CONNECTED" : "disconnected",
                      g_receiver.IsRemoteConnection() ? "yes" : "no");
            wasConnected = connected;
        }

        // Drive the camera hook: run the full tracking pipeline and enable
        // injection only while tracking is on, data is live, and we're in gameplay
        bool inGameplay = InGameplay();
        if (inGameplay != wasInGameplay) {
            log::Line("[state] %s", inGameplay ? "gameplay (tracking active)"
                                               : "menu (tracking suppressed)");
            wasInGameplay = inGameplay;
        }

        // Update() re-reads the receiver's connection locality every tick, so
        // swapping a local OpenTrack instance for a phone on WiFi picks up the
        // other smoothing parameter without restarting the game.
        g_session.Update(dt);

        bool track = g_trackingEnabled.load() && connected && inGameplay;
        camera_hook::SetEnabled(track);
        // Draw our reticle at the projected aim point only while tracking is
        // actively rotating the view; disabled, the game's own reticle shows.
        reticle_overlay::SetEnabled(track && g_config.ShowReticle);
        if (track) {
            float y = 0, p = 0, r = 0;
            g_session.GetRotation(y, p, r);  // zeroed in position-only mode
            camera_hook::SetHeadRotationDegrees(y, p, r);

            float ox = 0, oy = 0, oz = 0;
            if (g_session.GetPositionOffset(ox, oy, oz)) {
                // Offset is meters in the camera-local frame (+right/+up/+back);
                // scale to Unreal units for POV.Location.
                const float s = g_config.PositionScaleUU;
                camera_hook::SetHeadPosition(ox * s, oy * s, oz * s);
                camera_hook::SetPositionEnabled(true);
            } else {
                camera_hook::SetPositionEnabled(false);
            }

            // Wall-clock, not a tick count: this loop asks for an 8ms sleep but
            // the default scheduler granularity makes the real tick 15-16ms, so
            // a frame-gated period ran at roughly twice the interval it claimed.
            // Timing it also means a tick that lands in a menu, where this branch
            // is skipped, cannot swallow a whole period.
            if (nowTick - lastBeat >= std::chrono::seconds(30)) {
                lastBeat = nowTick;
                log::Line("[udp] pose yaw=%.2f pitch=%.2f roll=%.2f pos=(%.3f,%.3f,%.3f)m",
                          y, p, r, ox, oy, oz);
            }
        } else {
            camera_hook::SetHeadRotationDegrees(0.0f, 0.0f, 0.0f);
            camera_hook::SetPositionEnabled(false);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}

}  // namespace

void Start() {
    const std::wstring dir = ExeDir();
    // The crash handler below writes into this log and log::Open truncates, so
    // the session worth reading is usually the one that just crashed and the
    // user relaunched before sending the file. Keep one generation.
    const std::wstring logPath = dir + L"" MEHT_MOD_NAME L".log";
    DWORD rotateErr = 0;
    if (!MoveFileExW(logPath.c_str(), (dir + L"" MEHT_MOD_NAME L".prev.log").c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        rotateErr = GetLastError();
        if (rotateErr == ERROR_FILE_NOT_FOUND) rotateErr = 0;
    }
    log::Open(logPath);
    log::Line("=== %s v%s loaded ===", MEHT_MOD_NAME, MEHT_VERSION);
    if (rotateErr != 0) {
        log::Line("WARNING: could not rotate the previous log to .prev.log (error %lu)",
                  rotateErr);
    }

    cameraunlock::diagnostics::InstallCrashHandler();
    LogFingerprint();

    const std::string ini = ToUtf8(dir) + MEHT_MOD_NAME ".ini";
    g_config.Load(ini);
    log::Line("[config] port=%u enableOnStartup=%d aimDecouple=%d localSmoothing=%.2f "
              "remoteSmoothing=%.2f worldYaw=%d",
              g_config.Port, g_config.EnableOnStartup, g_config.AimDecoupling,
              g_config.LocalSmoothing, g_config.RemoteSmoothing, g_config.WorldSpaceYaw);

    // Remove the game's ~60 fps cap by disabling UE3's frame-rate smoother in the
    // runtime TdEngine.ini. Done as early as possible so the write lands before the
    // engine reads its config this launch. Independent of the camera build profile.
    framerate::Init(g_config);

    g_trackingEnabled.store(g_config.EnableOnStartup);
    camera_hook::SetWorldSpaceYaw(g_config.WorldSpaceYaw);

    // Configure the shared tracking pipeline from the mod config. Rotation
    // sensitivity/smoothing/invert flow into the TrackingProcessor; position
    // settings into the PositionProcessor (its default limits are preserved).
    cameraunlock::SensitivitySettings sens;
    sens.yaw = g_config.YawSensitivity;
    sens.pitch = g_config.PitchSensitivity;
    sens.roll = g_config.RollSensitivity;
    sens.invert_yaw = g_config.InvertYaw;
    sens.invert_pitch = g_config.InvertPitch;
    sens.invert_roll = g_config.InvertRoll;
    g_session.GetProcessor().SetSensitivity(sens);

    cameraunlock::PositionSettings psens = g_session.GetPositionProcessor().GetSettings();
    psens.sensitivity_x = g_config.PositionSensitivityX;
    psens.sensitivity_y = g_config.PositionSensitivityY;
    psens.sensitivity_z = g_config.PositionSensitivityZ;
    psens.invert_x = g_config.InvertPositionX;
    psens.invert_y = g_config.InvertPositionY;
    psens.invert_z = g_config.InvertPositionZ;
    g_session.GetPositionProcessor().SetSettings(psens);

    // Both smoothing values go into both processors; which one applies is
    // decided per frame from the packet source address inside Update(). Set
    // after SetSettings, which would otherwise overwrite the position pair.
    g_session.SetLocalSmoothing(g_config.LocalSmoothing);
    g_session.SetRemoteSmoothing(g_config.RemoteSmoothing);

    g_session.SetMode(g_config.PositionEnabled
                          ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);

    g_receiver.SetLog([](const std::string& msg) {
        log::Line("[udp] %s", msg.c_str());
    });
    if (g_receiver.Start(g_config.Port)) {
        log::Line("[udp] listening on port %u", g_config.Port);
    } else {
        log::Line("[udp] port %u busy; background retry active", g_config.Port);
    }

    RegisterHotkeys();
    if (g_hotkeys.Start(16)) {
        log::Line("[input] hotkey poller running");
    } else {
        log::Line("[input] ERROR: hotkey poller failed to start");
    }

    g_loopThread = std::thread(Phase1Loop);
    log::Line("[init] Phase 1 bootstrap complete");
}

void Stop() {
    g_stop.store(true);
    if (g_loopThread.joinable()) g_loopThread.join();
    camera_probe::Shutdown();
    g_hotkeys.Stop();
    g_receiver.Stop();
    log::Line("=== %s unloaded ===", MEHT_MOD_NAME);
    log::Close();
}

}  // namespace meht
