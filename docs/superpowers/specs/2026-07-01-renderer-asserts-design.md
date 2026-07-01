# Design Doc: Comprehensive Resource Assertions for RasterSComTreeViaComputeShadingDeferred::draw

## Problem
The `Renderer::RasterSComTreeViaComputeShadingDeferred::draw` function executes a complex sequence of compute and graphics operations. Currently, it lacks exhaustive checks for resource readiness and pipeline validity, which can lead to hard-to-debug Vulkan errors or crashes if state setup is incomplete.

## Goals
- Add a comprehensive "pre-flight check" using `assert` at the beginning of the `draw` function.
- Verify validity of:
    - Command buffer and input model.
    - All required descriptor sets (octree, active leafs, frustum, etc.).
    - All required pipelines (traversal, indirect preparation, MC, G-buffer).
    - Model-specific resources for the current frame.

## Implementation Details

### Target Function
`void Renderer::RasterSComTreeViaComputeShadingDeferred::draw (VkCommandBuffer cmd_buff, const std::unique_ptr <ModelResource>& model)` in `src/engine/renderer.cpp`.

### Assertions to Add
1.  **Core State:**
    - `cmd_buff != VK_NULL_HANDLE`
    - `model != nullptr`
    - `scomtree_model != nullptr` (cast check)
    - `r.context != nullptr`
    - `r.render_target != nullptr`

2.  **Descriptor Sets (Resource groups):**
    - `r.active_leafs_ds != nullptr`
    - `r.frustum_ds != nullptr`
    - `r.hz_buffer_ds != nullptr`
    - `r.lod_ds != nullptr`
    - `r.mesh_ds != nullptr`
    - `r.indirect_dispatch_ds != nullptr`
    - `r.draw_indexed_indirect_command_ds != nullptr`
    - `r.marching_cubes_lookup_table_ds != nullptr`
    - `r.deferred_shading != nullptr`

3.  **Pipelines & Layouts:**
    - `r.traverse_scomtree_pipeline != VK_NULL_HANDLE`
    - `r.compute_prepare_indirect_pipeline != VK_NULL_HANDLE`
    - `r.marching_cubes_scomtree_pipeline != VK_NULL_HANDLE`
    - `r.graphics_gbuffer_pipeline != VK_NULL_HANDLE`
    - `r.graphics_gbuffer_pipeline_layout != VK_NULL_HANDLE`

4.  **Model/Frame Resources:**
    - `scomtree_model->get_subtree_root_staging_buffer (r.frame_index) != VK_NULL_HANDLE`
    - `scomtree_model->get_subtree_root_buffer (r.frame_index) != VK_NULL_HANDLE`
    - `scomtree_model->get_descriptor_set (r.frame_index) != VK_NULL_HANDLE`

## Verification Plan
- Compilation check to ensure `assert` and all members are correctly referenced.
- (Manual) Verify that the application continues to run in debug mode when all resources are correctly initialized.
