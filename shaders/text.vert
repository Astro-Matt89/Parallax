#version 450

// -----------------------------------------------------------------
// Text vertex shader
//
// Converts pixel-space positions to NDC using viewport push constants.
// Passes through texcoords and vertex color.
// -----------------------------------------------------------------

layout(location = 0) in vec2 in_position;   // Screen pixels (top-left origin)
layout(location = 1) in vec2 in_texcoord;   // UV into font atlas
layout(location = 2) in vec3 in_color;       // RGB tint

layout(push_constant) uniform PushConstants {
    float viewport_w;
    float viewport_h;
};

layout(location = 0) out vec2 v_texcoord;
layout(location = 1) out vec3 v_color;

void main()
{
    // Convert pixel coords to NDC [-1, 1]
    // x: 0 → -1, viewport_w → +1
    // y: 0 → -1, viewport_h → +1  (Vulkan NDC: top = -1, bottom = +1)
    float ndc_x = (in_position.x / viewport_w) * 2.0 - 1.0;
    float ndc_y = (in_position.y / viewport_h) * 2.0 - 1.0;

    gl_Position = vec4(ndc_x, ndc_y, 0.0, 1.0);

    v_texcoord = in_texcoord;
    v_color = in_color;
}