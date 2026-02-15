// workgroup_prefix_sum.slang

#include "common.h"

groupshared uint s_data1 [PREFIX_SUM_WORKGROUP_SIZE];
groupshared uint s_data2 [PREFIX_SUM_WORKGROUP_SIZE];

// result[i] = data[0] + ... + data[i-1]
void workgroup_prefix_sum_inclusive (uint local_thread_index) {
    for (int offset = 1; offset < PREFIX_SUM_WORKGROUP_SIZE; offset *= 2) {
        GroupMemoryBarrierWithGroupSync ();
        if (local_thread_index >= offset) {
            s_data1 [local_thread_index] += s_data1 [local_thread_index - offset];
            s_data2 [local_thread_index] += s_data2 [local_thread_index - offset];
        }
    }
    GroupMemoryBarrierWithGroupSync ();
}

