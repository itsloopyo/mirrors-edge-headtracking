#include "camera_hook.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <intrin.h>

#include "camera_probe.h"
#include "pov_layout.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/unreal/ue_math.h"

namespace meht::camera_hook {
namespace {

namespace log = cameraunlock::logging;
namespace hooks = cameraunlock::hooks;
namespace ue = cameraunlock::unreal;

// A `CameraCache.POV = NewPOV` struct copy lands its destination at pov::kPovOffset;
// a whole-cache-entry copy at pov::kCacheEntryOffset. Either spans the rotation
// triple, so both are treated as a POV commit. Field offsets: see pov_layout.h.

// __thiscall UProperty::CopyCompleteValue(Dest, Src, SubobjectOuter,
// DestOwnerObject, InstanceGraph) - five stack args (the function ends RET 0x14),
// modeled as __fastcall so ECX (this) lands first. EDX is unused by the callee.
// Passing the wrong stack-arg count corrupts the stack (12-byte imbalance ->
// crash), so all five are declared and forwarded.
using CopyCompleteValueFn = void(__fastcall*)(void* thisPtr, void* edx, void* dst,
                                              void* src, void* a3, void* a4, void* a5);

CopyCompleteValueFn g_orig = nullptr;
void* g_target = nullptr;

std::atomic<bool> g_enabled{false};
// Horizon-locked (world-up) yaw by default; camera-local yaw when false.
std::atomic<bool> g_worldYaw{true};
std::atomic<int> g_yawUnits{0};
std::atomic<int> g_pitchUnits{0};
std::atomic<int> g_rollUnits{0};

// Position (6DOF) offset in Unreal units, camera-local (+right, +up, +back).
// Gated separately from rotation so the tracking-mode cycle can run
// rotation-only or position-only.
std::atomic<bool> g_positionEnabled{false};
std::atomic<float> g_posRight{0.0f};
std::atomic<float> g_posUp{0.0f};
std::atomic<float> g_posBack{0.0f};

// The head delta actually written into the POV this commit (final - clean), in
// rotator units. In world mode this equals the raw head delta; in local mode it
// is the delta after quaternion composition. The reticle projection reads these
// so its clean+delta reconstruction matches the rendered view in either mode.
std::atomic<int> g_appliedPitch{0};
std::atomic<int> g_appliedYaw{0};
std::atomic<int> g_appliedRoll{0};

// Last clean (game-authored) POV rotation captured just before the head delta is
// added, in UE3 rotator units. The reticle overlay reads these plus the applied
// delta to project clean aim into the rendered (head-rotated) view.
std::atomic<int> g_cleanPitch{0};
std::atomic<int> g_cleanYaw{0};
std::atomic<int> g_cleanRoll{0};
std::atomic<bool> g_havePov{false};

// Diagnostic: log distinct callers that copy the camera POV (read via src or
// write via dst), so the render read can be told apart from the game-logic read.
std::uintptr_t g_moduleBaseDiag = 0;
std::atomic<bool> g_diag{false};
constexpr int kMaxDiag = 24;
struct DiagEntry { std::uintptr_t ret; bool isRead; };
DiagEntry g_diagSeen[kMaxDiag];
std::atomic<int> g_diagCount{0};

bool InPovRange(std::uintptr_t addr, std::uintptr_t camAddr) {
    return addr >= camAddr + pov::kCacheEntryOffset && addr < camAddr + pov::kRotationEnd;
}

void MaybeDiag(void* dst, void* src, std::uintptr_t camAddr, std::uintptr_t ret) {
    auto dstAddr = reinterpret_cast<std::uintptr_t>(dst);
    auto srcAddr = reinterpret_cast<std::uintptr_t>(src);
    bool touchesDst = InPovRange(dstAddr, camAddr);
    bool touchesSrc = InPovRange(srcAddr, camAddr);
    if (!touchesDst && !touchesSrc) return;
    bool isRead = touchesSrc;
    int n = g_diagCount.load();
    for (int i = 0; i < n; ++i)
        if (g_diagSeen[i].ret == ret && g_diagSeen[i].isRead == isRead) return;
    if (n >= kMaxDiag) return;
    g_diagSeen[n] = {ret, isRead};
    g_diagCount.store(n + 1);
    log::Line("[diag] POV %s by caller rva=0x%X (dst+0x%X src+0x%X)",
              isRead ? "READ (src)" : "WRITE(dst)",
              (unsigned)(ret - g_moduleBaseDiag),
              touchesDst ? (unsigned)(dstAddr - camAddr) : 0u,
              touchesSrc ? (unsigned)(srcAddr - camAddr) : 0u);
}

void __fastcall Detour(void* thisPtr, void* edx, void* dst, void* src,
                       void* a3, void* a4, void* a5) {
    g_orig(thisPtr, edx, dst, src, a3, a4, a5);

    void* cam = camera_probe::GetCamera();
    if (!cam) return;
    auto camAddr = reinterpret_cast<std::uintptr_t>(cam);

    if (g_diag.load()) {
        MaybeDiag(dst, src, camAddr, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    }

    if (!g_enabled.load()) return;

    auto dstAddr = reinterpret_cast<std::uintptr_t>(dst);
    if (dstAddr != camAddr + pov::kPovOffset && dstAddr != camAddr + pov::kCacheEntryOffset) return;

    // The game just wrote the clean POV; add the head rotation so only the
    // rendered view is rotated. Because the clean POV is rewritten every commit,
    // this applies exactly one delta per frame regardless of how many commits
    // occur.
    auto* pitch = reinterpret_cast<std::int32_t*>(camAddr + pov::kPitchOffset);
    auto* yaw = reinterpret_cast<std::int32_t*>(camAddr + pov::kYawOffset);
    auto* roll = reinterpret_cast<std::int32_t*>(camAddr + pov::kRollOffset);
    // Capture the clean POV before rotating, so the reticle projection knows
    // where the game is actually aiming (Controller.Rotation, untouched).
    const int cleanP = *pitch, cleanY = *yaw, cleanR = *roll;
    g_cleanPitch.store(cleanP);
    g_cleanYaw.store(cleanY);
    g_cleanRoll.store(cleanR);
    g_havePov.store(true);

    const int dP = g_pitchUnits.load(), dY = g_yawUnits.load(), dR = g_rollUnits.load();

    int finalP, finalY, finalR;
    if (g_worldYaw.load()) {
        // World-space (horizon-locked): the UE3 FRotator's Yaw is a world-up (Z)
        // rotation, so adding the head delta straight onto the rotator yaws the
        // whole view around the horizon; pitch and roll add camera-locally. This
        // keeps "up" constant no matter where the camera is pitched.
        finalP = cleanP + dP;
        finalY = cleanY + dY;
        finalR = cleanR + dR;
    } else {
        // Camera-local: compose the head rotation into a single quaternion and
        // apply it in the camera's local frame (qTracked = qClean * qHead). Yaw
        // then rides the camera's current up-axis, so at extreme pitch the view
        // leans/rolls instead of staying horizon-locked.
        const ue::FQuat4d qClean = ue::QuatFromEulerDeg(
            cleanP * pov::kDegPerUnit, cleanY * pov::kDegPerUnit, cleanR * pov::kDegPerUnit);
        const ue::FQuat4d qHead = ue::QuatFromEulerDeg(
            dP * pov::kDegPerUnit, dY * pov::kDegPerUnit, dR * pov::kDegPerUnit);
        const ue::FRotator rot = ue::QuatToRotator(ue::QuatMul(qClean, qHead));
        finalP = static_cast<int>(std::lround(rot.Pitch * pov::kUnitsPerDegree));
        finalY = static_cast<int>(std::lround(rot.Yaw * pov::kUnitsPerDegree));
        finalR = static_cast<int>(std::lround(rot.Roll * pov::kUnitsPerDegree));
    }

    *pitch = finalP;
    *yaw = finalY;
    *roll = finalR;
    g_appliedPitch.store(finalP - cleanP);
    g_appliedYaw.store(finalY - cleanY);
    g_appliedRoll.store(finalR - cleanR);

    // Position (6DOF): translate the rendered viewpoint by the head offset. The
    // offset arrives camera-local (+right/+up/+back); rotate it by the clean
    // (game-authored) orientation so the lean follows body facing, then add it to
    // POV.Location. Rebuilt from the clean rotator each commit and applied to the
    // freshly-written clean location, so a single offset lands per frame and it
    // self-heals to vanilla the moment position is disabled.
    if (g_positionEnabled.load()) {
        const double back = g_posBack.load(), right = g_posRight.load(), up = g_posUp.load();
        if (back != 0.0 || right != 0.0 || up != 0.0) {
            const ue::FQuat4d qClean = ue::QuatFromEulerDeg(
                cleanP * pov::kDegPerUnit, cleanY * pov::kDegPerUnit, cleanR * pov::kDegPerUnit);
            // UE3 camera-local axes: +X forward, +Y right, +Z up. Back = -forward.
            const ue::FVector world = ue::QuatRotateVec(qClean, ue::FVector{-back, right, up});
            *reinterpret_cast<float*>(camAddr + pov::kLocXOffset) += static_cast<float>(world.X);
            *reinterpret_cast<float*>(camAddr + pov::kLocYOffset) += static_cast<float>(world.Y);
            *reinterpret_cast<float*>(camAddr + pov::kLocZOffset) += static_cast<float>(world.Z);
        }
    }

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        log::Line("[cam] first POV injection: dst=%p (cam+0x%X), mode=%s, applied yaw=%d pitch=%d roll=%d units",
                  dst, (unsigned)(dstAddr - camAddr), g_worldYaw.load() ? "world" : "local",
                  finalY - cleanY, finalP - cleanP, finalR - cleanR);
    }
}

}  // namespace

bool Install(std::uintptr_t moduleBase, std::uintptr_t copyCompleteValueRVA) {
    if (copyCompleteValueRVA == 0) {
        log::Line("[cam] no CopyCompleteValueRVA in profile; camera hook disabled");
        return false;
    }
    // The camera probe may have already initialized MinHook; that's fine.
    hooks::HookStatus init = hooks::HookManager::Instance().Initialize();
    if (init != hooks::HookStatus::Ok && init != hooks::HookStatus::ErrorAlreadyInitialized) {
        log::Line("[cam] MinHook init failed");
        return false;
    }
    g_moduleBaseDiag = moduleBase;
    g_diag.store(true);
    g_target = reinterpret_cast<void*>(moduleBase + copyCompleteValueRVA);
    hooks::HookStatus s = hooks::HookManager::Instance().CreateHook(
        g_target, reinterpret_cast<void*>(&Detour), reinterpret_cast<void**>(&g_orig));
    if (s != hooks::HookStatus::Ok) {
        log::Line("[cam] CreateHook(CopyCompleteValue @ %p) failed: %s",
                  g_target, hooks::HookStatusToString(s));
        return false;
    }
    s = hooks::HookManager::Instance().EnableHook(g_target);
    if (s != hooks::HookStatus::Ok) {
        log::Line("[cam] EnableHook failed: %s", hooks::HookStatusToString(s));
        return false;
    }
    log::Line("[cam] hooked CopyCompleteValue @ %p (POV injection ready)", g_target);
    return true;
}

void SetEnabled(bool enabled) { g_enabled.store(enabled); }

void SetWorldSpaceYaw(bool worldSpace) { g_worldYaw.store(worldSpace); }

bool GetWorldSpaceYaw() { return g_worldYaw.load(); }

bool GetPovState(int& cleanPitch, int& cleanYaw, int& cleanRoll,
                 int& deltaPitch, int& deltaYaw, int& deltaRoll) {
    if (!g_havePov.load()) return false;
    cleanPitch = g_cleanPitch.load();
    cleanYaw = g_cleanYaw.load();
    cleanRoll = g_cleanRoll.load();
    deltaPitch = g_appliedPitch.load();
    deltaYaw = g_appliedYaw.load();
    deltaRoll = g_appliedRoll.load();
    return true;
}

void SetHeadRotationDegrees(float yaw, float pitch, float roll) {
    g_yawUnits.store(static_cast<int>(yaw * pov::kUnitsPerDegree));
    g_pitchUnits.store(static_cast<int>(pitch * pov::kUnitsPerDegree));
    // Roll is inverted: the game's POV roll runs opposite the tracker convention.
    g_rollUnits.store(static_cast<int>(-roll * pov::kUnitsPerDegree));
}

void SetPositionEnabled(bool enabled) { g_positionEnabled.store(enabled); }

void SetHeadPosition(float rightUU, float upUU, float backUU) {
    g_posRight.store(rightUU);
    g_posUp.store(upUU);
    g_posBack.store(backUU);
}

}  // namespace meht::camera_hook
