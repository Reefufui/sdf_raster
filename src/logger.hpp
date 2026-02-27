#pragma once

#include <spdlog/spdlog.h>

namespace sdf_raster {

#define APP_NAME "sdf_raster"
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 0
#define APP_VERSION_PATCH 0

#define LOG_TRACE(...)    ::spdlog::trace(__VA_ARGS__)
#define LOG_INFO(...)     ::spdlog::info(__VA_ARGS__)
#define LOG_WARN(...)     ::spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...)    ::spdlog::error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::spdlog::critical(__VA_ARGS__)

void log_app_start ();
void log_app_exit (int exit_code);

}

