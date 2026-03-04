// octree_traversal.task.slang

#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

struct DummyPayload {
    uint data;
};

[shader ("amplification")]
[numthreads (1, 1, 1)]
void main () {
    DummyPayload data;
    DispatchMesh (1, 1, 1, data);
}

