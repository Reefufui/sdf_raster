// mip_max_pooling.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[shader ("compute")]
[numthreads (16, 16, 1)]
void main (uint3 thread_id : SV_GroupThreadID) {
}

