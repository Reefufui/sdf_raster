// scenes/base/model.cpp
#include "scenes/base/model.hpp"

#include <cassert>

namespace sdf_raster {

DrawMethod Model::get_current_draw_method () const {
    return this->get_available_draw_methods () [this->current_draw_method_index];
}

void Model::set_current_draw_method (DrawMethod method) {
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

void Model::apply_state (const ModelState& model_state) {
    this->state = model_state;
    auto methods = this->get_available_draw_methods ();
    for (size_t i = 0; i < methods.size (); i++) {
        if (methods [i] == model_state.draw_method) {
            this->current_draw_method_index = i;
            break;
        }
    }
}

LiteMath::float4x4 Model::get_model_matrix () const {
    using namespace LiteMath;
    float4x4 m = LiteMath::translate4x4 (state.position);
    m = m * LiteMath::rotate4x4Y (state.rotation.y * M_PI / 180.0f);
    m = m * LiteMath::rotate4x4X (state.rotation.x * M_PI / 180.0f);
    m = m * LiteMath::rotate4x4Z (state.rotation.z * M_PI / 180.0f);
    m = m * LiteMath::scale4x4 (state.scale);
    return m;
}

} // sdf_raster
