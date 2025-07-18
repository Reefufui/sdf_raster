#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform sampler2D cpu_framebuffer_texture;

layout(location = 0) in vec2 out_tex_coord; // Входные текстурные координаты из вершинного шейдера
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(cpu_framebuffer_texture, out_tex_coord);
}
