#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE 
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "logger.hpp"

namespace sdf_raster {

void init_logging () {
    spdlog::init_thread_pool (8192, 1);

    auto console_sink = std::make_shared <spdlog::sinks::stdout_color_sink_mt> ();
    console_sink->set_pattern ("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    auto file_sink = std::make_shared <spdlog::sinks::rotating_file_sink_mt> ("logs/render_log.txt", 1024 * 1024 * 10, 3);
    file_sink->set_pattern ("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

    std::vector <spdlog::sink_ptr> sinks {console_sink, file_sink};

    auto main_logger = std::make_shared <spdlog::async_logger> ("sdf_raster", sinks.begin (), sinks.end (),
        spdlog::thread_pool (), spdlog::async_overflow_policy::block);

#ifdef DEFAULT_SPDLOG_LEVEL
    auto default_level = static_cast <spdlog::level::level_enum> (DEFAULT_SPDLOG_LEVEL);
    main_logger->set_level (default_level);
    console_sink->set_level (default_level);
    file_sink->set_level (default_level);
#endif

    spdlog::set_default_logger (main_logger);

    spdlog::info ("spdlog initialized successfully. Default level: {}", spdlog::level::to_string_view (spdlog::get_level ()));
}

void shutdown_logging () {
    spdlog::shutdown ();
}

}

