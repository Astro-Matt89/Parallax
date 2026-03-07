#version 450

/// @file ui_rect.frag
/// @brief UI rectangle fragment shader — outputs vertex color with alpha.
///
/// SPRINT 05 Task 5.1

layout(location = 0) in vec4 v_color;

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = v_color;
}