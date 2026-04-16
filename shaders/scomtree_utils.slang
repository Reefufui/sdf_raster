// scomtree_utils.slang

#define SCOM2_CHILD_EMPTY 0

struct NodeHeadUnpacked {
    uint base_type;
    uint children_types;
    uint base_links_end;
    uint children_active;
};

struct SdfDAGChildEdge {
    uint child_offset;
    uint rotation_id;
};

struct SdfDAGDataEdge {
    uint data_offset;
    uint rotation_id;
    uint type_id;
    float add;
};

NodeHeadUnpacked unpack_node_head (ConstantBuffer <SComTreeHeader> header, uint32_t node0, uint32_t node1) {
    NodeHeadUnpacked head_unpacked;
    head_unpacked.base_type = node0 & 0xFFu;

    if (header.has_multi_nodes != 0)
        head_unpacked.children_types = (node0 >> header.children_types_shift) & header.children_types_mask;
    else
        head_unpacked.children_types = (node1 >> header.children_types_shift) & header.children_types_mask;

    if (header.has_multi_nodes != 0)
        head_unpacked.base_links_end = (node0 >> header.base_reference_shift) & header.reference_mask;
    else
        head_unpacked.base_links_end = (head_unpacked.base_type == SCOM2_CHILD_EMPTY ? 0 : (header.has_surfaces == 0 ? header.has_channels : 1+header.has_channels));

    head_unpacked.children_active = (node0 >> header.children_active_bits_shift) & header.children_active_bits_mask;
    return head_unpacked;
}

SdfDAGChildEdge unpack_child_edge (ConstantBuffer <SComTreeHeader> header, uint32_t edge0, uint32_t edge1) {
    SdfDAGChildEdge edge;
    edge.rotation_id = (edge0 >> header.child_rot_shift) & header.child_rot_mask;
    edge.child_offset = edge1 & header.node_offset_mask;
    return edge;
}

SdfDAGDataEdge unpack_data_edge (ConstantBuffer <SComTreeHeader> header, float max_val, uint32_t edge0, uint32_t edge1) {
    SdfDAGDataEdge edge;
    bool is_unique = (edge0 & header.unique_brick_prefix) == header.unique_brick_prefix;
    bool no_add = is_unique || (header.child_add_mask == 0);
    if (header.max_val > 0)
        edge.add = no_add ? 0.0f : header.max_val * (2 * ((edge0 >> header.child_add_shift) & header.child_add_mask) / float(header.child_add_mask) - 1);
    else
        edge.add = no_add ? 0.0f : max_val * (2 * ((edge0 >> header.child_add_shift) & header.child_add_mask) / float(header.child_add_mask) - 1);

    edge.rotation_id = is_unique ? 0 : (edge0 >> header.child_rot_shift) & header.child_rot_mask;
    edge.data_offset = is_unique ? edge1 & header.unique_brick_offset_mask : edge1 & header.child_offset_mask;
    edge.type_id = 0;
    return edge;
}

float get_max_sdf_val (float level_size) {
  return /*sqrt(3)*/ 1.7320508f * 2.0f / level_size;
}


