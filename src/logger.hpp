#pragma once

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE 
#include <spdlog/spdlog.h>

#include "shaders/common.h" // NodeContext

namespace sdf_raster {

#define LOG_TRACE(...)    ::spdlog::trace(__VA_ARGS__)
#define LOG_INFO(...)     ::spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)     ::spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::spdlog::critical(__VA_ARGS__)

void init_logging ();
void shutdown_logging ();

}

