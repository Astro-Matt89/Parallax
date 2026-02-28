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
// B-V color index → approximate RGB via blackbody temperature
//
// Step 1: B-V → effective temperature (Ballesteros 2012 approximation)
// Step 2: Temperature → RGB (simplified Planckian locus)
// -----------------------------------------------------------------
vec3 bv_to_rgb(float bv)
{
    // Clamp B-V to valid range
    bv = clamp(bv, -0.4, 2.0);

    // B-V → effective temperature (Kelvin)
    float t = 4600.0 / (1.7 * bv + 1.8) + 4600.0 / (0.92 * bv + 1.7);
    t = clamp(t, 1000.0, 40000.0);

    // Temperature → RGB (simplified blackbody approximation)
    float x = t / 1000.0;
    float r, g, b;

    if (t < 6600.0)
    {
        r = 1.0;
        g = clamp(0.39 * log(x) - 0.634, 0.0, 1.0);
        if (x > 1.0)
        {
            b = clamp(0.543 * log(x - 1.0) - 1.185, 0.0, 1.0);
        }
        else
        {
            b = 0.0;
        }
    }
    else
    {
        r = clamp(1.269 * pow(x - 6.0, -0.1332), 0.0, 1.0);
        g = clamp(1.144 * pow(x - 6.0, -0.0755), 0.0, 1.0);
        b = 1.0;
    }

    return vec3(r, g, b);
}

void main()
{
    vec4 star = stars[gl_InstanceIndex];

    // Screen position (already in NDC [-1, 1])
    gl_Position = vec4(star.xy, 0.0, 1.0);

    // Brightness with configurable scaling
    float brightness = star.z * brightness_scale;

    // Point size: sqrt scaling gives perceptually correct brightness-to-area
    float size = max(1.0, point_size_scale * sqrt(brightness));
    gl_PointSize = min(size, 8.0);

    // Output to fragment shader
    v_brightness = clamp(brightness, 0.0, 1.0);
    v_color = bv_to_rgb(star.w);
}