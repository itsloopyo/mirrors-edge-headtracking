#pragma once

namespace meht::reticle_overlay {

// Hooks the game's D3D9 device (via dx9_overlay's CreateDevice capture) so the
// stock reticle draw can be relocated to the clean-aim screen point. Requires
// MinHook to already be initialized (the camera probe/hook does this). Returns
// true if the device hook armed.
bool Install();

// Enables or disables moving the stock reticle. When disabled the game's reticle
// renders at its normal centre position (vanilla).
void SetEnabled(bool enabled);

}  // namespace meht::reticle_overlay
