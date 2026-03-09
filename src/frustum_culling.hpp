#pragma once
#include "shader_common.hpp"

#include <vector>

void frustum_culling (const std::vector <NodeContext>& nodes, const FrustumGeometry& frustum, std::vector <NodeContext>& result);

