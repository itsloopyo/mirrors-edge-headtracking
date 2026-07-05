#pragma once

#include "config.h"

namespace meht::framerate {

// Removes Mirror's Edge's ~60 fps ceiling.
//
// The cap is not V-Sync: UE3's frame-rate smoother clamps the tick rate to
// [MinSmoothedFrameRate, MaxSmoothedFrameRate] (62 by default) whenever
// bSmoothFrameRate is set, regardless of the V-Sync option. Those keys live in
// the runtime TdEngine.ini under [Engine.GameEngine], which the engine reads at
// startup (and is NOT integrity-checked, unlike DefaultEngine.ini).
//
// Init() writes bSmoothFrameRate=False there when cfg.UnlockFrameRate is set,
// which makes UGameEngine::GetMaxTickRate return 0 (uncapped). The write races
// the engine's config load; our loader thread runs before WinMain, so it
// normally lands first and takes effect this launch, next launch at worst.
//
// Config-based rather than an in-memory code patch on purpose: it survives every
// game patch with no per-build offset to maintain.
void Init(const Config& cfg);

}  // namespace meht::framerate
