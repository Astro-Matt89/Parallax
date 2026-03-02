#version 450

// -----------------------------------------------------------------
// Starfield vertex shader — Skychart mode
//
// Clean schematic rendering. Magnitude is linearly mapped to size
// and brightness. No atmospheric effects.
//
// push constants:
//   point_size_scale  = base size for brightest star (6.0)
//   mag_limit         = current magnitude limit slider value
//   brightest_mag     = brightest star in buffer
//   padding           = unused
// -----------------------------------------------------------------

layout(set = 0, binding = 0) readonly buffer StarBuffer {
    vec4 stars[];  // xy = screen position, z = mag_v, w = B-V color
};

layout(push_constant) uniform PushConstants {
    float point_size_scale;
    float mag_limit;
    float brightest_mag;
    float padding;
};

layout(location = 0) out float v_brightness;
layout(location = 1) out vec3 v_color;

// -----------------------------------------------------------------
// B-V color index → RGB (real catalog data, no reddening)
// -----------------------------------------------------------------
vec3 bv_to_rgb(float bv)
{
    bv = clamp(bv, -0.4, 2.0);
    float r, g, b;

    if (bv < 0.0)      { r = 0.83 + 0.17 * (bv + 0.4) / 0.4; }
    else                { r = 1.0; }

    if (bv < 0.0)      { g = 0.87 + 0.13 * (bv + 0.4) / 0.4; }
    else if (bv < 0.4)  { g = 1.0 - 0.2 * bv / 0.4; }
    else if (bv < 1.5)  { g = 0.8 - 0.55 * (bv - 0.4) / 1.1; }
    else                { g = max(0.25 - 0.15 * (bv - 1.5) / 0.5, 0.1); }

    if (bv < -0.2)      { b = 1.0; }
    else if (bv < 0.4)  { b = 1.0 - 0.6 * (bv + 0.2) / 0.6; }
    else if (bv < 1.0)  { b = 0.4 - 0.35 * (bv - 0.4) / 0.6; }
    else                { b = 0.05; }

    return vec3(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0));
}

void main()
{
    vec4 star = stars[gl_InstanceIndex];
    gl_Position = vec4(star.xy, 0.0, 1.0);

    float mag = star.z;

    // -----------------------------------------------------------------
    // Linear magnitude → normalized [0, 1]
    //
    // mag_min = brightest star in buffer (e.g. -0.05)
    // mag_limit = user slider (e.g. 6.5)
    // range = mag_limit - mag_min
    //
    // norm = (mag_limit - mag) / range
    //   brightest → 1.0, faintest → 0.0
    // -----------------------------------------------------------------
    float mag_range = max(mag_limit - brightest_mag, 1.0);
    float norm = clamp((mag_limit - mag) / mag_range, 0.0, 1.0);

    // -----------------------------------------------------------------
    // Point size: linear mapping
    //   size = max(1.0, base_size × norm)
    //
    //   Sirius (-1.46), MLIM 6.5: norm ≈ 1.0 → 6px
    //   Mag 3, MLIM 6.5:          norm ≈ 0.44 → 2.6px → 3px
    //   Mag 6, MLIM 6.5:          norm ≈ 0.06 → 0.4px → 1px
    // -----------------------------------------------------------------
    gl_PointSize = max(1.0, point_size_scale * norm);

    // -----------------------------------------------------------------
    // Brightness: linear ramp with floor at 0.15
    //   brightness = max(0.15, 1.0 - 0.8 × (1.0 - norm))
    //            = max(0.15, 0.2 + 0.8 × norm)
    //
    //   Sirius:  0.2 + 0.8×1.0   = 1.0
    //   Mag 3:   0.2 + 0.8×0.44  = 0.55
    //   Mag 5:   0.2 + 0.8×0.19  = 0.35
    //   Mag 6:   0.2 + 0.8×0.06  = 0.25
    //   Mag 6.5: 0.2 + 0.8×0.0   = 0.20 → floor 0.20
    //
    //   Every star clearly visible! Faintest at 15-20% brightness.
    // -----------------------------------------------------------------
    v_brightness = max(0.15, 0.2 + 0.8 * norm);

    v_color = bv_to_rgb(star.w);
}