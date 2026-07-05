#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace meht {

// Per-build offset profile for MirrorsEdge.exe. RVAs are pinned to a PE
// fingerprint; the mod fingerprints the running EXE at load and routes to the
// matching profile. No match leaves the mod dormant (no hooks installed).
//
// Append-only: when a patch changes RVAs, add a new profile to the TOP of
// kKnownProfiles. Never edit an existing profile's RVAs in place - users on the
// old build still match it by fingerprint. See doctrine "Maintain compatibility
// across new patches".
struct BuildProfile {
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;

    // UProperty::CopyCompleteValue (module-relative). The UE3 script VM routes the
    // per-frame CameraCache.POV commit through this generic struct-copy native
    // (memcpy dst,src). The camera hook post-hooks it and, when dst covers the
    // live camera's POV, adds the head-rotation delta. 0 = camera hook dormant.
    std::uintptr_t CopyCompleteValueRVA;

    // ACamera::execSetViewTarget (base UE3 native, statically resolvable). Used
    // by the camera probe to capture a live Camera object pointer at runtime.
    std::uintptr_t SetViewTargetRVA;
};

// Most recent build first (index 0 is the diagnostic primary). Null while no
// profile has been discovered yet.
extern const BuildProfile* const kKnownProfiles;
extern const int kKnownProfileCount;

// Returns the profile whose fingerprint matches the running module, or nullptr
// if the running build is unknown.
const BuildProfile* FindProfile(const cameraunlock::memory::PeFingerprint& running);

}  // namespace meht
