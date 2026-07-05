#pragma once

#include <cstdint>

namespace meht::pov {

// ACamera CameraCache / POV memory layout, shared by the probe (captures the
// camera), the hook (injects the head delta), and the reticle projection (reads
// the FOV). The FCameraCacheEntry begins at +0x4D0:
//
//   +0x4D0  float    TimeStamp            (whole-cache-entry copy destination)
//   +0x4D4  FVector  POV.Location         (POV struct destination)
//   +0x4E0  int32    POV.Rotation.Pitch
//   +0x4E4  int32    POV.Rotation.Yaw
//   +0x4E8  int32    POV.Rotation.Roll
//   +0x4EC  float    POV.FOV
//
// Fixed UE3 struct offsets, so they are build-independent - defined once here
// rather than re-derived in each translation unit.
constexpr std::uintptr_t kCacheEntryOffset = 0x4D0;
constexpr std::uintptr_t kPovOffset        = 0x4D4;  // == POV.Location.X (FVector start)
constexpr std::uintptr_t kLocXOffset       = 0x4D4;  // POV.Location.X (float)
constexpr std::uintptr_t kLocYOffset       = 0x4D8;  // POV.Location.Y (float)
constexpr std::uintptr_t kLocZOffset       = 0x4DC;  // POV.Location.Z (float)
constexpr std::uintptr_t kPitchOffset      = 0x4E0;
constexpr std::uintptr_t kYawOffset        = 0x4E4;
constexpr std::uintptr_t kRollOffset       = 0x4E8;
constexpr std::uintptr_t kFovOffset        = 0x4EC;
constexpr std::uintptr_t kRotationEnd      = 0x4F0;  // one dword past POV.Rotation.Roll

// UE3 FRotator packs a full turn into 65536 units; conversions to/from degrees.
constexpr float  kUnitsPerDegree = 65536.0f / 360.0f;
constexpr double kDegPerUnit     = 360.0 / 65536.0;

}  // namespace meht::pov
