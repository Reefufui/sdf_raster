#pragma once

#include <string>
#include <vector>

#include "LiteMath.h"

namespace sdf_raster {

struct SdfOctreeNode {
  float values [8];
  unsigned offset; // offset for children (they are stored together). 0 offset means it's a leaf
};

struct SdfOctree {
  std::vector <SdfOctreeNode> nodes;
};

void load_sdf_octree (SdfOctree &scene, const std::string &path);
void save_sdf_octree (const SdfOctree &scene, const std::string &path);
void dump_sdf_octree_text (const SdfOctree &scene, const std::string &path_to_dump);
float sample_sdf (const SdfOctree& scene, const LiteMath::float3& p);

}

