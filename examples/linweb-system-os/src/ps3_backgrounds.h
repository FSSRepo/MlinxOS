#pragma once

// Registers the shader backgrounds of the demo with the engine (`background: ring`).
void register_ps3_backgrounds();
// The loudness (0..1) and the bass (0..1) of what plays now; the ring follows them. Called by the module on every frame.
void set_music_levels(float level, float bass);
