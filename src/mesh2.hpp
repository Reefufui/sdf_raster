//
// Created by Daniel on 2019-07-10.
//

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <iostream>
#include <ostream>
#include <unordered_map>
#include <vector>

using Point = LiteMath::float3;

// a simple hash function for 3D vector. Reference:
// https://dmauro.com/post/77011214305/a-hashing-function-for-x-y-z-coordinates
double pointHash(float x, float y, float z){
    x = x >=0 ? 2 * x : -2 * x - 1;
    y = y >=0 ? 2 * y : -2 * y - 1;
    z = z >=0 ? 2 * z : -2 * z - 1;
    auto max = std::max({x,y,z});
    double hash = pow(max,3) + (2 * max * z) + z;
    if(max == z)
        hash += pow(std::max(x,y), 2);
    if(y >= x)
        hash += x+y;
    else
        hash += y;
    return hash;
}

//overload the hash function for unordered_map
namespace std {

template <>
struct hash<Point>
{
    std::size_t operator()(const Point& p) const {
        using std::size_t;
        using std::hash;

        return pointHash(p.x, p.y, p.z);

    }
};
}

struct Face{
    Face(Point a, Point b, Point c){
        vertex_[0] = a;
        vertex_[1] = b;
        vertex_[2] = c;
    }
    Point vertex_[3];
};


class Mesh{
public:
    int addPoint(const Point& point, int& index, int res){
        // simple hash function for a 3-D point
        //double hash = pointHash(point.x, point.y, point.z);//point.x * res * res  + point.y * res  + point.z ;

        // if the hashmap doesn't contain this point, insert
        // into the map, increment the index, and return the original
        if(!index_.count(point)) {
            index_.insert({point, index++});
            point_.push_back(point);
            return index-1;
        }

        // if the hashmap contains this point, return the index
        return index_[point];
    }

    // directly add the point without checking the container
    int addPoint(const Point& point, int& index){

        point_.push_back(point);

        return index++;

    }

    void addFace(int i, int j, int k){
        std::array<int,3> tmp({i,j,k});
        faces_.push_back(tmp);
    }

    int getSize() const{ return point_.size();}


    //output the mesh as OFF format
    friend std::ostream& operator<<(std::ostream& out, const Mesh& mesh){
        if(mesh.faces_.empty()){
            return out;
        }

        out << "OFF \n" << mesh.point_.size() << ' ' << mesh.faces_.size() << " 0\n";

        //output the vertices
        for(auto& point: mesh.point_){
            out << point.x << ' ' << point.y << ' ' << point.z << '\n';
        }

        //output the face indices
        for(auto& face: mesh.faces_){
            out << "3 " << face[0] << ' '<< face[1] <<' '<< face[2] <<'\n';
        }

        return out;
    }

private:
    std::vector<Point> point_; // stores the vertices
    std::vector<std::array<int,3>> faces_; // stores the faces
    std::unordered_map<Point,int> index_; // maps a point to its index
};

