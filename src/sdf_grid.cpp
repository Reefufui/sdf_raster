#include <algorithm>
#include <fstream>

#include "sdf_grid.hpp"

SdfGrid::SdfGrid(const std::string& path) {
    std::ifstream fs(path, std::ios::binary);
    fs.read((char *)&this->size, 3 * sizeof(unsigned));
    this->data.resize(this->size.x * this->size.y * this->size.z);
    size_t read_size = this->size.x * this->size.y * this->size.z * sizeof(float);
    fs.read((char *)this->data.data(), read_size);
    fs.close();
}

SdfGrid::SdfGrid(LiteMath::uint3 size, std::vector<float>&& data)
    : size(std::move(size)), data(std::move(data)) {}

void SdfGrid::set_data(LiteMath::uint3 size, std::vector<float>&& data) {
    this->size = std::move(size);
    this->data = std::move(data);
}

void SdfGrid::clear() {
    this->size = LiteMath::uint3{};
    this->data.clear();
}

void SdfGrid::save_sdf_grid(const std::string &path) {
    std::ofstream fs(path, std::ios::binary);
    fs.write((const char *)&this->size, 3 * sizeof(unsigned));
    size_t write_size = this->size.x * this->size.y * this->size.z * sizeof(float);
    fs.write((const char *)this->data.data(), write_size);
    fs.flush();
    fs.close();
}

float SdfGrid::sample(const LiteMath::float3& world_pos) const {
    LiteMath::float3 grid_range = this->sdf_grid_max - this->sdf_grid_min;
    LiteMath::float3 normalized_pos = (world_pos - this->sdf_grid_min) / grid_range;

    LiteMath::float3 scaled_pos = LiteMath::float3(
        normalized_pos.x * (this->size.x - 1),
        normalized_pos.y * (this->size.y - 1),
        normalized_pos.z * (this->size.z - 1)
    );

    int x0 = static_cast<int>(std::floor(scaled_pos.x));
    int y0 = static_cast<int>(std::floor(scaled_pos.y));
    int z0 = static_cast<int>(std::floor(scaled_pos.z));

    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int z1 = z0 + 1;

    x0 = std::clamp(x0, 0, static_cast<int>(this->size.x) - 1);
    y0 = std::clamp(y0, 0, static_cast<int>(this->size.y) - 1);
    z0 = std::clamp(z0, 0, static_cast<int>(this->size.z) - 1);
    x1 = std::clamp(x1, 0, static_cast<int>(this->size.x) - 1);
    y1 = std::clamp(y1, 0, static_cast<int>(this->size.y) - 1);
    z1 = std::clamp(z1, 0, static_cast<int>(this->size.z) - 1);

    float u = scaled_pos.x - x0;
    float v = scaled_pos.y - y0;
    float w = scaled_pos.z - z0;

    float v000 = this->get_distance(x0, y0, z0);
    float v100 = this->get_distance(x1, y0, z0);
    float v010 = this->get_distance(x0, y1, z0);
    float v110 = this->get_distance(x1, y1, z0);
    float v001 = this->get_distance(x0, y0, z1);
    float v101 = this->get_distance(x1, y0, z1);
    float v011 = this->get_distance(x0, y1, z1);
    float v111 = this->get_distance(x1, y1, z1);

    float c00 = v000 * (1 - u) + v100 * u;
    float c10 = v010 * (1 - u) + v110 * u;
    float c01 = v001 * (1 - u) + v101 * u;
    float c11 = v011 * (1 - u) + v111 * u;

    float c0 = c00 * (1 - v) + c10 * v;
    float c1 = c01 * (1 - v) + c11 * v;

    float interpolated_sdf = c0 * (1 - w) + c1 * w;

    return interpolated_sdf;
}


