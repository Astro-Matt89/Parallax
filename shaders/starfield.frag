#version 450

// -----------------------------------------------------------------
// Starfield fragment shader
//
// Renders circular star points with soft-edge falloff.
// Discards fragments outside the unit circle for round stars.
//
// For 1px points (gl_PointSize = 1.0), gl_PointCoord is always
// (0.5, 0.5), so dist_sq = 0 and alpha = v_brightness.
// This ensures faint stars are a single bright pixel.
// -----------------------------------------------------------------

layout(location = 0) in float v_brightness;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 frag_color;

void main()
{
    // gl_PointCoord: [0,1] over the point quad → remap to [-1,1]
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist_sq = dot(coord, coord);

    // Discard corners → circular point shape
    if (dist_sq > 1.0)
    {
        discard;
    }

    // Soft edge falloff: smoother quadratic fade
    // For bright multi-pixel stars: nice circular glow
    // For 1px stars (dist_sq ≈ 0): full brightness preserved
    float falloff = 1.0 - dist_sq;
    float alpha = v_brightness * falloff;

    // Ensure a minimum alpha so faint 1px stars are still visible
    // against the dark sky background (additive blending)
    alpha = max(alpha, v_brightness * 0.5);

    frag_color = vec4(v_color * alpha, alpha);
}