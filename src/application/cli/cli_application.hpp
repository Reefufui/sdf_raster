// application/cli/cli_application.hpp
#pragma once

#include "offscreen_render_target.hpp"

#include "scenes/base/scene_manager.hpp"
#include "state.hpp"
#include "vulkan/vulkan_context.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sdf_raster {

struct BenchmarkConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    std::filesystem::path scene_path;
    std::filesystem::path output_path = "benchmark_results.json";
    std::filesystem::path screenshot_path;
    uint32_t warmup_frames = 100;
    uint32_t measurement_frames = 500;

    nlohmann::json extra_params;
};

struct CLIArguments {
    std::optional <uint32_t> width;
    std::optional <uint32_t> height;
    std::optional <std::filesystem::path> scene_path;
    std::optional <std::filesystem::path> output_path;
    std::optional <std::filesystem::path> screenshot_path;
    std::optional <uint32_t> warmup_frames;
    std::optional <uint32_t> measurement_frames;
    std::optional <std::filesystem::path> export_mesh_path;
    std::optional <int> export_mesh_max_lod;
    std::optional <int> export_mesh_cpu_depth;
};

class CLIApplication {
public:
    explicit CLIApplication (const SessionState& session, int argc, char* argv[]);
    int run ();

private:
    std::shared_ptr <SceneManager> create_scene_manager ();
    std::shared_ptr <Scene> load_scene (const std::filesystem::path& path, SceneManager& scene_manager);
    CLIArguments parse_args (int argc, char* argv[]);
    BenchmarkConfig fill_config (const CLIArguments& args, const SessionState& session);
    void write_results (const std::vector <double>& gpu_times_ns,
                        const BenchmarkConfig& config,
                        std::shared_ptr <VulkanContext> vulkan_context,
                        std::shared_ptr <Scene> scene,
                        double timestamp_period);
    void drain_pending_frames (std::shared_ptr <OffscreenRenderTarget> render_target);
    void run_benchmark (const BenchmarkConfig& config);
    void run_export (const std::filesystem::path& output_path, const std::filesystem::path& scene_path, int max_lod_override = -1, int cpu_depth_override = -1);

    SessionState session;
    int argc = 0;
    char** argv = nullptr;
};

} // namespace sdf_raster
