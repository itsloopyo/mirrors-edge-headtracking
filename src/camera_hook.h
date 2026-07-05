#pragma once

#include <cstdint>

namespace meht::camera_hook {

// Installs the head-tracking injection by hooking UProperty::CopyCompleteValue
// (the native the UE3 script VM uses to commit CameraCache.POV each frame). The
// detour lets the game write its clean POV, then - when the copy targets the live
// player camera's POV - adds the current head rotation to the rendered view.
// moduleBase is the running module base; copyCompleteValueRVA comes from the
// matched build profile. Returns true if the hook installed.
bool Install(std::uintptr_t moduleBase, std::uintptr_t copyCompleteValueRVA);

// Enables or disables injection. When disabled the detour is a no-op passthrough,
// so the rendered view is exactly the game's own POV.
void SetEnabled(bool enabled);

// Selects the yaw composition. true (default) = world-space, horizon-locked yaw
// about world up; false = camera-local yaw about the camera's current up-axis.
void SetWorldSpaceYaw(bool worldSpace);
bool GetWorldSpaceYaw();

// Sets the processed head rotation (degrees, already centered/smoothed). Stored
// as UE3 rotator units and added to the camera POV on the next commit. Yaw right
// and pitch up are positive, matching the observed POV rotator signs.
void SetHeadRotationDegrees(float yaw, float pitch, float roll);

// Enables or disables the position (6DOF) offset independently of rotation, so
// the tracking-mode cycle can run rotation-only or position-only. When false the
// POV.Location is left exactly as the game wrote it.
void SetPositionEnabled(bool enabled);

// Sets the processed head position offset in Unreal units, expressed in the
// camera-local frame: +right, +up, +back. The detour rotates it by the clean
// (game-authored) camera orientation and adds it to POV.Location, so the offset
// follows body orientation and only the rendered viewpoint moves - aim, physics,
// and raycasts read the untouched clean location.
void SetHeadPosition(float rightUU, float upUU, float backUU);

// Reads the last POV commit's clean (game-authored) rotation and the head delta
// the detour applied, all in UE3 rotator units. The reticle overlay uses these
// to project clean aim into the rendered head-rotated view. Returns false until
// the first POV commit is seen.
bool GetPovState(int& cleanPitch, int& cleanYaw, int& cleanRoll,
                 int& deltaPitch, int& deltaYaw, int& deltaRoll);

}  // namespace meht::camera_hook
