// scenes/scene_state.hpp
#pragma once

#include "engine/camera.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <format>

namespace sdf_raster {

enum class DrawMethod : uint8_t { None
    , Explicit , ExplicitDeferred
    , OctreeCompute , OctreeMesh
    , SComTreeCompute, SComTreeComputeDeferred, SComTreeMesh, SComTreeMeshDeferred
};

enum class LODMode : uint8_t {
    Global,  // Single LOD for entire model
    PerNode  // Per-node LOD based on node position
};

struct SceneState {
    Camera camera {};
    DrawMethod draw_method = DrawMethod::None;
    std::string name {"N/A"};
    std::filesystem::path path {};
    int octree_depth {16};
    int cpu_traversed {3};
    int gpu_descend {5}; // NOTE: deprecated
    int max_lod {16};
    int frustum_culling_level {16};
    int occlusion_culling_level {16};
    LODMode lod_mode = LODMode::Global;
    float min_lod_distance {0.5f};  // LOD distance range (set to near plane by default)
    float max_lod_distance {100.f}; // LOD distance range (set to far plane by default)
};

constexpr std::string_view draw_method_name (DrawMethod m) {
    switch (m) {
        case DrawMethod::None:                     return "None";
        case DrawMethod::Explicit:                 return "Explicit Mesh via Forward Rendering";
        case DrawMethod::ExplicitDeferred:         return "Explicit Mesh via Deferred Rendering";
        case DrawMethod::OctreeCompute:            return "SDF-Octree via Compute Shaders";
        case DrawMethod::OctreeMesh:               return "SDF-Octree via Mesh Shaders";
        case DrawMethod::SComTreeCompute:          return "SComTree via Compute Shaders";
        case DrawMethod::SComTreeComputeDeferred:  return "SComTree via Compute Shaders & Deferred Rendering";
        case DrawMethod::SComTreeMesh:             return "SComTree via Mesh Shaders";
        case DrawMethod::SComTreeMeshDeferred:     return "SComTree via Mesh Shaders & Deferred Rendering";
    }
    return "Unknown";
}

constexpr std::string_view lod_mode_name (LODMode m) {
    switch (m) {
        case LODMode::Global:  return "Global (whole model)";
        case LODMode::PerNode: return "Per-Node (dynamic)";
    }
    return "Unknown";
}

} // sdf_raster

NLOHMANN_JSON_SERIALIZE_ENUM(sdf_raster::LODMode, {
    {sdf_raster::LODMode::Global, "global"},
    {sdf_raster::LODMode::PerNode, "per_node"},
});

template <>
struct std::formatter <sdf_raster::DrawMethod> : std::formatter <std::string_view> {
    auto format (sdf_raster::DrawMethod m, std::format_context& ctx) const {
        return std::formatter <std::string_view>::format (sdf_raster::draw_method_name (m), ctx);
    }
};

template <>
struct std::formatter <sdf_raster::LODMode> : std::formatter <std::string_view> {
    auto format (sdf_raster::LODMode m, std::format_context& ctx) const {
        return std::formatter <std::string_view>::format (sdf_raster::lod_mode_name (m), ctx);
    }
};

