#pragma once

#include <memory>
#include <string>

#include "GLFW/glfw3.h"

class Camera;
class Mesh;

class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual void init(int a_width, int a_height, GLFWwindow* a_window) = 0;
    virtual void render(const Camera& a_camera, const Mesh& a_mesh) = 0;
    virtual void render(const Camera& camera, const Mesh& mesh, const std::string& a_filename) = 0;
    virtual void resize(int a_width, int a_height) = 0;
    virtual void shutdown() = 0;
};

