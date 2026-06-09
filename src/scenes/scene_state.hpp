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
    Fixed,   // Fixed LOD value from slider
    Global,  // Single LOD for entire model (distance-based)
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
    int min_lod {3};
    int frustum_culling_level {16};
    int occlusion_culling_level {16};
    LODMode lod_mode = LODMode::Fixed;
    int fixed_lod = 6;
    LiteMath::float4 octree_root_center {0.0f, 0.0f, 0.0f, 0.0f};
    LiteMath::float3 position {0.0f, 0.0f, 0.0f};
    LiteMath::float3 rotation {0.0f, 0.0f, 0.0f}; // in degrees
    LiteMath::float3 scale {1.0f, 1.0f, 1.0f};
    float lod_threshold_pixels {2.0f};
    float lod_aggressivity {1.0f};
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
        case LODMode::Fixed:   return "Fixed LOD";
        case LODMode::Global:  return "Global (distance-based)";
        case LODMode::PerNode: return "Per-Node (dynamic)";
    }
    return "Unknown";
}

} // sdf_raster

NLOHMANN_JSON_SERIALIZE_ENUM(sdf_raster::LODMode, {
    {sdf_raster::LODMode::Fixed, "fixed"},
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

