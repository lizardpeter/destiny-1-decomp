#pragma once
#include <cstdint>

namespace d1::render {
// Native PC window, input and audio services. No firmware types or GPU
// command builders cross this interface.
// Native PC frontend input snapshot. The existing action mapping uses the
// retail button-bit convention at the input adapter boundary.
struct PadState {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2;
};
// Stats overlay (FPS, resolutions, v-sync, GPU, game state); F3 toggles it.
bool overlay_enabled();
void set_overlay(bool on);

bool window_active();
bool read_pad(PadState& out);  // false when there is no window: caller keeps its own input
// Raw mouse motion (device counts, no OS acceleration) accumulated since the last call while
// the window holds the cursor; the counts are consumed. False when the cursor is not captured.
bool take_mouse_motion(int32_t& dx, int32_t& dy);
// A weapon key (weapon_1..3 binding) pressed since the last call: weapon index 0..2, or -1.
int take_weapon_request();
// Button prompts: true while the keyboard and mouse are the last input used (and prompts are
// not switched off), so the UI shows key names; prompt_label writes the name of the key bound
// to the action behind a DualShock 4 glyph character (U+E000..), e.g. u"E", returning its
// length, or 0 when nothing is bound.
bool keyboard_prompts();
int prompt_label(uint32_t glyph, char16_t* out, int max);
// Audio (renderer build with a window): open the speakers for 7.1 float frames (SDL order
// L R C LFE back-L back-R side-L side-R; the device downmixes), and queue a buffer. Output
// blocks while about three buffers are queued, which paces the game's mixer like the
// console's audio port.
bool audio_open(uint32_t freq, uint32_t frames);
void audio_output(const float* frames8, uint32_t frames);
void run_window_loop();        // main thread; returns when the window is closed


} // namespace d1::render
