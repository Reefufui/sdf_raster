#pragma once

#include "scenes/scomtree/scomtree.hpp"

#include <filesystem>

namespace sdf_raster {

void traverse_scomtree (const SComTreeTree& scomtree);
void traverse_scomtree (const SComTreeTree& scomtree, const std::filesystem::path& log_file);

} // sdf_raster

