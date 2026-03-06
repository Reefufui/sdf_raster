// mesh_shader_debug.slang

SetMeshOutputCounts (MAX_VERTS_PER_WORKGROUP, MAX_PRIMS_PER_WORKGROUP);
float4 stride;

stride = float4 (gi.x, gti.x, 0, 0);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 0].position = mul (pc.view_proj, stride + float4 (-0.5, 0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 1].position = mul (pc.view_proj, stride + float4 (0.5,  0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 2].position = mul (pc.view_proj, stride + float4 (0, -0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 0].vertex.color = float4 (0, 0, 1, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 1].vertex.color = float4 (0, 0, 1, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 2].vertex.color = float4 (0, 0, 1, 1);

stride = float4 (gi.x, gti.x, 1, 0);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 3].position = mul (pc.view_proj, stride + float4 (-0.5, 0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 4].position = mul (pc.view_proj, stride + float4 (0.5,  0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 5].position = mul (pc.view_proj, stride + float4 (0, -0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 3].vertex.color = float4 (0, 1, 0, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 4].vertex.color = float4 (0, 1, 0, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 5].vertex.color = float4 (0, 1, 0, 1);

stride = float4 (gi.x, gti.x, 2, 0);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 6].position = mul (pc.view_proj, stride + float4 (-0.5, 0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 7].position = mul (pc.view_proj, stride + float4 (0.5,  0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 8].position = mul (pc.view_proj, stride + float4 (0, -0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 6].vertex.color = float4 (0, 1, 1, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 7].vertex.color = float4 (0, 1, 1, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 8].vertex.color = float4 (0, 1, 1, 1);

stride = float4 (gi.x, gti.x, 3, 0);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 9].position = mul (pc.view_proj, stride + float4 (-0.5, 0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 10].position = mul (pc.view_proj, stride + float4 (0.5,  0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 11].position = mul (pc.view_proj, stride + float4 (0, -0.5, 0, 1));
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 9].vertex.color = float4 (1, 0, 0, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 10].vertex.color = float4 (1, 0, 0, 1);
out_verts [gti.x * MAX_VERTS_PER_VOXEL + 11].vertex.color = float4 (1, 0, 0, 1);

out_indices [gti.x * MAX_PRIMS_PER_VOXEL + 0] = uint3 (gti.x * MAX_VERTS_PER_VOXEL) + uint3 (0, 1, 2);
out_indices [gti.x * MAX_PRIMS_PER_VOXEL + 1] = uint3 (gti.x * MAX_VERTS_PER_VOXEL) + uint3 (3, 4, 5);
out_indices [gti.x * MAX_PRIMS_PER_VOXEL + 2] = uint3 (gti.x * MAX_VERTS_PER_VOXEL) + uint3 (6, 7, 8);
out_indices [gti.x * MAX_PRIMS_PER_VOXEL + 3] = uint3 (gti.x * MAX_VERTS_PER_VOXEL) + uint3 (9, 10, 11);

