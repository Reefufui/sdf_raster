# Specification: Scene Inspector UI Implementation

**Date**: 2026-06-17
**Status**: Approved

## 1. Overview
The goal is to implement a "Scene Inspector" window in the ImGui overlay. This window will allow users to inspect the current scene structure, including unique meshes and their instances with transformation matrices.

## 2. UI Structure

### 2.1 Scene Inspector Window
- A new ImGui window titled "Scene Inspector".
- Controlled by a toggle in `Settings` (e.g., `settings.show_scene_inspector`).

### 2.2 Sections
- **Meshes**: A list or table showing:
    - Mesh ID
    - File Path
- **Instances**: A list of `ImGui::TreeNode` elements.
    - Title format: `[Index] MeshID`
    - Content (when expanded): A 4x4 table showing the transformation matrix values (read-only).

## 3. Implementation Details

### 3.1 Data Access
- Add a public getter to `Scene` class to access `items`.
- Since `Scene` is now a concrete class, `ModelManager` should provide access to the current `Scene` instance.

### 3.2 ImGui Overlay Integration
- Modify `UI::update` in `src/application/gui/imgui_overlay.cpp`.
- Implement `void scene_inspector_window (const Scene& scene)`.
- Respect `CODE_STYLE.md` (spaces before brackets, `this->` prefix).

## 4. Verification Plan
1. Add `show_scene_inspector` to `Settings`.
2. Implement data access in `Scene` and `ModelManager`.
3. Implement the UI window logic.
4. Verify that the window correctly displays data from `assets/scenes/test_scene.json`.
5. Verify compilation and style.
