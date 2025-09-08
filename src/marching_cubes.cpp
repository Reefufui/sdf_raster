#include <algorithm>
#include <cmath>
#include <random>

#include "marching_cubes.hpp"

namespace sdf_raster {

void MarchingCubes::constructGrid(const SdfGrid& a_sdf_grid) {
    const auto size = a_sdf_grid.get_size ();

    for (size_t z = 0; z < size.z; ++z) {
        for (size_t y = 0; y < size.y; ++y) {
            for (size_t x = 0; x < size.x; ++x) {
                const auto distance = a_sdf_grid.get_distance(x, y, z);
                if (distance <= 0.f) {
                    vertices_(x, y, z, 1);
                }
            }
        }
    }

    const float max = a_sdf_grid.get_sdf_grid_max().x;
    const float min = a_sdf_grid.get_sdf_grid_min().x;
    size_ = (max - min) / res_;
    offset_ = min;
}

void MarchingCubes::constructGrid(const std::vector<LiteMath::float3>& points)
{
    int isoScale = res_/100 * isoLevel_;

    // align the point to the grid starting from (0,0,0) with the resolution
    for(auto point: points){
        float x = (point.x - offset_) / size_;
        float y = (point.y - offset_) / size_;
        float z = (point.z - offset_) / size_;
        //std::cout << x <<' '<< y <<' ' << z<< '\n';

        //find the neighbor gird voxel of the point. For each vertex
        //the value varies (0,1), equals 1 when it's just the vertex
        for(int i = floor(x)-isoScale; i<= ceil(x)+isoScale; ++i){
            for(int j = floor(y)-isoScale ; j<= ceil(y)+isoScale; ++j){
                for(int k = floor(z)-isoScale; k<= ceil(z)+isoScale; ++k){
                    if(i<0 || j<0 || k<0 || i>res_ || j>res_ || k>res_)
                        continue;
                    float distance = LiteMath::length(LiteMath::float3(i,j,k) - LiteMath::float3(x,y,z));
                    //std::cout << distance <<'\n';

                    if(distance <= isoScale) {
                        vertices_(i, j, k, 1);

                    }

                }
            }
        }
    }

}

LiteMath::float3 MarchingCubes::VertexInterp(const Vertex &v1, const Vertex &v2) {
    if(abs(isoLevel_ - v1.getValue()) < 0.00001)
        return v1.getPoint();

    if(abs(isoLevel_ - v2.getValue()) < 0.00001)
        return v2.getPoint();

    if(abs(v1.getValue() - v2.getValue()) < 0.00001)
        return v1.getPoint();

    float tmp;
    LiteMath::float3 p;

    tmp = (isoLevel_ - v1.getValue())/(v2.getValue() - v1.getValue());
    p = v1.getPoint() + tmp * (v2.getPoint() - v1.getPoint());

    //the final point should be restored to the original scale
    //p = p * size_ + offset_;

    return p;
}

void MarchingCubes::generateMesh() {
    for (int i = 0; i < res_; ++i) {
        for (int j = 0; j < res_; ++j) {
            for (int k = 0; k < res_; ++k) {
                unsigned int cubeindex = 0;
                Vertex voxelPivot = vertices_.get(i,j,k);
                LiteMath::float3 vertList[12];

                //determine the index of edgeTable for each voxel
                for(unsigned int m=0; m<8; ++m){
                    if(vertices_(voxelPivot.getPoint(m)).getValue() < isoLevel_)
                        cubeindex |= (1 << m);
                }
                if(edgeTable[cubeindex] == 0)
                    continue;

                if(edgeTable[cubeindex] & 1)
                    vertList[0] = VertexInterp(voxelPivot, vertices_(voxelPivot.getPoint(1)));

                if(edgeTable[cubeindex] & 2)
                    vertList[1] = VertexInterp(vertices_(voxelPivot.getPoint(1)),
                            vertices_(voxelPivot.getPoint(2)));

                if(edgeTable[cubeindex] & 4)
                    vertList[2] = VertexInterp(vertices_(voxelPivot.getPoint(2)),
                            vertices_(voxelPivot.getPoint(3)));

                if(edgeTable[cubeindex] & 8)
                    vertList[3] = VertexInterp(vertices_(voxelPivot.getPoint(3)),voxelPivot);

                if(edgeTable[cubeindex] & 16)
                    vertList[4] = VertexInterp(vertices_(voxelPivot.getPoint(4)),
                            vertices_(voxelPivot.getPoint(5)));

                if(edgeTable[cubeindex] & 32)
                    vertList[5] = VertexInterp(vertices_(voxelPivot.getPoint(5)),
                            vertices_(voxelPivot.getPoint(6)));

                if(edgeTable[cubeindex] & 64)
                    vertList[6] = VertexInterp(vertices_(voxelPivot.getPoint(6)),
                            vertices_(voxelPivot.getPoint(7)));

                if(edgeTable[cubeindex] & 128)
                    vertList[7] = VertexInterp(vertices_(voxelPivot.getPoint(7)),
                            vertices_(voxelPivot.getPoint(4)));

                if(edgeTable[cubeindex] & 256)
                    vertList[8] = VertexInterp(vertices_(voxelPivot.getPoint(0)),
                            vertices_(voxelPivot.getPoint(4)));

                if(edgeTable[cubeindex] & 512)
                    vertList[9] = VertexInterp(vertices_(voxelPivot.getPoint(1)),
                            vertices_(voxelPivot.getPoint(5)));

                if(edgeTable[cubeindex] & 1024)
                    vertList[10] = VertexInterp(vertices_(voxelPivot.getPoint(2)),
                            vertices_(voxelPivot.getPoint(6)));

                if(edgeTable[cubeindex] & 2048)
                    vertList[11] = VertexInterp(vertices_(voxelPivot.getPoint(3)),
                            vertices_(voxelPivot.getPoint(7)));

                for (int n = 0; triTable[cubeindex][n] != -1; n += 3) {
                    auto a = sdf_raster::Vertex {
                        vertList[triTable[cubeindex][n]] * size_ + offset_
                            , LiteMath::float3 {1.f} // TODO: color
                        , LiteMath::float3 {1.f} // TODO: normal
                    };
                    auto b = sdf_raster::Vertex {
                        vertList[triTable[cubeindex][n + 1]] * size_ + offset_
                            , LiteMath::float3 {1.f} // TODO: color
                        , LiteMath::float3 {1.f} // TODO: normal
                    };
                    auto c = sdf_raster::Vertex {
                        vertList[triTable[cubeindex][n + 2]] * size_ + offset_
                            , LiteMath::float3 {1.f} // TODO: color
                        , LiteMath::float3 {1.f} // TODO: normal
                    };
                    this->mesh.add_triangle(a, b, c);
                }
            }
        }
    }
}

}

