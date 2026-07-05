#pragma once

#include <cstdint>
#include <string>

namespace meht {

// Mod configuration, loaded from MirrorsEdgeHeadTracking.ini next to the game
// EXE. Defaults follow the CameraUnlock doctrine (all sensitivities 1.0,
// smoothing 0.0 with the 0.15 baseline floor applied in processing).
struct Config {
    // Network
    uint16_t Port = 4242;

    // Master
    bool EnableOnStartup = true;
    bool AimDecoupling = true;
    bool ShowReticle = true;
    int DataFreshnessMs = 500;
    // Yaw mode: true = horizon-locked yaw about world up (default), false =
    // camera-local yaw about the camera's current up-axis. Runtime-toggleable.
    bool WorldSpaceYaw = true;

    // Performance. The game pins itself near 60 fps via UE3's frame-rate smoother
    // (bSmoothFrameRate / MaxSmoothedFrameRate in [Engine.GameEngine]), which the
    // V-Sync option does not affect. When set, the mod disables that smoother in
    // the runtime TdEngine.ini so the frame rate is limited only by the GPU. Note
    // Mirror's Edge ties some physics/animation timing to frame rate; very high
    // rates can introduce movement jank (this is the game, not the mod).
    bool UnlockFrameRate = true;

    // Rotation
    float YawSensitivity = 1.0f;
    float PitchSensitivity = 1.0f;
    float RollSensitivity = 1.0f;
    bool InvertYaw = false;
    bool InvertPitch = false;
    bool InvertRoll = false;
    float Smoothing = 0.0f;

    // Position (6DOF). PositionEnabled selects the startup tracking mode: true =
    // rotation + position, false = rotation only. The Cycle Mode hotkey then
    // rotates through rotation+position -> rotation only -> position only.
    bool PositionEnabled = true;
    float PositionSensitivityX = 1.0f;
    float PositionSensitivityY = 1.0f;
    float PositionSensitivityZ = 1.0f;
    float PositionSmoothing = 0.15f;
    bool InvertPositionX = true;
    bool InvertPositionY = false;
    bool InvertPositionZ = false;
    // Unreal units per meter: converts the processed offset (meters) to POV.Location
    // units. Mirror's Edge is UE3 world scale (~1 uu = 2 cm); tune to taste - larger
    // means a bigger viewpoint shift for the same head movement.
    float PositionScaleUU = 50.0f;

    // Hotkeys (Windows virtual key codes). Nav-cluster defaults per doctrine.
    int KeyRecenter = 0x24;       // Home
    int KeyToggleTracking = 0x23; // End
    int KeyCycleMode = 0x21;      // Page Up - cycle 6DOF / rotation / position
    int KeyToggleYawMode = 0x22;  // Page Down

    // Loads from disk. Missing file or keys leave defaults in place (boundary
    // validation only - everything inside trusts the contract).
    void Load(const std::string& iniPath);
};

}  // namespace meht
