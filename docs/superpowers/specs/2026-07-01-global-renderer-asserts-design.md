# Design Doc: Global Resource Assertions for Renderer Class

## Problem
Many methods in the `Renderer` class (and its nested classes) rely on initialized Vulkan resources, pipelines, and descriptor sets. Currently, these dependencies are often implicitly assumed or checked piecemeal. This makes debugging "late-stage" failures difficult.

## Goals
- Add exhaustive `assert` checks at the beginning of EVERY method in the `Renderer` class and its nested classes (`RasterMeshForward`, `RasterSComTreeViaComputeShadingForward`, etc.) that uses external resources.
- If a method uses a `unique_ptr` member, a Vulkan handle (compared with `VK_NULL_HANDLE`), or an input pointer/handle, it MUST be asserted at the start of the function.

## Implementation Details

### Scope
All `void Renderer::...` and `void Renderer::[NestedClass]::...` methods in `src/engine/renderer.cpp`.

### Assertion Strategy
- **Pointers:** `assert (ptr != nullptr)` or `assert (ptr)`
- **Vulkan Handles:** `assert (handle != VK_NULL_HANDLE)`
- **Context/Target:** `assert (this->context)` or `assert (r.context)`
- **Pipelines:** `assert (this->compute_..._pipeline != VK_NULL_HANDLE)`

### Example (Consolidated Checks)
For a method using `cmd_buff`, `active_leafs_ds`, and `traverse_scomtree_pipeline`:
```cpp
void Renderer::method (VkCommandBuffer cmd_buff) {
    assert (cmd_buff != VK_NULL_HANDLE);
    assert (this->active_leafs_ds != nullptr);
    assert (this->traverse_scomtree_pipeline != VK_NULL_HANDLE);
    
    // ... rest of the method
}
```

## Verification Plan
- Full build check (`cmake .. && make`) to ensure all member names and handle types are correctly referenced.
- Ensure `assert` is properly included (via `<cassert>`).
