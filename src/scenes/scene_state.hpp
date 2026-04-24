#pragma once

#include "camera.hpp"

#include <filesystem>
#include <string>
#include <format>

namespace sdf_raster {

enum class DrawMethod : uint8_t { None
    , Explicit , ExplicitDeferred
    , OctreeCompute , OctreeMesh
    , SComTreeCompute, SComTreeComputeDeferred, SComTreeMesh, SComTreeMeshDeferred
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

} // sdf_raster

template <>
struct std::formatter <sdf_raster::DrawMethod> : std::formatter <std::string_view> {
    auto format (sdf_raster::DrawMethod m, std::format_context& ctx) const {
        return std::formatter <std::string_view>::format (sdf_raster::draw_method_name (m), ctx);
    }
};

