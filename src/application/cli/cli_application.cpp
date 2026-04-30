// application/cli/cli_application.cpp
#include "cli_application.hpp"

#include "engine/sdf_rasterizer.hpp"
#include "logger.hpp"
#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace sdf_raster {

CLIApplication::CLIApplication (int argc_, char* argv_[])
    : argc (argc_)
    , argv (argv_) {}

int CLIApplication::run () {
    BenchmarkConfig config = parse_args (argc, argv);
    run_benchmark (config);
    return 0;
}

BenchmarkConfig CLIApplication::parse_args (int argc, char* argv[]) {
    BenchmarkConfig config;
    // TODO: real argument parsing.
    // For now, keep defaults and maybe accept a single scene path argument.
    if (argc > 1) {
        config.scene_path = std::filesystem::path (argv[1]);
    }
    return config;
}

void CLIApplication::run_benchmark (const BenchmarkConfig& config) {
    SessionState session;
    load_session (session, "/tmp/sdf_raster.json"); // TODO: fill from config

    LOG_INFO ("[Benchmark] Starting with resolution {}x{}, {} warmup frames, {} measurement frames",
              config.width, config.height, config.warmup_frames, config.measurement_frames);

    auto vulkan_context = std::make_shared <VulkanContext> ();
    vulkan_context->init ();

    auto render_target = std::make_shared <OffscreenRenderTarget> (
        vulkan_context,
        config.width,
        config.height,
        VK_FORMAT_B8G8R8A8_UNORM
    );

    auto renderer = std::make_unique <SDFRasterizer> (vulkan_context, render_target);
    renderer->init ();

    const uint32_t frames_in_flight = render_target->get_max_frames_in_flight ();
    uint32_t frame_idx = 0;
    const uint32_t total_frames = config.warmup_frames + config.measurement_frames;

    for (uint32_t frame = 0; frame < total_frames; ++frame) {
        if (frame == config.warmup_frames) {
            render_target->clear_gpu_times ();
            LOG_INFO ("[Benchmark] Warmup complete. Starting measurement phase.");
        }

        VkCommandBuffer cmd_buff = render_target->begin_frame (frame_idx);
        if (cmd_buff == VK_NULL_HANDLE) {
            throw std::runtime_error ("[Benchmark] begin_frame returned VK_NULL_HANDLE.");
        }

        renderer->update (frame, session.settings);
        renderer->render (cmd_buff);

        render_target->end_frame (cmd_buff, frame_idx);
        frame_idx = (frame_idx + 1) % frames_in_flight;
    }

    this->drain_pending_frames (render_target);

    std::vector <double> times_ns (
        render_target->get_gpu_times_ns ().begin (),
        render_target->get_gpu_times_ns ().end ()
    );

    this->write_results (
        times_ns,
        config,
        vulkan_context,
        render_target->get_timestamp_period ()
    );

    renderer->shutdown ();
    render_target->shutdown ();
    vulkan_context->shutdown ();
}

void CLIApplication::drain_pending_frames (std::shared_ptr <OffscreenRenderTarget> render_target) {
    render_target->collect_pending_timestamps ();
}

void CLIApplication::write_results (const std::vector <double>& gpu_times_ns,
                                    const BenchmarkConfig& config,
                                    std::shared_ptr <VulkanContext> vulkan_context,
                                    double timestamp_period) {
    if (gpu_times_ns.empty ()) {
        LOG_WARN ("[Benchmark] No GPU times recorded.");
        return;
    }

    nlohmann::json result;

    result["config"] = {
        {"width", config.width},
        {"height", config.height},
        {"scene", config.scene_path.string ()},
        {"warmup_frames", config.warmup_frames},
        {"measurement_frames", config.measurement_frames},
        {"timestamp_period_ns", timestamp_period}
    };

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties (vulkan_context->get_physical_device (), &props);

    result["config"]["gpu"] = props.deviceName;
    result["config"]["driver_version"] = props.driverVersion;

    std::vector <double> times_ms (gpu_times_ns.size ());
    std::transform (
        gpu_times_ns.begin (),
        gpu_times_ns.end (),
        times_ms.begin (),
        [] (double ns) { return ns * 1e-6; }
    );

    const double sum = std::accumulate (times_ms.begin (), times_ms.end (), 0.0);
    const double mean = sum / static_cast <double> (times_ms.size ());

    std::vector <double> sorted = times_ms;
    std::sort (sorted.begin (), sorted.end ());
    const size_t n = sorted.size ();

    const size_t median_idx = n / 2;
    const size_t p95_idx = std::min (static_cast <size_t> (n * 0.95), n - 1);
    const size_t p99_idx = std::min (static_cast <size_t> (n * 0.99), n - 1);

    const double median = sorted[median_idx];
    const double p95 = sorted[p95_idx];
    const double p99 = sorted[p99_idx];

    const double sq_sum = std::inner_product (
        times_ms.begin (),
        times_ms.end (),
        times_ms.begin (),
        0.0
    );

    const double variance = sq_sum / static_cast <double> (n) - mean * mean;
    const double std_dev = (variance > 0.0) ? std::sqrt (variance) : 0.0;

    result["statistics"] = {
        {"count", times_ms.size ()},
        {"mean_ms", mean},
        {"median_ms", median},
        {"min_ms", sorted.front ()},
        {"max_ms", sorted.back ()},
        {"p95_ms", p95},
        {"p99_ms", p99},
        {"std_dev_ms", std_dev}
    };

    result["frames_ms"] = times_ms;

    std::ofstream out (config.output_path.string ());
    out << result.dump (2);

    LOG_INFO ("[Benchmark] Results written to {}", config.output_path.string ());
}

} // namespace sdf_raster
