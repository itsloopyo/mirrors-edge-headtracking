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

// Default FOV when the live camera cannot be read.
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

// The canvas vertex stream comes from the game's draw call.
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

bool MaybeMoveReticle(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT minVtx, UINT numVerts,
                      UINT primCount, const void* idxData, D3DFORMAT idxFmt,
                      const void* vtxData, UINT vtxStride) {
    if (!g_enabled.load()) return false;
    if (type != D3DPT_TRIANGLELIST || minVtx != 0 || primCount != 2 ||
        numVerts != 4 || !vtxData || vtxStride != 48) return false;

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
    if (rhw < 0.25f || rhw > 4.0f) return false;  // homogeneous canvas position

    float cx = (xs[0] + xs[1] + xs[2] + xs[3]) * 0.25f;
    float cy = (ys[0] + ys[1] + ys[2] + ys[3]) * 0.25f;
    D3DVIEWPORT9 vp{};
    if (FAILED(dev->GetViewport(&vp)) || vp.Width == 0 || vp.Height == 0) return false;
    // Canvas vertices pass through the shader's c5-c8 transform. Their origin
    // differs from the viewport origin on ultrawide displays.
    float canvas[4][4]{};
    if (FAILED(dev->GetVertexShaderConstantF(5, canvas[0], 4))) return false;
    const float determinant = canvas[0][0] * canvas[1][1] - canvas[1][0] * canvas[0][1];
    if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12f) return false;
    const float screenX = (cx * canvas[0][0] + cy * canvas[1][0] + canvas[3][0] + 1.0f) * vp.Width * 0.5f;
    const float screenY = (1.0f - cx * canvas[0][1] - cy * canvas[1][1] - canvas[3][1]) * vp.Height * 0.5f;
    float scx = vp.Width * 0.5f, scy = vp.Height * 0.5f;
    // Must be a centred HUD element (the reticle), not some other small UP quad.
    if (std::fabs(screenX - scx) > 2.0f || std::fabs(screenY - scy) > 2.0f) return false;

    float ax = scx, ay = scy;
    if (!ComputeAimScreen(static_cast<float>(vp.Width), static_cast<float>(vp.Height), ax, ay))
        return false;  // aim behind tracked view: leave the reticle where it is

    unsigned char buf[4 * 64];
    const float ndcX = 2.0f * (ax - screenX) / vp.Width;
    const float ndcY = -2.0f * (ay - screenY) / vp.Height;
    const float dx = (ndcX * canvas[1][1] - ndcY * canvas[1][0]) / determinant;
    const float dy = (ndcY * canvas[0][0] - ndcX * canvas[0][1]) / determinant;
    if (!BuildOffsetQuad(vtxData, vtxStride, dx, dy, buf)) return false;
    g_origDrawIdxPrimUP(dev, type, minVtx, numVerts, primCount, idxData, idxFmt, buf, vtxStride);
    static ULONGLONG last = 0;
    const auto now = GetTickCount64();
    if (now - last >= 30000) {
        last = now;
        log::Line("[reticle] screen=(%.1f,%.1f) aim=(%.1f,%.1f) canvasOffset=(%.1f,%.1f)",
                  screenX, screenY, ax, ay, dx, dy);
    }
    return true;
}

HRESULT __stdcall HookedDrawIdxPrimUP(IDirect3DDevice9* dev, D3DPRIMITIVETYPE type, UINT minVtx,
                                      UINT numVerts, UINT primCount, const void* idxData,
                                      D3DFORMAT idxFmt, const void* vtxData, UINT vtxStride) {
    if (MaybeMoveReticle(dev, type, minVtx, numVerts, primCount, idxData, idxFmt, vtxData, vtxStride))
        return D3D_OK;
    return g_origDrawIdxPrimUP(dev, type, minVtx, numVerts, primCount, idxData, idxFmt, vtxData, vtxStride);
}

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
    g_overlay.SetRenderCallback([](rndr::DX9DrawContext&) {
        static ULONGLONG last = GetTickCount64();
        static unsigned frames = 0;
        ++frames;
        const auto now = GetTickCount64();
        if (now - last >= 30000) {
            log::Line("[render] fps=%.1f", frames * 1000.0 / (now - last));
            last = now;
            frames = 0;
        }
    });
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
