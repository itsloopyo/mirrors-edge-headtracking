#include "config.h"

#include "cameraunlock/config/ini_reader.h"

namespace meht {

void Config::Load(const std::string& iniPath) {
    cameraunlock::IniReader ini;
    if (!ini.Open(iniPath)) {
        return;  // No file: ship defaults.
    }

    const char* net = "Network";
    // A raw ReadInt()->uint16_t cast silently wraps an out-of-range value (e.g.
    // 70000 -> 4464), binding a surprise port. Keep the default unless the value
    // is a real port number.
    int port = ini.ReadInt(net, "Port", Port);
    if (port >= 1 && port <= 65535) Port = static_cast<uint16_t>(port);

    const char* gen = "General";
    EnableOnStartup = ini.ReadBool(gen, "EnableOnStartup", EnableOnStartup);
    AimDecoupling = ini.ReadBool(gen, "AimDecoupling", AimDecoupling);
    ShowReticle = ini.ReadBool(gen, "ShowReticle", ShowReticle);
    DataFreshnessMs = ini.ReadInt(gen, "DataFreshnessMs", DataFreshnessMs);
    WorldSpaceYaw = ini.ReadBool(gen, "WorldSpaceYaw", WorldSpaceYaw);

    const char* perf = "Performance";
    UnlockFrameRate = ini.ReadBool(perf, "UnlockFrameRate", UnlockFrameRate);

    const char* rot = "Rotation";
    YawSensitivity = ini.ReadFloat(rot, "YawSensitivity", YawSensitivity);
    PitchSensitivity = ini.ReadFloat(rot, "PitchSensitivity", PitchSensitivity);
    RollSensitivity = ini.ReadFloat(rot, "RollSensitivity", RollSensitivity);
    InvertYaw = ini.ReadBool(rot, "InvertYaw", InvertYaw);
    InvertPitch = ini.ReadBool(rot, "InvertPitch", InvertPitch);
    InvertRoll = ini.ReadBool(rot, "InvertRoll", InvertRoll);
    Smoothing = ini.ReadFloat(rot, "Smoothing", Smoothing);

    const char* pos = "Position";
    PositionEnabled = ini.ReadBool(pos, "PositionEnabled", PositionEnabled);
    PositionSensitivityX = ini.ReadFloat(pos, "PositionSensitivityX", PositionSensitivityX);
    PositionSensitivityY = ini.ReadFloat(pos, "PositionSensitivityY", PositionSensitivityY);
    PositionSensitivityZ = ini.ReadFloat(pos, "PositionSensitivityZ", PositionSensitivityZ);
    PositionSmoothing = ini.ReadFloat(pos, "PositionSmoothing", PositionSmoothing);
    InvertPositionX = ini.ReadBool(pos, "InvertPositionX", InvertPositionX);
    InvertPositionY = ini.ReadBool(pos, "InvertPositionY", InvertPositionY);
    InvertPositionZ = ini.ReadBool(pos, "InvertPositionZ", InvertPositionZ);
    PositionScaleUU = ini.ReadFloat(pos, "PositionScaleUU", PositionScaleUU);

    const char* keys = "Hotkeys";
    KeyRecenter = ini.ReadHex(keys, "Recenter", KeyRecenter);
    KeyToggleTracking = ini.ReadHex(keys, "ToggleTracking", KeyToggleTracking);
    KeyCycleMode = ini.ReadHex(keys, "CycleMode", KeyCycleMode);
    KeyToggleYawMode = ini.ReadHex(keys, "ToggleYawMode", KeyToggleYawMode);
}

}  // namespace meht
