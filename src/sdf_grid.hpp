#pragma once

#include <vector>

#include "LiteMath.h"

namespace sdf_raster {

class SdfGrid {
public:
    SdfGrid (const std::string& path);
    SdfGrid (LiteMath::uint3 size, std::vector<float>&& data);

    LiteMath::uint3 get_size () const { return this->size; }
    const std::vector<float>& get_data () const { return this->data; }
    float get_distance (int x, int y, int z) const { return this->data [
        x + y * this->size.x + z * this->size.x * this->size.y
        ]; }
    float sample (const LiteMath::float3& world_pos) const;

    void set_data (LiteMath::uint3 size, std::vector <float>&& data);
    void clear ();
    bool is_empty () const { return data.empty (); }

    void save_sdf_grid (const std::string &path);

    LiteMath::float3 get_sdf_grid_min () const { return this->sdf_grid_min; }
    LiteMath::float3 get_sdf_grid_max () const { return this->sdf_grid_max; }

private:
    const LiteMath::float3 sdf_grid_max = LiteMath::float3 (1.f, 1.f, 1.f);
    const LiteMath::float3 sdf_grid_min = LiteMath::float3 (-1.f, -1.f, -1.f);

    LiteMath::uint3 size;
    std::vector <float> data;
};

}

