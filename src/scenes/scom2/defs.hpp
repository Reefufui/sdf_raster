#pragma once

namespace sdf_raster {
namespace scom2 {

// TODO: enum
inline static constexpr unsigned SCOM2_CHILD_EMPTY        = 0;
inline static constexpr unsigned SCOM2_CHILD_LEAF_VOLUME  = 1;
inline static constexpr unsigned SCOM2_CHILD_LEAF_SURFACE = 3;
inline static constexpr unsigned SCOM2_CHILD_NODE         = 2;

inline static constexpr unsigned SCOM2_CHILD_TYPE_BITS    = 2;
inline static constexpr unsigned SCOM2_CHILD_TYPE_MASK    = (1 << SCOM2_CHILD_TYPE_BITS) - 1;

inline static constexpr unsigned SCOM2_MAGIC_NUMBER = 0xffffdefa;
inline static constexpr unsigned SCOM2_VERSION = 4;

} // scom2
} // sdf_raster

