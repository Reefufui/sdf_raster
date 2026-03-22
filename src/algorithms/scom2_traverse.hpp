#pragma once

#include "scenes/scom2/scom2.hpp"

#include <filesystem>

namespace sdf_raster {

void traverse_scom2 (const SCom2Tree& scom2);
void traverse_scom2 (const SCom2Tree& scom2, const std::filesystem::path& log_file);

} // sdf_raster

