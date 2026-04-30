// application/cli/cli_application.hpp
#pragma once

#include "offscreen_render_target.hpp"

#include "vulkan/vulkan_context.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sdf_raster {

struct BenchmarkConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    std::filesystem::path scene_path;
    std::filesystem::path output_path = "benchmark_results.json";
    uint32_t warmup_frames = 100;
    uint32_t measurement_frames = 500;

    nlohmann::json extra_params;
};

class CLIApplication {
public:
    explicit CLIApplication (int argc, char* argv[]);
    int run ();

private:
    BenchmarkConfig parse_args (int argc, char* argv[]);
    void write_results (const std::vector <double>& gpu_times_ns,
                        const BenchmarkConfig& config,
                        std::shared_ptr <VulkanContext> vulkan_context,
                        double timestamp_period);
    void drain_pending_frames (std::shared_ptr <OffscreenRenderTarget> render_target);
    void run_benchmark (const BenchmarkConfig& config);

    int argc = 0;
    char** argv = nullptr;
};

} // namespace sdf_raster
