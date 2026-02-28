#version 450

// -----------------------------------------------------------------
// Starfield vertex shader
//
// Reads star data from a storage buffer (one vec4 per star).
// Computes point size from brightness, converts B-V to RGB color.
// -----------------------------------------------------------------

layout(set = 0, binding = 0) readonly buffer StarBuffer {
    vec4 stars[];  // xy = screen position, z = brightness, w = B-V color
};

layout(push_constant) uniform PushConstants {
    float point_size_scale;
    float brightness_scale;
};

layout(location = 0) out float v_brightness;
layout(location = 1) out vec3 v_color;

// -----------------------------------------------------------------
// B-V color index → RGB color
//
// Uses a piecewise polynomial fit to the Planckian locus.
// Input: B-V in [-0.4, 2.0]
// Output: RGB color in [0, 1]^3
//
// Reference: Mitchell Charity's "What color are the stars?"
// http://www.vendian.org/mncharity/dir3/starcolor/
// -----------------------------------------------------------------
vec3 bv_to_rgb(float bv)
{
    bv = clamp(bv, -0.4, 2.0);

    float r, g, b;

    // Red channel
    if (bv < 0.0)
    {
        // Hot blue-white stars: slight reduction from pure white
        r = 0.83 + 0.17 * (bv + 0.4) / 0.4;
    }
    else if (bv < 0.4)
    {
        r = 1.0;
    }
    else
    {
        r = 1.0;
    }

    // Green channel
    if (bv < 0.0)
    {
        g = 0.87 + 0.13 * (bv + 0.4) / 0.4;
    }
    else if (bv < 0.4)
    {
        g = 1.0 - 0.2 * bv / 0.4;
    }
    else if (bv < 1.5)
    {
        g = 0.8 - 0.55 * (bv - 0.4) / 1.1;
    }
    else
    {
        g = 0.25 - 0.15 * (bv - 1.5) / 0.5;
        g = max(g, 0.1);
    }

    // Blue channel
    if (bv < -0.2)
    {
        b = 1.0;
    }
    else if (bv < 0.4)
    {
        b = 1.0 - 0.6 * (bv + 0.2) / 0.6;
    }
    else if (bv < 1.0)
    {
        b = 0.4 - 0.35 * (bv - 0.4) / 0.6;
    }
    else
    {
        b = 0.05;
    }

    return vec3(clamp(r, 0.0, 1.0),
                clamp(g, 0.0, 1.0),
                clamp(b, 0.0, 1.0));
}

void main()
{
    vec4 star = stars[gl_InstanceIndex];

    // Screen position (already in NDC [-1, 1])
    gl_Position = vec4(star.xy, 0.0, 1.0);

    // Brightness with configurable scaling
    float brightness = star.z * brightness_scale;

    // Point size: sqrt scaling gives perceptually correct brightness-to-area
    // Brighter stars get bigger points
    float size = max(1.0, point_size_scale * sqrt(brightness));
    gl_PointSize = min(size, 10.0);

    // Output to fragment shader
    v_brightness = clamp(brightness, 0.0, 1.0);
    v_color = bv_to_rgb(star.w);
}