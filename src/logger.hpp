#pragma once

#include <spdlog/spdlog.h>

#include <spdlog/fmt/bundled/format.h>
#include <concepts>

namespace fmt {

template <typename T>
concept HasXY = requires (T a) { a.x; a.y; };

template <typename T>
concept HasXYZ = requires (T a) {
    a.x; a.y; a.z;
};

template <typename T>
concept HasXYZW = HasXYZ <T> && requires (T a) {
    a.w;
};

template <typename T>
requires HasXY <T> && (!HasXYZ <T>)
struct formatter <T> {
    constexpr auto parse (format_parse_context& ctx) { return ctx.begin (); }
    template <typename FormatContext>
    auto format (const T& p, FormatContext& ctx) const {
        return fmt::format_to (ctx. out (), "({}, {})", p.x, p.y);
    }
};

template<typename T>
requires HasXYZ <T> && (!HasXYZW <T>)
struct formatter <T> {
    constexpr auto parse (format_parse_context& ctx) { return ctx.begin (); }

    template <typename FormatContext>
    auto format (const T& p, FormatContext& ctx) const {
        return fmt::format_to (ctx.out (), "({}, {}, {})", p.x, p.y, p.z);
    }
};

template <typename T>
requires HasXYZW <T>
struct formatter <T> {
    constexpr auto parse (format_parse_context& ctx) { return ctx.begin (); }

    template <typename FormatContext>
    auto format (const T& p, FormatContext& ctx) const {
        return fmt::format_to (ctx.out (), "({}, {}, {}, {})", p.x, p.y, p.z, p.w);
    }
};

}

namespace sdf_raster {

#ifndef APP_NAME
#define APP_NAME "app"
#endif
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

