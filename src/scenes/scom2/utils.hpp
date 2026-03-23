#pragma once

#include "scenes/scom2/header.hpp"

#include <cstdint>

namespace sdf_raster {

struct NodeHeadUnpacked {
    uint32_t base_type;
    uint32_t children_types;
    uint32_t base_links_end;
    uint32_t children_active;
};

NodeHeadUnpacked unpack_node_head (const Header &header, uint32_t node0, uint32_t node1);

struct SdfDAGChildEdge {
    uint32_t child_offset;
    uint32_t rotation_id;
};

SdfDAGChildEdge unpack_child_edge (const Header &header, uint32_t edge0, uint32_t edge1);

struct SdfDAGDataEdge {
    uint32_t data_offset;
    uint32_t rotation_id;
    uint32_t type_id;
    float    add;
};

SdfDAGDataEdge unpack_data_edge (const Header& header, float max_val, uint32_t edge0, uint32_t edge1);

float get_max_sdf_val (float level_size);

}

