#pragma once

#include <cstdint>

namespace sdf_raster {
namespace scom2 {

struct Header {
    uint32_t brick_size;
    uint32_t v_size;
    uint32_t bits_per_value;
    uint32_t values_per_uint;
    uint32_t value_mask;
    uint32_t bitmask_len;
    uint32_t dimension;

    uint32_t child_rot_shift;
    uint32_t child_rot_mask;
    uint32_t child_add_shift;
    uint32_t child_add_mask;
    uint32_t child_offset_mask;
    uint32_t child_offset_off;
    uint32_t node_offset_mask;
    uint32_t uints_per_link;
    uint32_t unique_brick_prefix;
    uint32_t unique_brick_offset_mask;

    uint32_t children_types_shift;
    uint32_t children_types_mask;
    uint32_t base_reference_shift;
    uint32_t children_active_bits_shift;
    uint32_t children_active_bits_mask;
    uint32_t references_offset;
    uint32_t reference_bits;
    uint32_t reference_mask;
    uint32_t references_per_uint;
    uint32_t links_offset;
    uint32_t max_surface_count;
    uint32_t max_surface_count_per_leaf;

    uint32_t bricks_step;
    uint32_t bricks_arr_offset;
    uint32_t nodes_arr_offset;
    uint32_t root_node_off;

    uint32_t has_channels;
    uint32_t has_surfaces;
    uint32_t has_multi_nodes;

    int tex_id_off;
    int mat_id_off;
    int all_float_tex_id_off;
    int all_int_mat_id_off;

    float max_val;
    uint32_t max_depth;
    float user_params [7];

    //to allow further extensions without breaking binary compatibility
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
    uint32_t _pad3;
    uint32_t _pad4;
};

} // scom2
} // sdf_raster

