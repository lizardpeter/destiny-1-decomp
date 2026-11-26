#pragma once
#include "nrender.h"
#include <cstring>
#include <limits>
namespace d1::ng {
// Ordered consecutive indexed draws may share all native state. No sorting,
// no merging across writes, no dynamic/inline geometry or target feedback.
// The leader must additionally prove its GPU buffers cover the whole resource.
inline bool same_native_draw_state(const Draw& a,const Draw& b){
    if(a.kind!=Draw::Indexed||b.kind!=Draw::Indexed||a.prim!=b.prim||a.prim==0x13||
       a.index32!=b.index32||a.vs!=b.vs||a.ps!=b.ps||
       a.shader_res[0]!=b.shader_res[0]||a.shader_res[1]!=b.shader_res[1]||
       a.blend!=b.blend||a.target_mask!=b.target_mask||a.su_sc_mode!=b.su_sc_mode||
       a.depth_ctl!=b.depth_ctl||a.stencil_ctl!=b.stencil_ctl||a.stencil_ref!=b.stencil_ref||
       a.poly_offset!=b.poly_offset||a.poly_scale!=b.poly_scale||
       a.znear!=b.znear||a.zfar!=b.zfar||a.zk!=b.zk||
       a.clear_flags||b.clear_flags||!a.inline_vertices.empty()||!b.inline_vertices.empty()||
       std::memcmp(a.vp,b.vp,sizeof a.vp)||std::memcmp(a.color,b.color,sizeof a.color)||
       std::memcmp(a.depth,b.depth,sizeof a.depth)||
       std::memcmp(a.vb,b.vb,sizeof a.vb)||std::memcmp(a.vres,b.vres,sizeof a.vres)||
       std::memcmp(&a.ires,&b.ires,sizeof a.ires))return false;
    for(unsigned s=0;s<2;++s){
        if(a.tex_mask[s]!=b.tex_mask[s]||a.smp_mask[s]!=b.smp_mask[s]||
           a.active_tex_mask[s]!=b.active_tex_mask[s]||
           a.native_tex_ready[s]!=a.active_tex_mask[s]||
           b.native_tex_ready[s]!=b.active_tex_mask[s]||
           a.tex_filter_mask[s]!=b.tex_filter_mask[s]||a.tex_sig[s]!=b.tex_sig[s])return false;
        // Compare every resolved slot: a shader mask published later could
        // otherwise expose a slot omitted from the first comparison.
        if(std::memcmp(a.tex_view[s],b.tex_view[s],sizeof a.tex_view[s])||
           std::memcmp(a.tex_dk[s],b.tex_dk[s],sizeof a.tex_dk[s])||
           std::memcmp(a.smp_lin[s],b.smp_lin[s],sizeof a.smp_lin[s])||
           std::memcmp(a.smp_near[s],b.smp_near[s],sizeof a.smp_near[s]))return false;
        for(unsigned i=0;i<16;++i)
            if(a.cb[s][i].addr!=b.cb[s][i].addr||a.cb[s][i].size!=b.cb[s][i].size||
               ((a.active_tex_mask[s]>>i&1)&&
                (!a.tex_res[s][i]||a.tex_res[s][i]!=b.tex_res[s][i])))return false;
    }
    return true;
}
inline bool native_index_range(const Draw& d,uint32_t& first){
    const uint64_t width=d.index32?4:2;
    if(!d.ires.id||!d.count||d.index_addr<d.ires.base)return false;
    const uint64_t rel=d.index_addr-d.ires.base;
    if(rel>d.ires.size||rel%width||uint64_t(d.count)*width>d.ires.size-rel||
       rel/width>std::numeric_limits<uint32_t>::max())return false;
    first=uint32_t(rel/width);return true;
}
} // namespace d1::ng
