#include "reticle_overlay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#define CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION
#include "cameraunlock/rendering/dx9_overlay.h"

#include "cameraunlock/rendering/aim_quat_projection.h"
#include "cameraunlock/unreal/ue_math.h"

#include "camera_hook.h"
#include "camera_probe.h"
#include "pov_layout.h"
#include "cameraunlock/logging/file_log.h"

namespace meht::reticle_overlay {
namespace {

namespace log = cameraunlock::logging;
namespace rndr = cameraunlock::rendering;
namespace ue = cameraunlock::unreal;

rndr::DX9Overlay g_overlay;
std::atomic<bool> g_enabled{false};

// Default vertical FOV when the live camera can't be read (out of gameplay).
constexpr float kFallbackFov = 90.0f;

// Reads POV.FOV from the live camera each frame so the projection tracks any FOV
// the game sets (see pov_layout.h for the offset). No C++ objects with
// destructors live in this frame so the SEH filter is legal here.
float ReadCameraFov() {
    void* cam = camera_probe::GetCamera();
    if (!cam) return kFallbackFov;
    float fov = kFallbackFov;
    __try {
        fov = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(cam) + pov::kFovOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return kFallbackFov;
    }
    // Reject implausible values (uninitialised camera, wrong offset on a menu cam).
    if (!(fov > 20.0f && fov < 170.0f)) return kFallbackFov;
    return fov;
}

// Projects the clean aim direction into the head-tracked view and returns its
// screen position (in a w x h viewport). Returns false when the aim is behind the
// tracked view (extreme head turn).
bool ComputeAimScreen(float w, float h, float& sx, float& sy) {
    int cp, cy, cr, dp, dy, dr;
    if (!camera_hook::GetPovState(cp, cy, cr, dp, dy, dr)) return false;

    const double cleanPitch = cp * pov::kDegPerUnit;
    const double cleanYaw = cy * pov::kDegPerUnit;
    const double cleanRoll = cr * pov::kDegPerUnit;
    const double trackedPitch = (cp + dp) * pov::kDegPerUnit;
    const double trackedYaw = (cy + dy) * pov::kDegPerUnit;
    const double trackedRoll = (cr + dr) * pov::kDegPerUnit;

    // qrel = trackedView^-1 * cleanView. Rotating camera-forward (1,0,0) by qrel
    // gives the clean-aim direction in the tracked camera's local frame, which is
    // exactly what the Hor+ projector consumes. Building both quaternions from the
    // full rotator components (not composing a delta) keeps the world-Z yaw and
    // roll exact regardless of base pitch.
    const ue::FQuat4d qClean = ue::QuatFromEulerDeg(cleanPitch, cleanYaw, cleanRoll);
    const ue::FQuat4d qTracked = ue::QuatFromEulerDeg(trackedPitch, trackedYaw, trackedRoll);
    const ue::FQuat4d qRel = ue::QuatMul(ue::QuatInv(qTracked), qClean);

    const float fov = ReadCameraFov();
    rndr::AimQuatProjection proj = rndr::ProjectAimQuatHorPlus(
        qRel.X, qRel.Y, qRel.Z, qRel.W, w, h, fov);
    if (!proj.inFront) return false;
    sx = proj.screenX;
    sy = proj.screenY;
    return true;
}

using DrawIdxPrimUPFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT,
                                            UINT, const void*, D3DFORMAT, const void*, UINT);
DrawIdxPrimUPFn g_origDrawIdxPrimUP = nullptr;

// IDirect3DDevice9::DrawIndexedPrimitiveUP slot in the device vtable.
constexpr int kDrawIndexedPrimitiveUPVTableIndex = 84;

// SEH-safe (no C++ objects with destructors): read the 4 quad vertices' screen
// x/y and vertex-0's rhw from an inline (user-pointer) vertex stream.
bool ReadQuadXY(const void* vtx, UINT stride, float xs[4], float ys[4], float& rhw) {
    __try {
        for (int i = 0; i < 4; ++i) {
            const float* v = reinterpret_cast<const float*>(
                reinterpret_cast<const unsigned char*>(vtx) + static_cast<size_t>(i) * stride);
            xs[i] = v[0];
            ys[i] = v[1];
        }
        rhw = reinterpret_cast<const float*>(vtx)[3];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SEH-safe: copy the 4 quad vertices into `out` and add (dx,dy) to each x/y.
bool BuildOffsetQuad(const void* vtx, UINT stride, float dx, float dy, void* out) {
    __try {
        std::memcpy(out, vtx, static_cast<size_t>(4) * stride);
        for (int i = 0; i < 4; ++i) {
            float* v = reinterpret_cast<float*>(
                reinterpret_cast<unsigned char*>(out) + static_cast<size_t>(i) * stride);
            v[0] += dx;
            v[1] += dy;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The stock reticle is the frame's last draw: a DrawIndexedPrimitiveUP quad
// (prim=2, 4 verts) with a tiny texture and pretransformed (XYZRHW) screen-space
// vertices centred on screen. When a draw matches, re-issue it with the vertices
// shifted to the clean-aim screen point - moving the native reticle onto the aim
// point (so it tracks where shots land, not where the head looks). Returns true
// if it issued the moved draw, and the caller must skip the original.
bool MaybeMoveReticle(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT minVtx, UINT numVerts,
                      UINT primCount, const void* idxData, D3DFORMAT idxFmt,
                      const void* vtxData, UINT vtxStride) {
    if (!g_enabled.load()) return false;
    if (primCount != 2 || numVerts != 4 || !vtxData || vtxStride < 16 || vtxStride > 64) return false;

    IDirect3DBaseTexture9* tex = nullptr;
    dev->GetTexture(0, &tex);
    UINT tw = 0, th = 0;
    if (tex) {
        if (tex->GetType() == D3DRTYPE_TEXTURE) {
            D3DSURFACE_DESC d{};
            if (SUCCEEDED(reinterpret_cast<IDirect3DTexture9*>(tex)->GetLevelDesc(0, &d))) {
                tw = d.Width;
                th = d.Height;
            }
        }
        tex->Release();
    }
    if (tw == 0 || tw > 64 || th > 64) return false;  // small reticle texture only

    float xs[4], ys[4], rhw = 0.0f;
    if (!ReadQuadXY(vtxData, vtxStride, xs, ys, rhw)) return false;
    if (rhw < 0.25f || rhw > 4.0f) return false;  // pretransformed (XYZRHW) screen space

    float cx = (xs[0] + xs[1] + xs[2] + xs[3]) * 0.25f;
    float cy = (ys[0] + ys[1] + ys[2] + ys[3]) * 0.25f;
    D3DVIEWPORT9 vp{};
    if (FAILED(dev->GetViewport(&vp)) || vp.Width == 0) return false;
    float scx = vp.Width * 0.5f, scy = vp.Height * 0.5f;
    // Must be a centred HUD element (the reticle), not some other small UP quad.
    if (std::fabs(cx - scx) > vp.Width * 0.15f || std::fabs(cy - scy) > vp.Height * 0.15f) return false;

    float ax = scx, ay = scy;
    if (!ComputeAimScreen(static_cast<float>(vp.Width), static_cast<float>(vp.Height), ax, ay))
        return false;  // aim behind tracked view: leave the reticle where it is

    unsigned char buf[4 * 64];
    if (!BuildOffsetQuad(vtxData, vtxStride, ax - cx, ay - cy, buf)) return false;
    g_origDrawIdxPrimUP(dev, type, minVtx, numVerts, primCount, idxData, idxFmt, buf, vtxStride);
    return true;
}

HRESULT __stdcall HookedDrawIdxPrimUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT minVtx,
                                      UINT numVerts, UINT primCount, const void* idxData,
                                      D3DFORMAT idxFmt, const void* vtxData, UINT vtxStride) {
    if (MaybeMoveReticle(dev, type, minVtx, numVerts, primCount, idxData, idxFmt, vtxData, vtxStride))
        return D3D_OK;
    return g_origDrawIdxPrimUP(dev, type, minVtx, numVerts, primCount, idxData, idxFmt, vtxData, vtxStride);
}

// Runs once when dx9_overlay captures the game's real device (via its CreateDevice
// hook). The stock reticle is a DrawIndexedPrimitiveUP (UE3 canvas tile) - hook
// that entry point so the reticle draw can be relocated to the aim point.
void OnDeviceReady(void** deviceVTable) {
    void* drawIdxUP = deviceVTable[kDrawIndexedPrimitiveUPVTableIndex];
    if (MH_CreateHook(drawIdxUP, &HookedDrawIdxPrimUP,
                      reinterpret_cast<void**>(&g_origDrawIdxPrimUP)) == MH_OK &&
        MH_EnableHook(drawIdxUP) == MH_OK) {
        log::Line("[reticle] DrawIndexedPrimitiveUP hooked (reticle mover ready)");
    } else {
        log::Line("[reticle] DrawIndexedPrimitiveUP hook failed; reticle stays centred");
    }
}

}  // namespace

bool Install() {
    rndr::SetDX9OverlayLogger([](const char* msg) { log::Line("%s", msg); });
    rndr::SetDX9DeviceReadyCallback(OnDeviceReady);
    if (!g_overlay.Install()) {
        log::Line("[reticle] DX9 device hook arm failed; reticle mover disabled");
        return false;
    }
    log::Line("[reticle] armed (waiting for game device)");
    return true;
}

void SetEnabled(bool enabled) { g_enabled.store(enabled); }

}  // namespace meht::reticle_overlay
