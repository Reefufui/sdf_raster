// pass1_prefix_sum.slang

#include "workgroup_prefix_sum.h"

globallycoherent RWStructuredBuffer <int> g_data; // .size () % PREFIX_SUM_WORKGROUP_SIZE == 0
RWStructuredBuffer <int> g_block_sums;

[shader ("compute")]
[numthreads (PREFIX_SUM_WORKGROUP_SIZE, 1, 1)]
void main (uint3 groupID : SV_GroupID, uint3 dispatchThreadID : SV_DispatchThreadID) {
    // const uint g_index = dispatchThreadID.x;
    // s_data [local_thread_index] = g_data [g_index];
    // GroupMemoryBarrierWithGroupSync ();
    //
    // int totalBlockSum = workgroup_prefix_sum (local_thread_index);
    //
    // g_data [g_index] = s_data [local_thread_index];
    //
    // if (local_thread_index == 0) {
    //     g_block_sums [groupID.x] = totalBlockSum;
    // }
}

