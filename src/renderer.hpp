#pragma once

#include <memory>
#include <string>

#include "GLFW/glfw3.h"

namespace sdf_raster {

class Camera;
class Mesh;
class SdfGrid;

class IRenderer {
public:
    virtual ~IRenderer () = default;
    virtual void init (int a_width, int a_height) = 0;
    virtual void render (const Camera& a_camera) = 0;
    virtual void resize (int a_width, int a_height) = 0;
    virtual void shutdown () = 0;
};

}

