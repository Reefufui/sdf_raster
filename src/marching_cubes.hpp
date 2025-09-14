#pragma once

#include <functional>

#include "LiteMath.h"

#include "mesh.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

struct MarchingCubesSettings {
    float iso_level = 0.5f;
    int max_threads = 1;
};

std::vector <Mesh> create_mesh_marching_cubes (const MarchingCubesSettings settings, const SdfOctree& sdf_octree);

}

