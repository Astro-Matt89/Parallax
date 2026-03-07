#version 450

/// @file ui_rect.vert
/// @brief UI rectangle vertex shader — filled quads for panel backgrounds and borders.
///
/// Per-vertex: position (NDC), color (RGBA).
/// Triangle list topology — 6 vertices per quad (2 triangles).
/// SPRINT 05 Task 5.1

layout(location = 0) in vec2 in_position;   // Screen NDC [-1, 1]
layout(location = 1) in vec4 in_color;       // RGBA

layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = vec4(in_position, 0.0, 1.0);
    v_color = in_color;
}