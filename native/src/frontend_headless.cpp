// Explicit headless PC frontend for development and server-side testing.
#include "pc_frontend.h"
#include "settings.h"
#include <cstdio>
namespace d1::render {
bool window_active() { return false; }
bool read_pad(PadState&) { return false; }
bool take_mouse_motion(int32_t&, int32_t&) { return false; }
int take_weapon_request() { return -1; }
// headless testing: D1_PROMPTS=keyboard labels every glyph "[#<code>]"
bool keyboard_prompts() { return d1::settings().prompts == "keyboard"; }
int prompt_label(uint32_t glyph, char16_t* out, int max) {
    char text[16];
    const int n = std::snprintf(text, sizeof text, "[#%X]", unsigned(glyph));
    int k = 0;
    for (; k < n && k < max - 1; ++k) out[k] = char16_t(text[k]);
    out[k] = 0;
    return k;
}
bool audio_open(uint32_t, uint32_t) { return false; }
void audio_output(const float*, uint32_t) {}
void run_window_loop() {}

} // namespace d1::render
