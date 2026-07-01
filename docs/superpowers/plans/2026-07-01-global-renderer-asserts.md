# Global Renderer Resource Assertions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add comprehensive `assert` checks for all resources (unique_ptr and Vulkan handles) at the start of every method in the `Renderer` class and its nested classes.

**Architecture:** Fail-fast validation at function entry.

**Tech Stack:** C++, Vulkan, `assert`.

---

### Task 1: Add Assertions to Initialization Methods

**Files:**
- Modify: `src/engine/renderer.cpp`

- [ ] **Step 1: Add asserts to `init_*` methods (lines 448-807 approx)**
    - For each `init_..._pipeline` method, ensure `this->context` is valid.
    - If it's a compute pipeline, ensure required descriptor set layouts are valid.
    - If it's a graphics pipeline, ensure `this->forward_shading` or `this->deferred_shading` is valid.

- [ ] **Step 2: Commit initialization asserts**

```bash
git add src/engine/renderer.cpp
git commit -m "feat: add resource asserts to renderer initialization methods"
```

### Task 2: Add Assertions to Core Rendering & Command Methods

**Files:**
- Modify: `src/engine/renderer.cpp`

- [ ] **Step 1: Add asserts to `update`, `clear_geometry`, `copy_subtrees`, `traverse_*`, `prepare_*`, `marching_cubes_*`, `geometry_barrier`**
    - Check `cmd_buff`, `this->context`, and all accessed descriptor sets/pipelines.

- [ ] **Step 2: Commit core rendering asserts**

```bash
git add src/engine/renderer.cpp
git commit -m "feat: add resource asserts to core renderer methods"
```

### Task 3: Add Assertions to Nested RenderMethod Classes

**Files:**
- Modify: `src/engine/renderer.cpp`

- [ ] **Step 1: Add asserts to `RasterSComTreeViaComputeShadingForward`, `RasterSComTreeViaComputeShadingDeferred`, and `RasterMeshForward` methods**
    - Note: In these classes, `r` refers to the `Renderer` instance. Check `r.context`, `r.render_target`, and other resources accessed via `r`.

- [ ] **Step 2: Commit nested class asserts**

```bash
git add src/engine/renderer.cpp
git commit -m "feat: add resource asserts to nested render method classes"
```

### Task 4: Final Verification Build

- [ ] **Step 1: Run build check**

```bash
mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

- [ ] **Step 2: Fix any syntax errors or typos in asserts**

- [ ] **Step 3: Commit final fixes**

```bash
git add src/engine/renderer.cpp
git commit -m "fix: address build issues in renderer asserts"
```
