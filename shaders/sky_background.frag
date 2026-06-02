#version 450

// -----------------------------------------------------------------
// Sky background fragment shader — parametric twilight sky gradient
//
// Computes per-pixel sky color based on Sun/Moon position and
// atmospheric state. Pure black when atmosphere_enabled==0.
//
// When enabled, smoothstep blends across:
//   Night (sun_alt < -18°): Bortle gradient + optional Moon glow
//   Astronomical twilight (-18°..−12°): faint warm glow at horizon
//   Nautical twilight     (−12°..−6°):  deep blue + warm horizon
//   Civil twilight        (−6°..0°):    orange/pink horizon blend
//   Day                   (>0°):         bright blue sky
//
// All transitions use smoothstep — no hard altitude cliffs.
//
// This is PARAMETRIC only. Physical Rayleigh/Mie scattering is
// reserved for imaging mode (future sprint).
//
// IMPORTANT: The swapchain uses B8G8R8A8_SRGB, which means the GPU
// applies sRGB gamma encoding to our output. We must output LINEAR
// values that, after sRGB encoding, look correct on screen.
// -----------------------------------------------------------------

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 frag_color;

layout(push_constant) uniform ViewportPC
{
    vec2 viewport_origin;
    vec2 viewport_size;
} pc;

layout(set = 0, binding = 0) uniform SkyUBO
{
    // Row 0 (bytes 0–15)
    float camera_alt_rad;     // Camera altitude in radians
    float camera_az_rad;      // Camera azimuth in radians
    float fov_rad;            // Vertical field of view in radians
    float aspect_ratio;       // Viewport width / height

    // Row 1 (bytes 16–31)
    float bortle_scale;       // Bortle dark-sky scale (1–9)
    float sun_altitude_deg;   // Sun altitude in degrees
    float sun_azimuth_deg;    // Sun azimuth in degrees (0=N, 90=E)
    float moon_altitude_deg;  // Moon altitude in degrees

    // Row 2 (bytes 32–47)
    float moon_azimuth_deg;   // Moon azimuth in degrees
    float moon_illumination;  // Moon illumination fraction (0..1)
    uint  atmosphere_enabled; // 0=disabled (pure black), 1=enabled
    float _pad0;
};

// -----------------------------------------------------------------
// Constants
// -----------------------------------------------------------------
const float PI      = 3.14159265358979;
const float RAD2DEG = 180.0 / PI;
const float DEG2RAD = PI / 180.0;
const float HALF_PI = PI * 0.5;

// Moon glow parameters
const float MOON_GLOW_MIN_ALT_DEG = -5.0;  // Moon glow visible even slightly below horizon
const float MOON_GLOW_RADIUS_DEG  = 30.0;  // Angular radius of diffuse glow bloom
const vec3  MOON_GLOW_COLOR = vec3(0.10, 0.10, 0.09);  // Warm white (slightly warm tint)
const float MOON_GLOW_INTENSITY = 0.5;     // Scale factor for moon glow brightness

// -----------------------------------------------------------------
// Compute the altitude angle (radians) for this fragment.
// Top of screen = higher altitude, bottom = lower altitude.
// -----------------------------------------------------------------
float compute_altitude(vec2 uv)
{
    float centered_y = uv.y - 0.5;
    float alt_offset = -centered_y * fov_rad;
    float altitude = camera_alt_rad + alt_offset;
    return clamp(altitude, -0.15, HALF_PI);
}

// -----------------------------------------------------------------
// Compute the azimuth angle (degrees) for this fragment.
// Linear approximation valid for small-to-medium FOVs.
// -----------------------------------------------------------------
float compute_azimuth_deg(vec2 uv)
{
    float centered_x = uv.x - 0.5;
    float az_rad = camera_az_rad + centered_x * fov_rad * aspect_ratio;
    return az_rad * RAD2DEG;
}

// -----------------------------------------------------------------
// Angular distance between two Alt/Az positions (degrees).
// Uses the spherical law of cosines. Inputs in degrees.
// -----------------------------------------------------------------
float angular_distance_deg(float alt1_deg, float az1_deg, float alt2_deg, float az2_deg)
{
    float a1 = alt1_deg * DEG2RAD;
    float z1 = az1_deg  * DEG2RAD;
    float a2 = alt2_deg * DEG2RAD;
    float z2 = az2_deg  * DEG2RAD;
    float cos_ang = sin(a1) * sin(a2) + cos(a1) * cos(a2) * cos(z1 - z2);
    return acos(clamp(cos_ang, -1.0, 1.0)) * RAD2DEG;
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
// a hash of gl_FragCoord. Triangular distribution concentrates noise
// near zero, reducing visible grain while still breaking banding.
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
    return (r1 + r2 - 1.0) * (0.5 / 255.0);
}

// -----------------------------------------------------------------
// Bortle-based night-sky gradient color for a given altitude.
// Used as the base night color — the same as the original shader.
// -----------------------------------------------------------------
vec3 bortle_gradient(float altitude_rad, float bscale)
{
    float bortle_norm = (bscale - 1.0) / 8.0;

    vec3 zenith_color = mix(
        vec3(0.0003, 0.0003, 0.0012),   // Bortle 1
        vec3(0.006,  0.005,  0.008),     // Bortle 9
        bortle_norm
    );
    vec3 horizon_color = mix(
        vec3(0.0005, 0.0004, 0.0006),   // Bortle 1
        vec3(0.015,  0.010,  0.005),     // Bortle 9
        bortle_norm
    );

    float X = min(airmass(altitude_rad), 10.0);
    float lp_factor = mix(0.005, 0.15, bortle_norm);
    float atm_brightness = 1.0 + lp_factor * (X - 1.0);

    float alt_deg = altitude_rad * RAD2DEG;
    float horizon_blend = 1.0 - smoothstep(0.0, 45.0, alt_deg);

    vec3 sky = mix(zenith_color, horizon_color, horizon_blend);
    sky *= atm_brightness;

    if (altitude_rad < 0.0)
    {
        sky *= smoothstep(-0.087, 0.0, altitude_rad);
    }

    return sky;
}

// -----------------------------------------------------------------
// Sky color computation
// -----------------------------------------------------------------
void main()
{
    // ---- Atmosphere disabled: pure black sky ----
    if (atmosphere_enabled == 0u)
    {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float frag_alt_rad = compute_altitude(v_uv);
    float frag_az_deg  = compute_azimuth_deg(v_uv);
    float frag_alt_deg = frag_alt_rad * RAD2DEG;

    // ---- Sun altitude band transitions (all via smoothstep) ----
    float t_astro    = smoothstep(-18.0, -12.0, sun_altitude_deg);
    float t_nautical = smoothstep(-12.0,  -6.0, sun_altitude_deg);
    float t_civil    = smoothstep( -6.0,   0.0, sun_altitude_deg);
    float t_day      = smoothstep(  0.0,   6.0, sun_altitude_deg);

    // ---- Per-band base sky colors ----
    vec3 night_color    = bortle_gradient(frag_alt_rad, bortle_scale);

    // Astronomical twilight: very dark blue tint over night
    vec3 astro_color    = mix(night_color, vec3(0.001, 0.001, 0.004), 0.4);

    // Nautical: deep blue, altitude-dependent
    vec3 nautical_zenith  = vec3(0.01, 0.02, 0.06);
    vec3 nautical_horizon = vec3(0.04, 0.06, 0.12);
    float n_blend = 1.0 - smoothstep(0.0, 60.0, frag_alt_deg);
    vec3 nautical_color = mix(nautical_zenith, nautical_horizon, n_blend);
    if (frag_alt_rad < 0.0) nautical_color *= smoothstep(-0.087, 0.0, frag_alt_rad);

    // Civil: medium blue, brighter at horizon
    vec3 civil_zenith  = vec3(0.08, 0.14, 0.28);
    vec3 civil_horizon = vec3(0.22, 0.26, 0.42);
    float c_blend = 1.0 - smoothstep(0.0, 75.0, frag_alt_deg);
    vec3 civil_color = mix(civil_zenith, civil_horizon, c_blend);
    if (frag_alt_rad < 0.0) civil_color *= smoothstep(-0.087, 0.0, frag_alt_rad);

    // Day: bright blue, slightly brighter near horizon
    vec3 day_zenith  = vec3(0.25, 0.45, 0.85);
    vec3 day_horizon = vec3(0.45, 0.62, 0.90);
    float d_blend = 1.0 - smoothstep(0.0, 80.0, frag_alt_deg);
    vec3 day_color = mix(day_zenith, day_horizon, d_blend);
    if (frag_alt_rad < 0.0) day_color *= smoothstep(-0.087, 0.0, frag_alt_rad);

    // ---- Blend bands based on sun altitude ----
    vec3 base = night_color;
    base = mix(base, astro_color,    t_astro);
    base = mix(base, nautical_color, t_nautical);
    base = mix(base, civil_color,    t_civil);
    base = mix(base, day_color,      t_day);

    // ---- Twilight horizon glow in Sun's direction ----
    // Active from the onset of astronomical twilight (-18°) through daybreak (+6°).
    // At pure night (sun_alt < -18°) this is zero — no spurious warm glow at midnight.
    float twilight_weight = smoothstep(-18.0, -12.0, sun_altitude_deg) * (1.0 - t_day);

    if (twilight_weight > 0.001)
    {
        // Azimuth distance from fragment to sun's azimuth: 0° (toward sun) .. 180°
        // Add 540.0 = 360.0 + 180.0 before mod to ensure a positive argument, then
        // subtract 180.0 so the result is symmetric 0..180 rather than directional.
        float daz = abs(mod(frag_az_deg - sun_azimuth_deg + 540.0, 360.0) - 180.0);
        float az_falloff  = smoothstep(90.0, 0.0, daz);
        float alt_falloff = smoothstep(35.0, 0.0, max(frag_alt_deg, 0.0));

        // Color shifts from warm orange (low sun) toward pink-orange (rising sun)
        vec3 glow_color = mix(
            vec3(0.60, 0.18, 0.05),   // deep orange (twilight low)
            vec3(0.80, 0.42, 0.18),   // warm pink-orange (approaching sunrise)
            t_civil
        );

        base += glow_color * az_falloff * alt_falloff * twilight_weight * 0.6;
    }

    // ---- Moon glow: diffuse warm-white bloom near Moon position ----
    // Only at night / early twilight, not in full daylight.
    // The threshold MOON_GLOW_MIN_ALT_DEG = -5° lets refracted moonlight
    // contribute even when the Moon is just below the geometric horizon.
    if (moon_altitude_deg > MOON_GLOW_MIN_ALT_DEG && moon_illumination > 0.05 && t_day < 0.5)
    {
        float ang = angular_distance_deg(
            frag_alt_deg, frag_az_deg,
            moon_altitude_deg, moon_azimuth_deg
        );
        float moon_glow = smoothstep(MOON_GLOW_RADIUS_DEG, 0.0, ang)
                        * moon_illumination
                        * (1.0 - t_day);

        base += MOON_GLOW_COLOR * moon_glow * MOON_GLOW_INTENSITY;
    }

    // ---- Dithering: break 8-bit banding artifacts ----
    vec2 local_coord = (gl_FragCoord.xy - pc.viewport_origin) / max(pc.viewport_size, vec2(1.0));
    float dither = triangular_dither(local_coord * pc.viewport_size);
    base += vec3(dither);

    // Clamp to valid linear range
    base = clamp(base, vec3(0.0), vec3(1.0));

    frag_color = vec4(base, 1.0);
}