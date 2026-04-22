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

} // sdf_raster

template <>
struct std::formatter <sdf_raster::DrawMethod> : std::formatter <std::string_view> {
    auto format (sdf_raster::DrawMethod m, std::format_context& ctx) const {
        std::string_view name = "Unknown";
        switch (m) {
            case sdf_raster::DrawMethod::None:                     name = "None"; break;
            case sdf_raster::DrawMethod::Explicit:                 name = "Explicit Mesh via Forward Rendering"; break;
            case sdf_raster::DrawMethod::ExplicitDeferred:         name = "Explicit Mesh via Deferred Rendering"; break;
            case sdf_raster::DrawMethod::OctreeCompute:            name = "SDF-Octree via Compute Shaders"; break;
            case sdf_raster::DrawMethod::OctreeMesh:               name = "SDF-Octree via Mesh Shaders"; break;
            case sdf_raster::DrawMethod::SComTreeCompute:          name = "SComTree via Compute Shaders"; break;
            case sdf_raster::DrawMethod::SComTreeComputeDeferred:  name = "SComTree via Compute Shaders & Deferred Rendering"; break;
            case sdf_raster::DrawMethod::SComTreeMesh:             name = "SComTree via Mesh Shaders"; break;
            case sdf_raster::DrawMethod::SComTreeMeshDeferred:     name = "SComTree via Mesh Shaders & Deferred Rendering"; break;
        }
        return std::formatter <std::string_view>::format (name, ctx);
    }
};

