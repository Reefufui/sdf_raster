// linear_indirect_prep.comp.slang

#include "shader_common.hpp"

[[vk::binding (0, 0)]] StructuredBuffer <uint> g_counter;
[[vk::binding (0, 1)]] RWStructuredBuffer <IndirectDispatch> g_indirect_dispatch;

[[vk::push_constant]] uint workgroup_size;

[shader ("compute")]
[numthreads (1, 1, 1)]
void main () {
    uint count = g_counter [0];
    uint num_groups = (count + workgroup_size - 1) / workgroup_size;
    g_indirect_dispatch [0].x = num_groups;
    g_indirect_dispatch [0].y = 1;
    g_indirect_dispatch [0].z = 1;
}
