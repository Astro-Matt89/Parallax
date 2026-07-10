/// @file test_psf.cpp
/// @brief Unit tests for Image, MultispectralImage, and PSF (Sprint 10a Task 10a.3).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "imaging/image.hpp"
#include "imaging/multispectral_image.hpp"
#include "imaging/psf.hpp"

#include <cmath>
#include <numeric>

namespace
{
    using parallax::f32;
    using parallax::f64;
    using parallax::u32;
    using parallax::imaging::Image;
    using parallax::imaging::ImageMetadata;
    using parallax::imaging::MultispectralImage;
    using parallax::imaging::Psf;

    // Tolerance for flux-conservation checks (0.1 %).
    constexpr f64 kFluxTol = 1.0e-3;

    /// Sum all pixel values in an image.
    f64 pixel_sum(const Image& img)
    {
        f64 total = 0.0;
        for (const f32 v : img.pixels())
        {
            total += static_cast<f64>(v);
        }
        return total;
    }

    /// Return the maximum pixel value in an image.
    f32 pixel_max(const Image& img)
    {
        f32 peak = 0.0f;
        for (const f32 v : img.pixels())
        {
            if (v > peak) { peak = v; }
        }
        return peak;
    }

    /// Return the (x, y) coordinates of the brightest pixel.
    std::pair<u32, u32> peak_coords(const Image& img)
    {
        u32 bx = 0, by = 0;
        f32 best = -1.0f;
        for (u32 y = 0; y < img.height(); ++y)
        {
            for (u32 x = 0; x < img.width(); ++x)
            {
                if (img(x, y) > best)
                {
                    best = img(x, y);
                    bx = x;
                    by = y;
                }
            }
        }
        return {bx, by};
    }
}

// =============================================================================
// Image construction and basic operations
// =============================================================================

TEST_CASE("Image is zero-initialised on construction")
{
    Image img(32, 32);
    CHECK(pixel_sum(img) == doctest::Approx(0.0));
}

TEST_CASE("Image width and height accessors are correct")
{
    Image img(64, 48);
    CHECK(img.width() == 64u);
    CHECK(img.height() == 48u);
}

TEST_CASE("Image fill sets all pixels to the given value")
{
    Image img(16, 16);
    img.fill(3.14f);
    const f64 expected = 3.14 * 16.0 * 16.0;
    CHECK(pixel_sum(img) == doctest::Approx(expected).epsilon(1.0e-4));
}

TEST_CASE("Image clear resets all pixels to zero")
{
    Image img(16, 16);
    img.fill(5.0f);
    img.clear();
    CHECK(pixel_sum(img) == doctest::Approx(0.0));
}

TEST_CASE("Image at() and operator() read and write correctly")
{
    Image img(10, 10);
    img(3, 7) = 42.0f;
    CHECK(img(3, 7) == doctest::Approx(42.0f));
    CHECK(img.at(3, 7) == doctest::Approx(42.0f));
}

TEST_CASE("Image at() throws on out-of-range access")
{
    Image img(8, 8);
    CHECK_THROWS_AS(img.at(8, 0), std::out_of_range);
    CHECK_THROWS_AS(img.at(0, 8), std::out_of_range);
}

// =============================================================================
// MultispectralImage
// =============================================================================

TEST_CASE("MultispectralImage starts with zero bands")
{
    MultispectralImage ms(128, 128, 0.05);
    CHECK(ms.band_count() == 0u);
    CHECK(ms.width() == 128u);
    CHECK(ms.height() == 128u);
    CHECK(ms.pixel_scale_arcsec_per_px() == doctest::Approx(0.05));
}

TEST_CASE("MultispectralImage emplace_band adds a band with correct geometry")
{
    MultispectralImage ms(64, 64, 0.1);
    ms.emplace_band(ImageMetadata{.band_name = "V", .wavelength_nm = 550.0});
    ms.emplace_band(ImageMetadata{.band_name = "H-alpha", .wavelength_nm = 656.3});

    CHECK(ms.band_count() == 2u);
    CHECK(ms.band(0).width() == 64u);
    CHECK(ms.band(1).height() == 64u);
    CHECK(ms.band(0).metadata().band_name == "V");
    CHECK(ms.band(1).metadata().band_name == "H-alpha");
    // Pixel scale is propagated from the MultispectralImage.
    CHECK(ms.band(0).metadata().pixel_scale_arcsec_per_px == doctest::Approx(0.1));
}

TEST_CASE("MultispectralImage add_band rejects mismatched geometry")
{
    MultispectralImage ms(64, 64, 0.1);
    Image wrong(32, 32);
    CHECK_THROWS_AS(ms.add_band(std::move(wrong)), std::invalid_argument);
}

// =============================================================================
// PSF — FWHM helpers
// =============================================================================

TEST_CASE("Psf::fwhm_px matches the 1.028*lambda/D formula")
{
    // 12 m aperture at 550 nm, pixel scale = 0.01 arcsec/px.
    constexpr f64 aperture_m = 12.0;
    constexpr f64 lambda_nm  = 550.0;
    constexpr f64 pscale     = 0.01; // arcsec/px

    const f64 fwhm_rad    = 1.028 * (lambda_nm * 1.0e-9) / aperture_m;
    const f64 fwhm_arcsec = fwhm_rad * (180.0 * 3600.0 / 3.14159265358979323846);
    const f64 expected_px = fwhm_arcsec / pscale;

    CHECK(Psf::fwhm_px(lambda_nm, aperture_m, pscale) == doctest::Approx(expected_px).epsilon(1.0e-6));
}

// =============================================================================
// PSF — render_point_source flux conservation
// =============================================================================

TEST_CASE("render_point_source conserves total flux for an interior source")
{
    // Place the source well inside a 128×128 image so no flux is clipped.
    Image img(128, 128);
    constexpr f64 total_flux = 1000.0;
    constexpr f64 fwhm_px   = 4.0;

    Psf::render_point_source(img, 64.0, 64.0, total_flux, fwhm_px);

    const f64 deposited = pixel_sum(img);
    CHECK(deposited == doctest::Approx(total_flux).epsilon(kFluxTol));
}

TEST_CASE("render_point_source: wider PSF produces lower peak than narrow PSF")
{
    // Same total flux, different FWHM.
    constexpr f64 total_flux    = 1000.0;
    constexpr f64 fwhm_narrow   = 2.0;
    constexpr f64 fwhm_wide     = 8.0;

    Image narrow_img(128, 128);
    Image wide_img(128, 128);

    Psf::render_point_source(narrow_img, 64.0, 64.0, total_flux, fwhm_narrow);
    Psf::render_point_source(wide_img,   64.0, 64.0, total_flux, fwhm_wide);

    CHECK(pixel_max(narrow_img) > pixel_max(wide_img));
}

TEST_CASE("render_point_source: peak pixel is nearest to the sub-pixel centre")
{
    Image img(64, 64);
    // Place source at a known sub-pixel offset — peak should be at integer pixel (31,31).
    Psf::render_point_source(img, 31.3, 31.7, 500.0, 3.0);

    const auto [px, py] = peak_coords(img);
    // Within 1 pixel of the rounded centre.
    CHECK(std::abs(static_cast<int>(px) - 31) <= 1);
    CHECK(std::abs(static_cast<int>(py) - 31) <= 1);
}

TEST_CASE("render_point_source accumulates (does not overwrite) on repeated calls")
{
    Image img(64, 64);
    constexpr f64 flux = 300.0;

    Psf::render_point_source(img, 32.0, 32.0, flux, 3.0);
    Psf::render_point_source(img, 32.0, 32.0, flux, 3.0);

    // Two identical sources → total ≈ 2 × flux.
    CHECK(pixel_sum(img) == doctest::Approx(2.0 * flux).epsilon(kFluxTol));
}

TEST_CASE("render_point_source with zero flux is a no-op")
{
    Image img(32, 32);
    Psf::render_point_source(img, 16.0, 16.0, 0.0, 2.0);
    CHECK(pixel_sum(img) == doctest::Approx(0.0));
}
