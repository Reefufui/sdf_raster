// engine/utils/frustum_culling.hpp
#pragma once
#include "shader_common.hpp"

#include <vector>

struct NodeContext;
struct SComTreeStackElement;

void frustum_culling (const std::vector <NodeContext>& nodes, const FrustumGeometry& frustum, std::vector <NodeContext>& result);
void frustum_culling (const std::vector <SComTreeStackElement>& nodes, const FrustumGeometry& frustum, std::vector <SComTreeStackElement>& result);

