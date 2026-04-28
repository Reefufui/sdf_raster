// scenes/base/scene.cpp
#include "scenes/base/scene.hpp"

#include <cassert>

namespace sdf_raster {

DrawMethod Scene::get_current_draw_method () const {
    return this->get_available_draw_methods () [this->current_draw_method_index];
}

void Scene::set_current_draw_method (DrawMethod method) {
    auto methods = this->get_available_draw_methods ();
    for (size_t i = 0; i < methods.size (); i++) {
        if (methods [i] == method) {
            this->current_draw_method_index = i;
            this->state.draw_method = method;
            return;
        }
    }
    assert (false && "method not found in available_draw_methods");
}

void Scene::apply_state (const SceneState& scene_state) {
    this->state = scene_state;
    auto methods = this->get_available_draw_methods ();
    for (size_t i = 0; i < methods.size (); i++) {
        if (methods [i] == scene_state.draw_method) {
            this->current_draw_method_index = i;
            break;
        }
    }
}

} // sdf_raster
