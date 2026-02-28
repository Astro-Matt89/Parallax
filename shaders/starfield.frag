#version 450

// -----------------------------------------------------------------
// Starfield fragment shader
//
// Renders circular star points with soft-edge falloff.
// Discards fragments outside the unit circle for round stars.
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

    // Soft edge falloff: quadratic fade from center to edge
    float alpha = v_brightness * (1.0 - dist_sq * dist_sq);

    frag_color = vec4(v_color * alpha, alpha);
}