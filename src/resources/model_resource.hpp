// resources/model.hpp

#pragma once

#include <vk_include.h>

namespace sdf_raster {

class ModelResource {
public:
    virtual ~ModelResource () = default;
};

class ModelSubtreeResource {
public:
    virtual ~ModelSubtreeResource () = default;

    virtual VkDeviceSize get_subtree_count () const = 0;
    virtual VkBuffer get_subtree_root_staging_buffer (uint32_t frame_index) const = 0;
    virtual VkBuffer get_subtree_root_buffer (uint32_t frame_index) const = 0;
    virtual size_t get_element_size () const = 0;
};

} // sdf_raster
