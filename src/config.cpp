#include "config.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/logging/file_log.h"

namespace meht {
namespace {

namespace log = cameraunlock::logging;

// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    if (reader.ReadString(section, key, "").empty()) return;
    log::Line(
        "WARNING: Config key [%s] %s has been retired and is IGNORED. Smoothing is "
        "now two keys: LocalSmoothing (default 0, applies to a tracker on this "
        "machine) and RemoteSmoothing (default 0.15, applies to a tracker on the "
        "network). The old value is not migrated because the semantics changed - it "
        "carried a hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

}  // namespace

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
    LocalSmoothing = ini.ReadFloat(rot, "LocalSmoothing", LocalSmoothing);
    RemoteSmoothing = ini.ReadFloat(rot, "RemoteSmoothing", RemoteSmoothing);
    WarnRetiredSmoothingKey(ini, rot, "Smoothing");

    const char* pos = "Position";
    PositionEnabled = ini.ReadBool(pos, "PositionEnabled", PositionEnabled);
    PositionSensitivityX = ini.ReadFloat(pos, "PositionSensitivityX", PositionSensitivityX);
    PositionSensitivityY = ini.ReadFloat(pos, "PositionSensitivityY", PositionSensitivityY);
    PositionSensitivityZ = ini.ReadFloat(pos, "PositionSensitivityZ", PositionSensitivityZ);
    // No PositionSmoothing key: position uses the same LocalSmoothing /
    // RemoteSmoothing pair as rotation. It was retired alongside the rotation
    // key, so an existing ini can still carry it; warn rather than drop it
    // silently.
    WarnRetiredSmoothingKey(ini, pos, "PositionSmoothing");
    InvertPositionX = ini.ReadBool(pos, "InvertPositionX", InvertPositionX);
    InvertPositionY = ini.ReadBool(pos, "InvertPositionY", InvertPositionY);
    InvertPositionZ = ini.ReadBool(pos, "InvertPositionZ", InvertPositionZ);
    PositionScaleUU = ini.ReadFloat(pos, "PositionScaleUU", PositionScaleUU);

    const char* keys = "Hotkeys";
    KeyToggleTracking = ini.ReadHex(keys, "ToggleTracking", KeyToggleTracking);
    KeyCycleMode = ini.ReadHex(keys, "CycleMode", KeyCycleMode);
    KeyToggleYawMode = ini.ReadHex(keys, "ToggleYawMode", KeyToggleYawMode);
}

}  // namespace meht
