#pragma once
#include "spirv.h"

namespace d1::ng {
// Reduce the true 64-bit sample counts to a visibility predicate. OR the
// two words of every counter: no integer-64 shader feature or overflow.
inline std::vector<uint32_t> query_reduce_shader() {
    using namespace d1::spv;
    Module m;
    m.capability(CapShader);
    const Id u = m.t_u32(), v = m.t_void(), b = m.t_bool();
    const Id zero = m.c_u32(0), one = m.c_u32(1), two = m.c_u32(2);
    const Id arr = m.t_runtime_array(u);
    m.decorate(arr, DecArrayStride, {4});
    const Id block = m.t_struct({arr});
    m.decorate(block, DecBlock);
    m.member_decorate(block, 0, DecOffset, {0});
    const Id storage = m.t_ptr(SCStorageBuffer, block);
    const Id word = m.t_ptr(SCStorageBuffer, u);
    const Id src = m.variable(storage, SCStorageBuffer);
    const Id dst = m.variable(storage, SCStorageBuffer);
    m.decorate(src, DecDescriptorSet, {0});
    m.decorate(src, DecBinding, {0});
    m.decorate(dst, DecDescriptorSet, {0});
    m.decorate(dst, DecBinding, {1});
    const Id push_block = m.t_struct({u, u});
    m.decorate(push_block, DecBlock);
    m.member_decorate(push_block, 0, DecOffset, {0});
    m.member_decorate(push_block, 1, DecOffset, {4});
    const Id push = m.variable(m.t_ptr(SCPushConstant, push_block), SCPushConstant);
    const Id pu = m.t_ptr(SCPushConstant, u);
    const Id f = m.begin_function(v, m.t_func(v));
    const Id i = m.function_variable(m.t_ptr(SCFunction, u));
    const Id accum = m.function_variable(m.t_ptr(SCFunction, u));
    const Id count = m.op_r(OpLoad, u, {m.op_r(OpAccessChain, pu, {push, zero})});
    const Id slot = m.op_r(OpLoad, u, {m.op_r(OpAccessChain, pu, {push, one})});
    m.op(OpStore, {i, zero});
    m.op(OpStore, {accum, zero});
    const Id header = m.label(), body = m.label(), cont = m.label(), merge = m.label();
    m.op(OpBranch, {header});
    m.place_label(header);
    const Id index = m.op_r(OpLoad, u, {i});
    const Id cond = m.op_r(OpULessThan, b, {index, count});
    m.op(OpLoopMerge, {merge, cont, 0});
    m.op(OpBranchConditional, {cond, body, merge});
    m.place_label(body);
    const Id low_index = m.op_r(OpIMul, u, {index, two});
    const Id high_index = m.op_r(OpIAdd, u, {low_index, one});
    const Id low = m.op_r(OpLoad, u, {m.op_r(OpAccessChain, word, {src, zero, low_index})});
    const Id high = m.op_r(OpLoad, u, {m.op_r(OpAccessChain, word, {src, zero, high_index})});
    const Id bits = m.op_r(OpBitwiseOr, u, {low, high});
    const Id prior = m.op_r(OpLoad, u, {accum});
    m.op(OpStore, {accum, m.op_r(OpBitwiseOr, u, {prior, bits})});
    m.op(OpBranch, {cont});
    m.place_label(cont);
    m.op(OpStore, {i, m.op_r(OpIAdd, u, {index, one})});
    m.op(OpBranch, {header});
    m.place_label(merge);
    const Id result = m.op_r(OpINotEqual, b, {m.op_r(OpLoad, u, {accum}), zero});
    const Id predicate = m.op_r(OpSelect, u, {result, one, zero});
    m.op(OpStore, {m.op_r(OpAccessChain, word, {dst, zero, slot}), predicate});
    m.op(OpReturn, {});
    m.end_function();
    m.entry_point(ExecGLCompute, f, "main", {src, dst, push});
    m.execution_mode(f, ModeLocalSize, {1, 1, 1});
    return m.binary();
}
} // namespace d1::ng
