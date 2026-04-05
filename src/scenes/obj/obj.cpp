#include "scenes/obj/obj.hpp"

#include "logger.hpp"

#include <LiteMath.h>
#include <tiny_obj_loader.h>

#include <unordered_map>
#include <limits>
#include <algorithm>

namespace sdf_raster {

bool operator== (const Vertex& a, const Vertex& b) {
    return memcmp (&a, &b, sizeof (Vertex)) == 0;
}

bool ObjScene::load (const std::filesystem::path& path) {
    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true; 
    tinyobj::ObjReader reader;
    
    if (!reader.ParseFromFile (path.string (), reader_config)) {
        LOG_ERROR ("[ObjScene] Failed to parse file: {}", path.string ());
        return false;
    }
    
    auto& attrib = reader.GetAttrib ();
    auto& shapes = reader.GetShapes ();
    
    this->data.vertices.clear ();
    this->data.indices.clear ();
    
    bool has_normals_in_file = !attrib.normals.empty ();

    struct IndexPack {
        int v, n, c;
        bool operator <(const IndexPack& other) const {
            return std::tie (v, n, c) < std::tie (other.v, other.n, other.c);
        }
    };

    std::map <IndexPack, uint32_t> unique_vertices;
    
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            int n_idx = has_normals_in_file ? index.normal_index : -1;
            IndexPack pack = {index.vertex_index, n_idx, index.vertex_index};
            
            if (unique_vertices.find (pack) == unique_vertices.end ()) {
                unique_vertices [pack] = static_cast <uint32_t> (this->data.vertices.size ());
                
                Vertex v {};
                v.position = {
                    attrib.vertices [3 * index.vertex_index + 0]
                    , attrib.vertices [3 * index.vertex_index + 1]
                    , attrib.vertices [3 * index.vertex_index + 2]
                    , 1.0f
                };
                
                if (has_normals_in_file && index.normal_index >= 0) {
                    v.normal = {
                        attrib.normals [3 * index.normal_index + 0]
                        , attrib.normals [3 * index.normal_index + 1]
                        , attrib.normals [3 * index.normal_index + 2]
                        , 0.0f
                    };
                } else {
                    v.normal = {0.0f, 0.0f, 0.0f, 0.0f};
                }
                
                if (!attrib.colors.empty ()) {
                    v.color = {
                        attrib.colors [3 * index.vertex_index + 0]
                        , attrib.colors [3 * index.vertex_index + 1]
                        , attrib.colors [3 * index.vertex_index + 2]
                        , 1.0f
                    };
                } else {
                    v.color = {1.0f, 1.0f, 1.0f, 1.0f};
                }
                this->data.vertices.push_back (v);
            }
            this->data.indices.push_back (unique_vertices [pack]);
        }
    }

    if (!has_normals_in_file) {
        LOG_INFO ("[ObjScene] No normals found in '{}'. Calculating from faces...", path.filename ().string ());
        
        for (size_t i = 0; i < this->data.indices.size (); i += 3) {
            uint32_t i0 = this->data.indices [i + 0];
            uint32_t i1 = this->data.indices [i + 1];
            uint32_t i2 = this->data.indices [i + 2];

            LiteMath::float3 p0 = {this->data.vertices [i0].position.x, this->data.vertices [i0].position.y, this->data.vertices [i0].position.z};
            LiteMath::float3 p1 = {this->data.vertices [i1].position.x, this->data.vertices [i1].position.y, this->data.vertices [i1].position.z};
            LiteMath::float3 p2 = {this->data.vertices [i2].position.x, this->data.vertices [i2].position.y, this->data.vertices [i2].position.z};

            LiteMath::float3 edge1 = p1 - p0;
            LiteMath::float3 edge2 = p2 - p0;
            LiteMath::float3 face_n = LiteMath::normalize (LiteMath::cross (edge1, edge2));

            this->data.vertices [i0].normal += LiteMath::to_float4 (face_n, 0.0f);
            this->data.vertices [i1].normal += LiteMath::to_float4 (face_n, 0.0f);
            this->data.vertices [i2].normal += LiteMath::to_float4 (face_n, 0.0f);
        }

        for (auto& v : this->data.vertices) {
            float len = LiteMath::length (LiteMath::to_float3 (v.normal));
            if (len > 1e-6f) {
                v.normal = v.normal / len;
            } else {
                v.normal = {0.0f, 1.0f, 0.0f, 0.0f};
            }
        }
    }

    if (!this->data.vertices.empty ()) {
        LiteMath::float3 v_min (std::numeric_limits <float>::max ());
        LiteMath::float3 v_max (std::numeric_limits <float>::lowest ());
        for (const auto& v : this->data.vertices) {
            v_min.x = std::min (v_min.x, v.position.x);
            v_min.y = std::min (v_min.y, v.position.y);
            v_min.z = std::min (v_min.z, v.position.z);
            v_max.x = std::max (v_max.x, v.position.x);
            v_max.y = std::max (v_max.y, v.position.y);
            v_max.z = std::max (v_max.z, v.position.z);
        }
        LiteMath::float3 center = (v_min + v_max) * 0.5f;
        LiteMath::float3 size = v_max - v_min;
        float max_side = std::max ({size.x, size.y, size.z});
        
        float factor = (max_side > 1e-7f) ? (2.0f / max_side) : 1.0f;
        for (auto& v : this->data.vertices) {
            v.position.x = (v.position.x - center.x) * factor;
            v.position.y = (v.position.y - center.y) * factor;
            v.position.z = (v.position.z - center.z) * factor;
        }
    }

    this->state = SceneState {
        .camera = Camera (),
        .draw_method = DrawMethod::Explicit,
        .name = path.stem ().string (),
        .path = path,
        .octree_depth = 0,
        .cpu_traversed = 0
    };
    
    return true;
}

SceneState& ObjScene::get_state () {
    return this->state;
}

void ObjScene::set_state (const SceneState& scene_state) {
    this->state = scene_state;
}

const ObjModel& ObjScene::get_model_data () const {
    return this->data;
}

ObjScene::~ObjScene () {
    this->data.vertices.clear ();
    this->data.indices.clear ();
}

} // namespace sdf_raster

