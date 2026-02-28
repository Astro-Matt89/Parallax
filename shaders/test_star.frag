#version 450

/// @file test_star.frag
/// @brief Test fragment shader — solid white output.

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(1.0, 1.0, 1.0, 1.0);
}