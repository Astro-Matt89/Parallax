/// @file image_analyzer.cpp
/// @brief ImageAnalyzer implementation — Sprint 10a Task 10a.8.

#include "analysis/image_analyzer.hpp"

#include "imaging/multispectral_image.hpp"
#include "instruments/snr_calculator.hpp"
#include "knowledge/property_registry.hpp"
#include "observation/data_record.hpp"
#include "universe/celestial_object.hpp"
#include "universe/universe.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace parallax::analysis
{

// =============================================================================
// Constants
// =============================================================================

namespace
{

/// @brief Minimum physical SNR to report L1 measurements (position + magnitude).
constexpr double kSnrThresholdL1 = 5.0;

/// @brief Minimum physical SNR to report L2 measurements (colour, spectral).
constexpr double kSnrThresholdL2 = 20.0;

/// @brief Minimum physical SNR to report L3 measurements (parallax, distance).
constexpr double kSnrThresholdL3 = 50.0;

/// @brief Noise-estimation MAD multiplier.
///
/// Threshold = median + kMadK * MAD.  A k of 3.0 gives a ~99.7 % confidence
/// upper tail for Gaussian noise (the 3-sigma rule).
constexpr double kMadK = 3.0;

/// @brief Peak-detection threshold: centre pixel must exceed this multiple of
///        the estimated noise floor for a source to be considered detected.
constexpr double kDetectionSigma = 3.0;

/// @brief Base measurement uncertainty for position/angle properties (arcsec-equivalent).
constexpr double kBaseUncertaintyRaDec = 1.0e-3;

/// @brief Base magnitude uncertainty.
constexpr double kBaseUncertaintyMag = 1.0e-2;

/// @brief Base colour-index uncertainty.
constexpr double kBaseUncertaintyColor = 1.0e-2;

/// @brief Base parallax uncertainty (mas).
constexpr double kBaseUncertaintyParallax = 1.0;

/// @brief Base distance uncertainty (pc).
constexpr double kBaseUncertaintyDistance = 0.5;

/// @brief Conversion factor: 1000 / parallax_mas = distance_pc (no zero parallax guard).
constexpr double kParallaxToDistancePcFactor = 1000.0;

// ---------------------------------------------------------------------------
// Noise estimation
// ---------------------------------------------------------------------------

/// @brief Estimate the per-band noise floor as  median + kMadK * MAD.
///
/// Uses a copy of the pixel buffer so the original image is not modified.
/// Returns 0.0 if the band has no pixels.
[[nodiscard]] double estimate_noise_floor(std::span<const float> pixels) noexcept
{
    if (pixels.empty())
    {
        return 0.0;
    }

    // Build a sorted copy for median / MAD computation.
    std::vector<float> sorted(pixels.begin(), pixels.end());
    std::sort(sorted.begin(), sorted.end());

    const std::size_t n = sorted.size();
    const double median = (n % 2 == 0)
        ? 0.5 * (static_cast<double>(sorted[n / 2 - 1]) + static_cast<double>(sorted[n / 2]))
        : static_cast<double>(sorted[n / 2]);

    // Compute median absolute deviation (MAD).
    std::vector<double> abs_dev(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        abs_dev[i] = std::abs(static_cast<double>(sorted[i]) - median);
    }
    std::sort(abs_dev.begin(), abs_dev.end());
    const double mad = (n % 2 == 0)
        ? 0.5 * (abs_dev[n / 2 - 1] + abs_dev[n / 2])
        : abs_dev[n / 2];

    return median + kMadK * mad;
}

// ---------------------------------------------------------------------------
// Aperture sum
// ---------------------------------------------------------------------------

/// @brief Sum pixels in a square aperture of radius @p half_side around (cx, cy).
///
/// Clamps to image bounds. Returns 0.0 if the aperture contains no pixels.
[[nodiscard]] double aperture_sum(const imaging::Image& img,
                                  std::uint32_t         cx,
                                  std::uint32_t         cy,
                                  std::uint32_t         half_side) noexcept
{
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    const std::uint32_t x0 = (cx >= half_side) ? cx - half_side : 0u;
    const std::uint32_t y0 = (cy >= half_side) ? cy - half_side : 0u;
    const std::uint32_t x1 = std::min(cx + half_side + 1u, w);
    const std::uint32_t y1 = std::min(cy + half_side + 1u, h);

    double sum = 0.0;
    for (std::uint32_t y = y0; y < y1; ++y)
    {
        for (std::uint32_t x = x0; x < x1; ++x)
        {
            sum += static_cast<double>(img(x, y));
        }
    }
    return sum;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief SNR-scaled uncertainty: base / sqrt(snr), clamped to base when snr <= 0.
[[nodiscard]] double scaled_uncertainty(double base, double snr) noexcept
{
    if (snr <= 0.0)
    {
        return base;
    }
    return base / std::sqrt(snr);
}

/// @brief True if a property exists in the PropertyRegistry for this object type.
[[nodiscard]] bool property_exists(universe::ObjectType type, std::string_view name) noexcept
{
    return knowledge::PropertyRegistry::get_property(type, name).has_value();
}

/// @brief Build a measurement update if the property is registered for this type.
[[nodiscard]] std::optional<KnowledgeUpdate> make_update(universe::ObjectType type,
                                                          std::uint64_t        object_id,
                                                          std::string_view     property_name,
                                                          MeasurementValue     value,
                                                          double               base_uncertainty,
                                                          double               snr)
{
    if (!property_exists(type, property_name))
    {
        return std::nullopt;
    }
    return KnowledgeUpdate{
        .object_id     = object_id,
        .property_name = std::string(property_name),
        .value         = value,
        .uncertainty   = scaled_uncertainty(base_uncertainty, snr),
        .snr           = snr,
        .new_level     = std::nullopt,
    };
}

// ---------------------------------------------------------------------------
// Colour from image
// ---------------------------------------------------------------------------

/// @brief Derive a pseudo-B–V colour index from the ratio of two band aperture sums.
///
/// colour_AB = -2.5 * log10(sum_A / sum_B)
///
/// Returns std::nullopt if either sum is not positive (avoids log(0) or log(<0)).
[[nodiscard]] std::optional<double>
compute_image_color(double sum_a, double sum_b) noexcept
{
    if (sum_a <= 0.0 || sum_b <= 0.0)
    {
        return std::nullopt;
    }
    return -2.5 * std::log10(sum_a / sum_b);
}

} // anonymous namespace

// =============================================================================
// ImageAnalyzer::analyze
// =============================================================================

std::vector<KnowledgeUpdate>
ImageAnalyzer::analyze(const observation::DataRecord&     record,
                       const imaging::MultispectralImage& image,
                       const universe::Universe&          universe) const
{
    // ------------------------------------------------------------------
    // 1. Guard: need a valid target and at least one image band.
    // ------------------------------------------------------------------
    if (record.target_object_id == 0 || image.band_count() == 0)
    {
        return {};
    }

    // ------------------------------------------------------------------
    // 2. Resolve target from universe.
    // ------------------------------------------------------------------
    const std::optional<universe::CelestialObject> object_opt =
        universe.query_object(record.target_object_id);
    if (!object_opt.has_value())
    {
        return {};
    }

    const universe::CelestialObject& object = *object_opt;

    // Only star-type objects have measurement mappings for now.
    const bool is_star_type = (object.type == universe::ObjectType::Star
                            || object.type == universe::ObjectType::ProceduralStar);
    if (!is_star_type)
    {
        return {};
    }

    const auto* star_data = std::get_if<universe::StarData>(&object.data);
    if (star_data == nullptr)
    {
        return {};
    }

    // ------------------------------------------------------------------
    // 3. Per-band noise estimation and source detection.
    //
    //    The image formation engine places the target at the image centre
    //    (gnomonic projection, zero angular offset → centre pixel).
    //    Target centre pixel: (width-1)/2, (height-1)/2.
    // ------------------------------------------------------------------
    const std::uint32_t cx = (image.width()  > 0) ? (image.width()  - 1) / 2 : 0;
    const std::uint32_t cy = (image.height() > 0) ? (image.height() - 1) / 2 : 0;

    // Aperture half-side: 2 pixels (5×5 box) to capture PSF wings.
    constexpr std::uint32_t kApertureHalfSide = 2;

    // Collect per-band sums for colour and determine if any band detects the target.
    std::vector<double> band_sums;
    band_sums.reserve(image.band_count());
    bool detected_any_band = false;

    for (const imaging::Image& band_img : image.bands())
    {
        const double noise_floor = estimate_noise_floor(band_img.pixels());
        const double center_val  = static_cast<double>(band_img(cx, cy));
        const double sum         = aperture_sum(band_img, cx, cy, kApertureHalfSide);

        band_sums.push_back(sum);

        if (center_val > noise_floor * kDetectionSigma)
        {
            detected_any_band = true;
        }
    }

    // No band detected a source — return empty.
    if (!detected_any_band)
    {
        return {};
    }

    // ------------------------------------------------------------------
    // 4. Knowledge updates gated by physical SNR.
    // ------------------------------------------------------------------
    const double snr = record.achieved_snr;
    std::vector<KnowledgeUpdate> updates;

    auto push = [&](std::string_view prop, MeasurementValue val, double base_unc)
    {
        if (auto u = make_update(object.type, object.id, prop, val, base_unc, snr))
        {
            updates.push_back(std::move(*u));
        }
    };

    // L1: position + visual magnitude (same properties as MockAnalyzer)
    if (snr >= kSnrThresholdL1)
    {
        push("ra",    object.ra,                             kBaseUncertaintyRaDec);
        push("dec",   object.dec,                            kBaseUncertaintyRaDec);
        push("mag_v", static_cast<double>(object.mag_v),    kBaseUncertaintyMag);
    }

    // L2: colour index.
    //   Prefer image-derived colour if two or more bands are available (avoids
    //   absolute calibration).  Fallback to universe colour_bv otherwise.
    if (snr >= kSnrThresholdL2)
    {
        if (band_sums.size() >= 2)
        {
            // Image-derived colour: ratio of first two band aperture sums.
            // colour_01 = -2.5 * log10(sum[0] / sum[1])
            const std::optional<double> img_color = compute_image_color(band_sums[0], band_sums[1]);
            if (img_color.has_value())
            {
                push("color_bv", *img_color, kBaseUncertaintyColor);
            }
            else
            {
                // Fall back to universe truth value if sums are non-positive (very faint).
                push("color_bv", static_cast<double>(object.color_bv), kBaseUncertaintyColor);
            }
        }
        else
        {
            // Single-band observation: use universe colour.
            push("color_bv", static_cast<double>(object.color_bv), kBaseUncertaintyColor);
        }
    }

    // L3: parallax and distance (structural parameters).
    if (snr >= kSnrThresholdL3)
    {
        if (star_data->parallax_mas > 0.0f)
        {
            const double parallax_mas = static_cast<double>(star_data->parallax_mas);
            push("parallax_mas", parallax_mas, kBaseUncertaintyParallax);

            const double dist_pc = (star_data->distance_pc > 0.0f)
                ? static_cast<double>(star_data->distance_pc)
                : kParallaxToDistancePcFactor / parallax_mas;

            push("distance_pc", dist_pc, kBaseUncertaintyDistance);
        }
        else if (star_data->distance_pc > 0.0f)
        {
            push("distance_pc",
                 static_cast<double>(star_data->distance_pc),
                 kBaseUncertaintyDistance);
        }
    }

    return updates;
}

} // namespace parallax::analysis
