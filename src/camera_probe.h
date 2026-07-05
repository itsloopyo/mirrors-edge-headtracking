#pragma once

#include <cstdint>

namespace meht::camera_probe {

// Hooks ACamera::execSetViewTarget to capture a live Camera object pointer, so
// its CameraCache.POV layout (FVector Location, FRotator Rotation, float FOV)
// can be read at runtime - the static image can't resolve ME's per-game camera
// natives (their lookup-table entries are filled at runtime). moduleBase is the
// running module base; setViewTargetRVA comes from the matched build profile.
//
// Returns true if the hook was installed.
bool Install(std::uintptr_t moduleBase, std::uintptr_t setViewTargetRVA);

// Request an on-demand dump of the captured Camera (e.g. from a hotkey). The
// dump happens on the next Poll() so it runs on the probe thread, not the
// hotkey thread.
void RequestDump();

// Request a one-shot hardware write-watchpoint on CameraCache.Rotation.Yaw
// (cam+0x4E4). When the game thread next writes it, a VEH logs the writer's
// instruction address + module RVA - that instruction lives inside the
// per-frame POV committer (FillCameraCache/UpdateCamera), the injection point.
void RequestWatch();

// Call each frame. Once a Camera has been captured and settled for a moment,
// dumps its memory to the log exactly once (offset / hex / float / rotator-int
// views) so the POV fields can be identified.
void Poll();

// Stops the page-guard watch worker and removes its vectored exception handler.
// Safe to call when the watch was never armed (no-op). Call before the module
// unloads so a stale VEH can't fire into unloaded code.
void Shutdown();

// The most recent live Camera (APlayerCameraManager) captured from
// SetViewTarget, or nullptr before one is seen. The camera hook uses this to
// recognize the POV-commit struct copy for the active player camera.
void* GetCamera();

}  // namespace meht::camera_probe
