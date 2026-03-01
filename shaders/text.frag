#version 450

// -----------------------------------------------------------------
// Text fragment shader
//
// Samples the R8 font atlas, uses it as alpha.
// Tints with the vertex color. Discards transparent pixels.
// -----------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D font_atlas;

layout(location = 0) in vec2 v_texcoord;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 out_color;

void main()
{
    // R8_UNORM: glyph alpha is in the red channel
    float alpha = texture(font_atlas, v_texcoord).r;

    // Hard cutoff for pixel-sharp text (no anti-aliasing)
    if (alpha < 0.5)
    {
        discard;
    }

    out_color = vec4(v_color, alpha);
}