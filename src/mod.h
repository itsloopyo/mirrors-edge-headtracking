#pragma once

namespace meht {

// Mod lifecycle entry points, called from DllMain via a bootstrap thread.
// Start() brings up logging, the crash handler, config, the UDP receiver and
// hotkeys, logs the running EXE fingerprint, and (Phase 1) spawns a background
// loop that exercises the tracking pipeline so loader-presence and networking
// can be confirmed from the log before any camera hook exists.
void Start();
void Stop();

}  // namespace meht
