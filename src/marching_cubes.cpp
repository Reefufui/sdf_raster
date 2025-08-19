#include <algorithm>
#include <cmath>
#include <random>

#include "marching_cubes.hpp"

std::vector<Point> MarchingCubes::generateSphere(int radius, int number) {
    std::vector<Point> res;
    int iter = number;

    //use polar coordinate to generate the sphere
    for(int a=0; a <iter; ++a)
        for(int b=0; b<iter; ++b) {
            float alpha = (float)a/iter * 2. * 3.1415926;
            float beta = (float)b/iter * 2. * 3.1415926;
            float z = radius * sin(alpha) + radius;
            float y = radius * cos(alpha) * sin(beta) + radius;
            float x = radius * cos(alpha) * cos(beta) + radius;
            res.emplace_back(Point(x,y,z));
        }

    return res;
}


std::vector<Point> MarchingCubes::processPoints(std::istream &in)
{
    std::vector<Point> Points;
    float max = 0, min = 0; //record the largest value
    while(in)
    {

        float x,y,z;
        if(!(in >> x >> y >> z)){

            break;
            //throw std::runtime_error("point not valid");
        }
        auto max1 = std::max({x,y,z});
        auto min1 = std::min({x,y,z});

        max = max1 > max? max1 : max;
        min = min1 < min? min1: min;

        Points.emplace_back(Point(x,y,z));

    }

    //all points will be shifted by offset_ towards (0,0,0)
    //and then align to the grid of the specific resolution
    size_ = (max - min)/res_;
    offset_ = min;

    return Points;

}

void MarchingCubes::constructGrid(const std::vector<Point>& points)
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
                    float distance = glm::distance(Point(i,j,k),Point(x,y,z));
                    //std::cout << distance <<'\n';

                    if(distance <= isoScale) {
                        vertices_(i, j, k, 1);

                    }

                }
            }
        }
    }

}

Point MarchingCubes::VertexInterp(const Vertex &v1, const Vertex &v2) {
    if(abs(isoLevel_ - v1.getValue()) < 0.00001)
        return v1.getPoint();

    if(abs(isoLevel_ - v2.getValue()) < 0.00001)
        return v2.getPoint();

    if(abs(v1.getValue() - v2.getValue()) < 0.00001)
        return v1.getPoint();

    float tmp;
    Point p;

    tmp = (isoLevel_ - v1.getValue())/(v2.getValue() - v1.getValue());
    p = v1.getPoint() + tmp * (v2.getPoint() - v1.getPoint());

    //the final point should be restored to the original scale
    //p = p * size_ + offset_;

    return p;
}

void MarchingCubes::generateMesh() {
    int faceindex = 0;

    //iterator each voxel
    for(int i=0; i<res_; ++i)
        for(int j=0; j<res_; ++j)
            for(int k=0; k<res_; ++k){
                unsigned int cubeindex = 0;
                Vertex voxelPivot = vertices_.get(i,j,k);
                Point vertList[12];

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

                //get the triangle from the vertList
                int trinum = 0;
                for(int n=0; triTable[cubeindex][n] != -1; n+=3 ){

                    // add the three points in the mesh
                    int index1 = mesh_.addPoint(vertList[triTable[cubeindex][n]] * size_ + offset_, faceindex);
                    int index2 = mesh_.addPoint(vertList[triTable[cubeindex][n+1]]* size_ + offset_, faceindex);
                    int index3 = mesh_.addPoint(vertList[triTable[cubeindex][n+2]]* size_ + offset_, faceindex);

                    // add the triangle index in the mesh.
                    mesh_.addFace(index1, index2, index3);

                }

            }


}



