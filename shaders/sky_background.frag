#version 450

// -----------------------------------------------------------------
// Sky background fragment shader — procedural night sky gradient
//
// Computes per-pixel sky color based on altitude above the horizon.
// Uses camera pointing and FOV from a uniform buffer to determine
// each fragment's altitude angle.
//
// Model:
//   - Zenith color: Bortle-dependent deep blue-black
//   - Horizon color: warmer (sodium light pollution spectrum)
//   - Gradient: airmass-based blending for physical correctness
//   - Below-horizon: smooth fade to black
//
// Sprint 03: night sky + light pollution only.
// Twilight rendering deferred to Sprint 04.
// -----------------------------------------------------------------

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 frag_color;

layout(set = 0, binding = 0) uniform SkyUBO
{
    float camera_alt_rad;     // Camera altitude in radians
    float camera_az_rad;      // Camera azimuth in radians
    float fov_rad;            // Vertical field of view in radians
    float aspect_ratio;       // Viewport width / height
    float bortle_scale;       // Bortle dark-sky scale (1–9)
    float sun_altitude_deg;   // Sun altitude in degrees (< -18 = night)
    float padding0;
    float padding1;
};

// -----------------------------------------------------------------
// Constants
// -----------------------------------------------------------------
const float PI      = 3.14159265358979;
const float HALF_PI = PI * 0.5;

// -----------------------------------------------------------------
// Compute the altitude angle (radians) for this fragment.
//
// Maps screen UV to a vertical angular offset from camera center,
// then adds to the camera altitude. Vulkan screen Y increases
// downward, so a fragment above center (lower UV.y) means higher
// altitude.
// -----------------------------------------------------------------
float compute_altitude(vec2 uv)
{
    float centered_y = uv.y - 0.5;

    // Vertical angular offset from center of screen
    float alt_offset = -centered_y * fov_rad;

    // Fragment altitude = camera altitude + vertical offset
    float altitude = camera_alt_rad + alt_offset;

    // Clamp to slightly below horizon to allow smooth transition
    return clamp(altitude, -0.15, HALF_PI);
}

// -----------------------------------------------------------------
// Airmass approximation (Rozenberg formula, simplified for GPU)
//
// X ≈ 1 / (cos(z) + 0.025 × exp(-11 × cos(z)))
// where z = zenith angle = π/2 - altitude
// -----------------------------------------------------------------
float airmass(float alt_rad)
{
    float z = HALF_PI - max(alt_rad, 0.001);
    float cos_z = cos(z);
    return 1.0 / (cos_z + 0.025 * exp(-11.0 * cos_z));
}

// -----------------------------------------------------------------
// Sky color computation
// -----------------------------------------------------------------
void main()
{
    float altitude = compute_altitude(v_uv);

    // -----------------------------------------------------------------
    // Bortle normalization [0, 1]
    // -----------------------------------------------------------------
    float bortle_norm = (bortle_scale - 1.0) / 8.0;

    // -----------------------------------------------------------------
    // Zenith base color: Bortle-dependent
    //   Bortle 1: (0.01, 0.01, 0.03) — pristine dark sky
    //   Bortle 9: (0.05, 0.04, 0.06) — inner city
    // -----------------------------------------------------------------
    vec3 zenith_color = mix(
        vec3(0.01, 0.01, 0.03),
        vec3(0.05, 0.04, 0.06),
        bortle_norm
    );

    // -----------------------------------------------------------------
    // Horizon color: warmer due to light pollution (sodium spectrum)
    //   Bortle 1: faint natural airglow
    //   Bortle 9: prominent orange glow
    // -----------------------------------------------------------------
    vec3 horizon_color = mix(
        vec3(0.015, 0.012, 0.020),
        vec3(0.12,  0.08,  0.04),
        bortle_norm
    );

    // -----------------------------------------------------------------
    // Gradient blending via airmass
    //
    // Light pollution contribution scales with:
    //   1. Bortle scale (how much LP there is)
    //   2. Airmass (longer path through LP layer near horizon)
    //
    // LP factor: fraction of sky brightness added per unit airmass
    // -----------------------------------------------------------------
    float X = airmass(altitude);
    float lp_factor = mix(0.02, 0.35, bortle_norm);
    float atm_brightness = 1.0 + lp_factor * (X - 1.0);

    // -----------------------------------------------------------------
    // Color shift: smoothstep on altitude for zenith→horizon blend
    // -----------------------------------------------------------------
    float alt_deg = altitude * (180.0 / PI);
    float horizon_blend = 1.0 - smoothstep(0.0, 45.0, alt_deg);

    vec3 sky = mix(zenith_color, horizon_color, horizon_blend);
    sky *= atm_brightness;

    // -----------------------------------------------------------------
    // Below horizon: smooth fade to near-black
    // Transition across -5° to 0° (approximately -0.087 radians)
    // -----------------------------------------------------------------
    if (altitude < 0.0)
    {
        float below_factor = smoothstep(-0.087, 0.0, altitude);
        sky *= below_factor;
    }

    frag_color = vec4(sky, 1.0);
}