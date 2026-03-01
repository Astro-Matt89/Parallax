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
//   - Triangular dithering: breaks 8-bit banding artifacts
//
// IMPORTANT: The swapchain uses B8G8R8A8_SRGB, which means the GPU
// applies sRGB gamma encoding to our output. We must output LINEAR
// values that, after sRGB encoding, look correct on screen.
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
// -----------------------------------------------------------------
float compute_altitude(vec2 uv)
{
    float centered_y = uv.y - 0.5;
    float alt_offset = -centered_y * fov_rad;
    float altitude = camera_alt_rad + alt_offset;
    return clamp(altitude, -0.15, HALF_PI);
}

// -----------------------------------------------------------------
// Airmass approximation (Rozenberg formula, simplified for GPU)
// -----------------------------------------------------------------
float airmass(float alt_rad)
{
    float z = HALF_PI - max(alt_rad, 0.001);
    float cos_z = cos(z);
    return 1.0 / (cos_z + 0.025 * exp(-11.0 * cos_z));
}

// -----------------------------------------------------------------
// Triangular-distribution dithering noise
//
// Generates a per-pixel noise value in [-0.5/255, +0.5/255] using
// a hash of gl_FragCoord. Triangular distribution (sum of two
// uniform samples - 1.0) concentrates noise near zero, reducing
// visible grain while still breaking banding perfectly.
//
// Reference: Gjøl & Wronski, "Banding in Games", 2016
// -----------------------------------------------------------------
float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float triangular_dither(vec2 frag_coord)
{
    float r1 = hash12(frag_coord);
    float r2 = hash12(frag_coord + vec2(97.0, 71.0));
    // Triangular PDF in [-1, 1], then scale to ±0.5 LSB in sRGB 8-bit
    return (r1 + r2 - 1.0) * (0.5 / 255.0);
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
    // Zenith base color: Bortle-dependent (LINEAR values)
    //
    //   Bortle 1: near-black with faint blue tint
    //   Bortle 9: noticeably grey-purple inner city
    // -----------------------------------------------------------------
    vec3 zenith_color = mix(
        vec3(0.0003, 0.0003, 0.0012),   // Bortle 1
        vec3(0.006,  0.005,  0.008),     // Bortle 9
        bortle_norm
    );

    // -----------------------------------------------------------------
    // Horizon color: warmer due to light pollution (sodium spectrum)
    //
    //   Bortle 1: barely perceptible natural airglow
    //   Bortle 9: prominent warm orange glow
    // -----------------------------------------------------------------
    vec3 horizon_color = mix(
        vec3(0.0005, 0.0004, 0.0006),   // Bortle 1
        vec3(0.015,  0.010,  0.005),     // Bortle 9
        bortle_norm
    );

    // -----------------------------------------------------------------
    // Gradient blending via airmass
    // -----------------------------------------------------------------
    float X = airmass(altitude);
    X = min(X, 10.0);

    float lp_factor = mix(0.005, 0.15, bortle_norm);
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
    // -----------------------------------------------------------------
    if (altitude < 0.0)
    {
        float below_factor = smoothstep(-0.087, 0.0, altitude);
        sky *= below_factor;
    }

    // -----------------------------------------------------------------
    // Dithering: add triangular noise to break 8-bit banding
    //
    // Without this, the tiny linear values (0.0003–0.006) map to
    // only 3-5 distinct sRGB levels, creating visible step bands.
    // The noise is ±0.5 LSB — invisible to the eye but randomizes
    // which sRGB level each pixel rounds to, producing a smooth
    // perceptual gradient.
    // -----------------------------------------------------------------
    float dither = triangular_dither(gl_FragCoord.xy);
    sky += vec3(dither);

    // Ensure we don't go negative from dither
    sky = max(sky, vec3(0.0));

    frag_color = vec4(sky, 1.0);
}