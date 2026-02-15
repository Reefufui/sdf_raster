// pass1_prefix_sum.slang

#include "workgroup_prefix_sum.h"

[[vk::binding (1, 0)]] RWStructuredBuffer <uint> g_count;
[[vk::binding (2, 0)]] RWStructuredBuffer <uint> g_data1;
[[vk::binding (3, 0)]] RWStructuredBuffer <uint> g_data2;

[[vk::binding (0, 1)]] RWStructuredBuffer <uint> g_workgroup_sums;

[shader ("compute")]
[numthreads (PREFIX_SUM_WORKGROUP_SIZE, 1, 1)]
void main (uint local_thread_index : SV_GroupIndex, uint3 dispatchThreadID : SV_DispatchThreadID) {
    const uint g_index = dispatchThreadID.x;

    s_data1 [local_thread_index] = g_data1 [g_index];
    s_data2 [local_thread_index] = g_data2 [g_index];

    workgroup_prefix_sum_inclusive (local_thread_index);

    if (local_thread_index == 0) {
        g_data1 [0] = 0;
        g_data2 [0] = 0;
    } else {
        g_data1 [g_index] = s_data1 [local_thread_index - 1];
        g_data2 [g_index] = s_data2 [local_thread_index - 1];
    }
}

