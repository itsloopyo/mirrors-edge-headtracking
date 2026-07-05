#include "build_profiles.h"

namespace meht {

// Append-only registry, most recent build first.
//
// Confirmed in-game on the Steam release (fingerprint captured from the running
// EXE at load): TimeDateStamp 0x4965FFF1 (2009-01-08), SizeOfImage 0x01F73000,
// CheckSum 0x01E827C7.
static const BuildProfile kProfiles[] = {
    {
        "steam-win32-20090108",
        { 0x4965FFF1u, 0x01F73000u, 0x01E827C7u },
        0xD53410u,   // CopyCompleteValueRVA: per-frame POV-commit struct copy
        0x6DA810u,   // SetViewTargetRVA: ACamera::execSetViewTarget
    },
};

const BuildProfile* const kKnownProfiles = kProfiles;
const int kKnownProfileCount = static_cast<int>(sizeof(kProfiles) / sizeof(kProfiles[0]));

const BuildProfile* FindProfile(const cameraunlock::memory::PeFingerprint& running) {
    for (int i = 0; i < kKnownProfileCount; ++i) {
        if (kKnownProfiles[i].Fingerprint.Matches(running)) {
            return &kKnownProfiles[i];
        }
    }
    return nullptr;
}

}  // namespace meht
