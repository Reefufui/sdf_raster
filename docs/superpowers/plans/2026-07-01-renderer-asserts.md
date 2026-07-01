# Renderer Resource Assertions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a comprehensive "pre-flight check" of asserts at the start of `RasterSComTreeViaComputeShadingDeferred::draw` to ensure all required Vulkan resources and pipelines are valid.

**Architecture:** All checks are consolidated at the function entry to fail-fast during development.

**Tech Stack:** C++, Vulkan, `assert`.

---

### Task 1: Add Comprehensive Assertions to RasterSComTreeViaComputeShadingDeferred::draw

**Files:**
- Modify: `src/engine/renderer.cpp`

- [ ] **Step 1: Read the existing draw function to confirm line numbers and context**

- [ ] **Step 2: Replace current minimal asserts with a comprehensive list at the start of the function**

```cpp
void Renderer::RasterSComTreeViaComputeShadingDeferred::draw (VkCommandBuffer cmd_buff, const std::unique_ptr <ModelResource>& model) {
    const auto scomtree_model = dynamic_cast <SComTreeTreeDescriptorSetInfo*> (model.get ());

    assert (cmd_buff != VK_NULL_HANDLE && "command buffer must be valid");
    assert (model != nullptr && "model must not be null");
    assert (scomtree_model && "reqired for RasterSComTreeViaComputeShadingDeferred::draw");
    assert (r.context && "renderer context must be initialized");
    assert (r.render_target && "render target must be initialized");

    // Descriptor Sets
    assert (r.active_leafs_ds && "active_leafs_ds must be valid");
    assert (r.frustum_ds && "frustum_ds must be valid");
    assert (r.hz_buffer_ds && "hz_buffer_ds must be valid");
    assert (r.lod_ds && "lod_ds must be valid");
    assert (r.mesh_ds && "mesh_ds must be valid");
    assert (r.indirect_dispatch_ds && "indirect_dispatch_ds must be valid");
    assert (r.draw_indexed_indirect_command_ds && "draw_indexed_indirect_command_ds must be valid");
    assert (r.marching_cubes_lookup_table_ds && "marching_cubes_lookup_table_ds must be valid");
    assert (r.deferred_shading && "deferred_shading must be initialized");

    // Pipelines & Layouts
    assert (r.traverse_scomtree_pipeline != VK_NULL_HANDLE);
    assert (r.compute_prepare_indirect_pipeline != VK_NULL_HANDLE);
    assert (r.marching_cubes_scomtree_pipeline != VK_NULL_HANDLE);
    assert (r.graphics_gbuffer_pipeline != VK_NULL_HANDLE);
    assert (r.graphics_gbuffer_pipeline_layout != VK_NULL_HANDLE);

    // Frame/Model Resources
    assert (scomtree_model->get_subtree_root_staging_buffer (r.frame_index) != VK_NULL_HANDLE);
    assert (scomtree_model->get_subtree_root_buffer (r.frame_index) != VK_NULL_HANDLE);
    assert (scomtree_model->get_descriptor_set (r.frame_index) != VK_NULL_HANDLE);

    auto subtree_count = scomtree_model->get_subtree_count ();
    if (!subtree_count) {
        return;
    }

    r.copy_subtrees (cmd_buff, subtree_count * sizeof (SComTreeStackElement)
        , scomtree_model->get_subtree_root_staging_buffer (r.frame_index)
        , scomtree_model->get_subtree_root_buffer (r.frame_index));
    r.reset_active_leafs_counter (cmd_buff);
    r.traverse_scomtree (cmd_buff, static_cast <uint32_t> (subtree_count), scomtree_model->get_descriptor_set (r.frame_index));
    r.clear_geometry (cmd_buff);
    r.prepare_indirect (cmd_buff, uint32_t {BRICKS_PER_COMPUTE_WORKGROUP});
    r.marching_cubes_scomtree (cmd_buff, scomtree_model->get_descriptor_set (r.frame_index));
    r.geometry_barrier (cmd_buff);
    r.deferred_rendering (cmd_buff);
}
```

- [ ] **Step 3: Commit the changes**

```bash
git add src/engine/renderer.cpp
git commit -m "feat: add comprehensive resource asserts to RasterSComTreeViaComputeShadingDeferred::draw"
```
