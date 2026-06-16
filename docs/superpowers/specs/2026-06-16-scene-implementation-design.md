# Specification: Scene and RenderableItem Implementation

**Date**: 2026-06-16
**Status**: Approved

## 1. Overview
The goal is to implement a `Scene` class that manages a collection of `RenderableItem` objects. The scene will be loaded from a JSON file that defines unique meshes and their instances with associated transformation matrices.

## 2. Components

### 2.1 RenderableItem (`src/scenes/renderable_item.hpp`)
A simple structure representing an instance of a model in the scene.
- `std::shared_ptr <Model> model`: Pointer to the loaded model resource.
- `LiteMath::float4x4 transform`: Model matrix for this instance.

### 2.2 Scene (`src/scenes/scene.hpp` and `src/scenes/scene.cpp`)
A concrete class responsible for managing scene objects.
- `bool load (const std::filesystem::path& path)`: Loads the scene configuration from a JSON file.
- `std::vector <RenderableItem> items`: Private storage for all instances in the scene.

## 3. Data Format (`assets/scenes/test_scene.json`)
The JSON structure supports instancing by separating mesh definitions from their occurrences.

```json
{
    "meshes": [
        {
            "id": "cube_model",
            "path": "data/models/cube.obj"
        }
    ],
    "instances": [
        {
            "mesh_id": "cube_model",
            "transform": [
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0
            ]
        }
    ]
}
```

## 4. Implementation Details
- **Coding Style**: Adhere to `CODE_STYLE.md` (spaces before brackets, `this->` prefix, Egyptian brackets).
- **Validation**:
    - Method `load` returns `false` on failure.
    - Log successful loading of unique meshes and total instances.
- **Dependencies**: Uses `nlohmann/json` (as seen in `src/state_json.hpp`) and `LiteMath`.

## 5. Verification Plan
1. Create header and source files with the defined structure.
2. Create a test JSON file in `assets/scenes/`.
3. Verify compilation.
4. (Next step) Implement a basic test or log output to verify that data is correctly loaded into `std::vector <RenderableItem>`.
