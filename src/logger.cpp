#include "logger.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace sdf_raster {

static std::chrono::time_point <std::chrono::high_resolution_clock> g_app_start_time;

void log_app_start () {
    g_app_start_time = std::chrono::high_resolution_clock::now ();

    spdlog::init_thread_pool (8192, 1);

    auto console_sink = std::make_shared <spdlog::sinks::stdout_color_sink_mt> ();
    console_sink->set_pattern ("[%n] [%^%l%$] %v");

    auto file_sink = std::make_shared <spdlog::sinks::rotating_file_sink_mt> ("logs/render_log.txt", 1024 * 1024 * 10, 3);
    file_sink->set_pattern ("[%n] [%l] %v");

    std::vector <spdlog::sink_ptr> sinks {console_sink, file_sink};

    auto main_logger = std::make_shared <spdlog::async_logger> (APP_NAME, sinks.begin (), sinks.end (),
        spdlog::thread_pool (), spdlog::async_overflow_policy::block);

    std::vector <spdlog::sink_ptr> validation_sinks {
        std::make_shared <spdlog::sinks::stdout_color_sink_mt> (),
        std::make_shared <spdlog::sinks::rotating_file_sink_mt> ("logs/vulkan_validation_log.txt", 1024 * 1024 * 10, 3)
    };

    validation_sinks [0]->set_pattern ("[%n] [%^%l%$] %v");
    validation_sinks [1]->set_pattern ("[%n] [%l] %v");

    auto validation_logger = std::make_shared <spdlog::async_logger> ("VK_LAYER_KHRONOS", validation_sinks.begin (), validation_sinks.end (),
        spdlog::thread_pool (), spdlog::async_overflow_policy::block);

#ifdef DEFAULT_SPDLOG_LEVEL
    auto default_level = static_cast <spdlog::level::level_enum> (DEFAULT_SPDLOG_LEVEL);

    main_logger->set_level (default_level);
    console_sink->set_level (default_level);
    file_sink->set_level (default_level);

    validation_logger->set_level (default_level);
    validation_sinks[0]->set_level (default_level);
    validation_sinks[1]->set_level (default_level);
#endif

    spdlog::set_default_logger (main_logger);
    spdlog::register_logger (validation_logger);

    spdlog::info ("'{} v{}.{}.{}' starting. Log level: {}", APP_NAME
        , APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH, spdlog::level::to_string_view (spdlog::get_level ()));
}

void log_app_exit (int exit_code) {
    auto end_time = std::chrono::high_resolution_clock::now ();
    std::chrono::duration <double> duration = end_time - g_app_start_time;

    if (exit_code) {
        spdlog::error ("'{} v{}.{}.{}' finished with errors in {:.2f} seconds. See logs above for details. Exiting with code {}."
            , APP_NAME, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH, duration.count (), exit_code);
    } else {
        spdlog::info ("'{} v{}.{}.{}' finished successfully in {:.2f} seconds. Exiting with code 0."
            , APP_NAME, APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH, duration.count ());
    }

    spdlog::shutdown ();
}

}

