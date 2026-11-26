#include "../tiling.h"
#include <cstdlib>
#include <cstdio>
#include <vector>
using namespace d1::tiling;
static void check(bool b){if(!b)std::abort();}
int main(){
    // F85CB0 linear-general has no row or slice alignment.
    Surface s;s.tile_mode=31;s.bpp=32;s.width=33;s.height=17;s.pitch=64;s.levels=3;
    layout(s);
    check(s.mips[0].pitch==64&&s.mips[0].size==64*17*4);
    check(s.mips[1].pitch==32&&s.mips[1].offset==64*17*4);
    check(s.mips[2].pitch==16&&s.total_size==64*17*4+32*8*4+16*4*4);
    // Linear-aligned requires a pitch multiple of max(8,64/bytes)
    // and a slice element count multiple of max(64,256/bytes).
    s.tile_mode=8;s.pitch=128;layout(s);
    check(s.mips[0].pitch==128&&s.mips[1].pitch==64&&s.mips[2].pitch==32);
    // F89BD0 pads six cube faces to eight allocation slices.
    s.tile_mode=31;s.width=16;s.height=16;s.pitch=16;s.layers=6;s.pow2=true;
    layout(s);
    check(s.mips[0].size==16*16*4*8);
    check(s.mips[1].offset==16*16*4*8&&s.mips[1].size==8*8*4*8);
    // Authored layer count can be smaller than descriptor allocation count.
    s.layers=3;s.stored_layers=8;layout(s);
    check(s.mips[0].size==16*16*4*8);
    // Read/write only actual layers, with padding left untouched.
    std::vector<uint8_t> tiled(s.total_size,0xff),linear(16*16*4*3,0x4a),out(linear.size()),ref(linear.size());
    tile(s,0,linear.data(),tiled.data());
    detile(s,0,tiled.data(),out.data());detile_reference(s,0,tiled.data(),ref.data());
    check(out==linear&&ref==linear&&tiled[linear.size()]==0xff);
    // BC conversion follows the shifted texel pitch.
    s={};s.tile_mode=31;s.bpp=128;s.width=33;s.height=17;s.pitch=64;s.levels=3;s.block=true;layout(s);
    check(s.mips[0].pitch==16&&s.mips[1].pitch==8&&s.mips[2].pitch==4);
    std::puts("Original layout contract: linear, mip pitch, BC and physical slice padding passed");
}
