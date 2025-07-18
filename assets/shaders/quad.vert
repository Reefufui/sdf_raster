#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec2 out_tex_coord;

void main() {
    // Генерируем полноэкранный квад с помощью gl_VertexIndex
    // Вершины в clip-space (-1..1) и соответствующие текстурные координаты (0..1)
    vec2 positions[6] = vec2[](
        vec2(-1.0f, -1.0f), // 0: bottom-left
        vec2( 1.0f, -1.0f), // 1: bottom-right
        vec2(-1.0f,  1.0f), // 2: top-left (Vulkan Y-down)

        vec2(-1.0f,  1.0f), // 3: top-left
        vec2( 1.0f, -1.0f), // 4: bottom-right
        vec2( 1.0f,  1.0f)  // 5: top-right
    );

    vec2 tex_coords[6] = vec2[](
        vec2(0.0f, 1.0f), // 0: bottom-left
        vec2(1.0f, 1.0f), // 1: bottom-right
        vec2(0.0f, 0.0f), // 2: top-left

        vec2(0.0f, 0.0f), // 3: top-left
        vec2(1.0f, 1.0f), // 4: bottom-right
        vec2(1.0f, 0.0f)  // 5: top-right
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0f, 1.0f);
    out_tex_coord = tex_coords[gl_VertexIndex];
}
