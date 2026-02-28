#version 450

/// @file test_star.vert
/// @brief Test vertex shader — single point at screen center.

void main()
{
    // Hardcoded single point at normalized device coordinate center
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
    gl_PointSize = 3.0;
}