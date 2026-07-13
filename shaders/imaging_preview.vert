#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;

layout(push_constant) uniform PushConstants
{
    float viewport_w;
    float viewport_h;
};

layout(location = 0) out vec2 v_uv;

void main()
{
    float ndc_x = (in_position.x / viewport_w) * 2.0 - 1.0;
    float ndc_y = (in_position.y / viewport_h) * 2.0 - 1.0;

    gl_Position = vec4(ndc_x, ndc_y, 0.0, 1.0);
    v_uv = in_uv;
}
