/// @file universe.cpp
/// @brief Implementation of the Universe facade.

#include "universe/universe.hpp"

#include "universe/star_catalog_provider.hpp"
#include "universe/dso_provider.hpp"
#include "universe/solar_system_provider.hpp"
#include "universe/procedural_provider.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>

namespace parallax::universe
{

// ---------------------------------------------------------------------------
// Anonymous helpers
// ---------------------------------------------------------------------------

namespace
{

/// @brief Degrees-to-radians conversion factor.
constexpr double kDegToRad = std::numbers::pi / 180.0;

/// @brief Strip leading/trailing ASCII whitespace from a string (in-place).
void trim(std::string& s)
{
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

/// @brief Strip UTF-8 BOM (\xEF\xBB\xBF) from the front of @p s if present.
void strip_bom(std::string& s)
{
    if (s.size() >= 3
        && static_cast<unsigned char>(s[0]) == 0xEF
        && static_cast<unsigned char>(s[1]) == 0xBB
        && static_cast<unsigned char>(s[2]) == 0xBF)
    {
        s.erase(0, 3);
    }
}

/// @brief Parse a decimal integer from a string_view.  Returns 0 on failure.
[[nodiscard]] std::uint32_t parse_u32(std::string_view sv) noexcept
{
    std::uint32_t result = 0;
    std::from_chars(sv.data(), sv.data() + sv.size(), result);
    return result;
}

/// @brief Split a string by a single delimiter character.
[[nodiscard]] std::vector<std::string> split_csv_line(const std::string& line, char delim = ',')
{
    std::vector<std::string> fields;
    std::istringstream ss(line);
    std::string field;
    while (std::getline(ss, field, delim))
    {
        trim(field);
        fields.push_back(std::move(field));
    }
    return fields;
}

} // anonymous namespace

// =============================================================================
// Construction / destruction
// =============================================================================

Universe::Universe()
    : stars_(std::make_unique<StarCatalogProvider>())
    , dsos_(std::make_unique<DsoCatalogProvider>())
    , solar_(std::make_unique<SolarSystemProvider>())
    , procedural_(std::make_unique<ProceduralProvider>())
    , body_names_{"Sun", "Moon", "Mercury", "Venus", "Mars",
                  "Jupiter", "Saturn", "Uranus", "Neptune"}
{
}

Universe::~Universe() = default;

Universe::Universe(Universe&&) noexcept = default;
Universe& Universe::operator=(Universe&&) noexcept = default;

// =============================================================================
// load_catalogs
// =============================================================================

bool Universe::load_catalogs(const std::filesystem::path& data_dir)
{
    // -------------------------------------------------------------------------
    // 1. Star catalog (Tycho-2 required; Hipparcos optional)
    // -------------------------------------------------------------------------
    const auto tycho2_path     = data_dir / "tycho2.csv";
    const auto hipparcos_path  = data_dir / "hipparcos.csv";

    const std::optional<std::filesystem::path> hip_opt =
        std::filesystem::exists(hipparcos_path) ? std::optional{hipparcos_path} : std::nullopt;

    const bool stars_ok = stars_->load(tycho2_path, hip_opt);
    if (!stars_ok)
    {
        PLX_CORE_ERROR("Universe: failed to load star catalog from '{}'", tycho2_path.string());
        return false;
    }

    // -------------------------------------------------------------------------
    // 2. DSO catalog (Messier)
    // -------------------------------------------------------------------------
    const auto messier_path = data_dir / "messier.csv";
    const bool dsos_ok = dsos_->load(messier_path);
    if (!dsos_ok)
    {
        PLX_CORE_WARN("Universe: DSO catalog not loaded from '{}'", messier_path.string());
    }

    // -------------------------------------------------------------------------
    // 3. Star name / Bayer database
    // -------------------------------------------------------------------------
    const auto names_path = data_dir / "star_names.csv";
    if (std::filesystem::exists(names_path))
    {
        load_name_database(names_path);
    }
    else
    {
        PLX_CORE_WARN("Universe: star_names.csv not found at '{}'", names_path.string());
    }

    // -------------------------------------------------------------------------
    // 4. Messier popular names (hardcoded)
    // -------------------------------------------------------------------------
    init_messier_names();

    // -------------------------------------------------------------------------
    // 5. Constellation full-name table (hardcoded 88 entries)
    // -------------------------------------------------------------------------
    init_constellation_full_names();

    PLX_CORE_INFO("Universe: catalogs loaded — {} stars, {} DSOs",
                  stars_->get_count(), dsos_->get_count());

    return stars_ok;
}

// =============================================================================
// init_procedural
// =============================================================================

void Universe::init_procedural(std::uint64_t master_seed)
{
    procedural_->set_master_seed(master_seed);
    procedural_initialized_ = true;
    PLX_CORE_INFO("Universe: procedural generator initialised (seed={})", master_seed);
}

// =============================================================================
// update
// =============================================================================

void Universe::update(double julian_date)
{
    solar_->update(julian_date);
}

// =============================================================================
// query_fov
// =============================================================================

void Universe::query_fov(double ra,
                         double dec,
                         double radius_deg,
                         float  mag_limit,
                         QueryFlags flags,
                         std::vector<CelestialObject>& results) const
{
    results.clear();

    // Rough capacity estimate to reduce reallocations.
    static constexpr double kFullSkyDeg2 = 41253.0;
    const double area_frac = (radius_deg * radius_deg) / kFullSkyDeg2;
    const std::size_t star_estimate =
        static_cast<std::size_t>(static_cast<double>(stars_->get_count()) * area_frac);
    results.reserve(star_estimate + procedural_->get_count() + dsos_->get_count() + 16u);

    if (has_flag(flags, QueryFlags::Stars))
    {
        stars_->query_fov(ra, dec, radius_deg, mag_limit, flags, results);
    }

    if (has_flag(flags, QueryFlags::DeepSky))
    {
        dsos_->query_fov(ra, dec, radius_deg, mag_limit, flags, results);
    }

    if (has_flag(flags, QueryFlags::SolarSystem))
    {
        solar_->query_fov(ra, dec, radius_deg, mag_limit, flags, results);
    }

    static constexpr float kMinMagForProcedural = 12.0f;
    if (has_flag(flags, QueryFlags::Procedural)
        && mag_limit > kMinMagForProcedural
        && procedural_initialized_)
    {
        procedural_->query_fov(ra, dec, radius_deg, mag_limit, flags, results);
    }

    // Sort brightest first (ascending mag_v).
    std::sort(results.begin(), results.end(),
              [](const CelestialObject& a, const CelestialObject& b) noexcept
              {
                  return a.mag_v < b.mag_v;
              });
}

// =============================================================================
// query_object
// =============================================================================

std::optional<CelestialObject> Universe::query_object(u64 id) const
{
    switch (decode_type(id))
    {
        case ObjectType::Star:
            return stars_->query_object(id);

        case ObjectType::SolarSystemBody:
            return solar_->query_object(id);

        case ObjectType::DeepSkyObject:
            return dsos_->query_object(id);

        case ObjectType::Galaxy:
            // No Galaxy provider in this sprint.
            return std::nullopt;

        case ObjectType::ProceduralStar:
        case ObjectType::ProceduralDso:
            return procedural_->query_object(id);

        case ObjectType::Unknown:
        default:
            return std::nullopt;
    }
}

// =============================================================================
// get_name
// =============================================================================

std::string_view Universe::get_name(u64 id) const
{
    const auto source_id = static_cast<std::uint32_t>(decode_source_id(id));

    switch (decode_type(id))
    {
        case ObjectType::Star:
        {
            // Prefer common name, then Bayer designation.
            if (const auto it = hip_names_.find(source_id); it != hip_names_.end())
            {
                return it->second;
            }
            if (const auto it = hip_bayer_.find(source_id); it != hip_bayer_.end())
            {
                return it->second;
            }
            return {};
        }

        case ObjectType::DeepSkyObject:
        case ObjectType::Galaxy:
        {
            if (const auto it = messier_names_.find(source_id); it != messier_names_.end())
            {
                return it->second;
            }
            // Return empty — callers may format "M<num>" themselves.
            return {};
        }

        case ObjectType::SolarSystemBody:
        {
            if (source_id < body_names_.size())
            {
                return body_names_[source_id];
            }
            return {};
        }

        case ObjectType::ProceduralStar:
        case ObjectType::ProceduralDso:
        case ObjectType::Unknown:
        default:
            return {};
    }
}

// =============================================================================
// get_constellation
// =============================================================================

std::string Universe::get_constellation(double ra, double dec) const
{
    // TODO: replace with a precise Delporte boundary implementation once boundary
    //       data is available.  This is a rectangular-region approximation for the
    //       ~20 most recognisable constellations; all other positions return "???".

    // Convert to degrees for the lookup table.
    const double ra_deg  = ra  / kDegToRad;  // ra in radians → degrees
    const double dec_deg = dec / kDegToRad;  // dec in radians → degrees

    // Normalise RA to [0, 360).
    double ra_norm = std::fmod(ra_deg, 360.0);
    if (ra_norm < 0.0)
    {
        ra_norm += 360.0;
    }

    struct Region
    {
        double ra_min;   ///< degrees [0, 360)
        double ra_max;   ///< degrees [0, 360)
        double dec_min;  ///< degrees [-90, 90]
        double dec_max;  ///< degrees [-90, 90]
        const char* abbrev;
    };

    // Table ordered with smaller/more distinctive regions first so overlaps
    // resolve deterministically toward the more specific match.
    // IMPORTANT: For regions straddling RA=0/360 two entries are used.
    static constexpr std::array<Region, 27> kRegions
    {{
        // UMi — Ursa Minor (polar cap)
        {  0.0, 360.0,  70.0,  90.0, "UMi"},
        // UMa — Ursa Major
        {145.0, 210.0,  40.0,  70.0, "UMa"},
        // Cas — Cassiopeia
        {  0.0,  30.0,  50.0,  70.0, "Cas"},
        {330.0, 360.0,  50.0,  70.0, "Cas"},
        // Cyg — Cygnus
        {290.0, 330.0,  27.0,  60.0, "Cyg"},
        // Per — Perseus
        { 35.0,  60.0,  30.0,  58.0, "Per"},
        // Aur — Auriga
        { 75.0,  100.0,  28.0,  56.0, "Aur"},
        // Dra — Draco (roughly)
        {240.0, 310.0,  50.0,  80.0, "Dra"},
        // And — Andromeda
        {  0.0,  30.0,  23.0,  50.0, "And"},
        {330.0, 360.0,  23.0,  50.0, "And"},
        // Lyr — Lyra
        {275.0, 295.0,  30.0,  47.0, "Lyr"},
        // Boo — Boötes
        {200.0, 240.0,  10.0,  55.0, "Boo"},
        // Peg — Pegasus
        {320.0, 360.0,   5.0,  35.0, "Peg"},
        {  0.0,  30.0,   5.0,  35.0, "Peg"},
        // Ori — Orion
        { 70.0,  90.0,  -15.0,  25.0, "Ori"},
        // Tau — Taurus
        { 55.0,  90.0,  10.0,  32.0, "Tau"},
        // Gem — Gemini
        { 95.0, 120.0,  15.0,  35.0, "Gem"},
        // Cnc — Cancer
        {120.0, 135.0,   6.0,  33.0, "Cnc"},
        // Leo — Leo
        {140.0, 175.0,  -5.0,  35.0, "Leo"},
        // Vir — Virgo
        {180.0, 215.0, -20.0,  15.0, "Vir"},
        // Sco — Scorpius
        {235.0, 260.0, -45.0,  -5.0, "Sco"},
        // Sgr — Sagittarius
        {270.0, 300.0, -45.0,  -5.0, "Sgr"},
        // Aql — Aquila
        {278.0, 305.0,  -5.0,  20.0, "Aql"},
        // Ari — Aries
        { 25.0,  55.0,   8.0,  30.0, "Ari"},
        // Psc — Pisces (straddles RA=0)
        {335.0, 360.0,  -7.0,  30.0, "Psc"},
        {  0.0,  25.0,  -7.0,  30.0, "Psc"},
        // Oph — Ophiuchus
        {245.0, 280.0, -30.0,  15.0, "Oph"},
    }};

    for (const auto& r : kRegions)
    {
        if (ra_norm >= r.ra_min && ra_norm < r.ra_max
            && dec_deg >= r.dec_min && dec_deg < r.dec_max)
        {
            return r.abbrev;
        }
    }

    return "???";
}

// =============================================================================
// get_constellation_full_name
// =============================================================================

std::string_view Universe::get_constellation_full_name(std::string_view abbrev) const
{
    // std::unordered_map requires a std::string key for lookup.
    const std::string key{abbrev};
    if (const auto it = constellation_full_names_.find(key); it != constellation_full_names_.end())
    {
        return it->second;
    }
    return {};
}

// =============================================================================
// resolve_hip
// =============================================================================

std::optional<CelestialObject> Universe::resolve_hip(std::uint32_t hip) const
{
    return stars_->resolve_hip(hip);
}

// =============================================================================
// Object counts
// =============================================================================

std::size_t Universe::get_real_object_count() const
{
    return stars_->get_count() + dsos_->get_count() + solar_->get_count();
}

std::size_t Universe::get_procedural_estimate() const
{
    return procedural_->get_count();
}

// =============================================================================
// Provider accessors
// =============================================================================

const StarCatalogProvider& Universe::stars() const
{
    return *stars_;
}

const DsoCatalogProvider& Universe::dsos() const
{
    return *dsos_;
}

const SolarSystemProvider& Universe::solar_system() const
{
    return *solar_;
}

const ProceduralProvider& Universe::procedural() const
{
    return *procedural_;
}

// =============================================================================
// load_name_database  (private)
// =============================================================================

void Universe::load_name_database(const std::filesystem::path& csv_path)
{
    std::ifstream file(csv_path);
    if (!file.is_open())
    {
        PLX_CORE_WARN("Universe: cannot open star name database '{}'", csv_path.string());
        return;
    }

    std::string line;
    bool header_found = false;
    int  hip_col     = -1;
    int  name_col    = -1;
    int  bayer_col   = -1;
    std::size_t loaded = 0;

    while (std::getline(file, line))
    {
        // Strip UTF-8 BOM on first line.
        if (!header_found)
        {
            strip_bom(line);
        }
        trim(line);

        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        const auto fields = split_csv_line(line);

        if (!header_found)
        {
            // Detect column positions from the header row.
            for (int i = 0; i < static_cast<int>(fields.size()); ++i)
            {
                std::string lower = fields[static_cast<std::size_t>(i)];
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (lower == "hip")  { hip_col  = i; }
                else if (lower == "name" || lower == "common_name") { name_col  = i; }
                else if (lower == "bayer")                          { bayer_col = i; }
            }

            header_found = true;

            if (hip_col < 0)
            {
                PLX_CORE_ERROR("Universe: star_names.csv has no 'HIP' column — skipping");
                return;
            }
            continue;
        }

        if (fields.empty())
        {
            continue;
        }

        // Safely read HIP number.
        const int n = static_cast<int>(fields.size());
        if (hip_col >= n)
        {
            continue;
        }

        const std::uint32_t hip = parse_u32(fields[static_cast<std::size_t>(hip_col)]);
        if (hip == 0)
        {
            continue;
        }

        if (name_col >= 0 && name_col < n)
        {
            const auto& name = fields[static_cast<std::size_t>(name_col)];
            if (!name.empty())
            {
                hip_names_.emplace(hip, name);
            }
        }

        if (bayer_col >= 0 && bayer_col < n)
        {
            const auto& bayer = fields[static_cast<std::size_t>(bayer_col)];
            if (!bayer.empty())
            {
                hip_bayer_.emplace(hip, bayer);
            }
        }

        ++loaded;
    }

    PLX_CORE_INFO("Universe: star name database loaded ({} entries from '{}')",
                  loaded, csv_path.filename().string());
}

// =============================================================================
// init_messier_names  (private)
// =============================================================================

void Universe::init_messier_names()
{
    // Well-known popular names for Messier objects.
    // Source: IAU / commonly accepted designations.
    messier_names_ =
    {
        { 1,  "Crab Nebula"          },
        { 8,  "Lagoon Nebula"        },
        { 11, "Wild Duck Cluster"    },
        { 13, "Great Hercules Cluster"},
        { 16, "Eagle Nebula"         },
        { 17, "Omega Nebula"         },
        { 20, "Trifid Nebula"        },
        { 22, "Sagittarius Cluster"  },
        { 27, "Dumbbell Nebula"      },
        { 31, "Andromeda Galaxy"     },
        { 32, "Le Gentil"            },
        { 33, "Triangulum Galaxy"    },
        { 42, "Orion Nebula"         },
        { 43, "De Mairan's Nebula"   },
        { 44, "Beehive Cluster"      },
        { 45, "Pleiades"             },
        { 51, "Whirlpool Galaxy"     },
        { 57, "Ring Nebula"          },
        { 63, "Sunflower Galaxy"     },
        { 64, "Black Eye Galaxy"     },
        { 65, "Leo Triplet (NGC 3623)"},
        { 66, "Leo Triplet (NGC 3627)"},
        { 74, "Phantom Galaxy"       },
        { 77, "Cetus A"              },
        { 81, "Bode's Galaxy"        },
        { 82, "Cigar Galaxy"         },
        { 83, "Southern Pinwheel"    },
        { 84, "Markarian's Chain"    },
        { 86, "Markarian's Chain"    },
        { 87, "Virgo A"              },
        { 92, "Hercules Cluster"     },
        {101, "Pinwheel Galaxy"      },
        {104, "Sombrero Galaxy"      },
    };
}

// =============================================================================
// init_constellation_full_names  (private)
// =============================================================================

void Universe::init_constellation_full_names()
{
    // All 88 IAU constellations.
    constellation_full_names_ =
    {
        {"And", "Andromeda"},
        {"Ant", "Antlia"},
        {"Aps", "Apus"},
        {"Aql", "Aquila"},
        {"Aqr", "Aquarius"},
        {"Ara", "Ara"},
        {"Ari", "Aries"},
        {"Aur", "Auriga"},
        {"Boo", "Boötes"},
        {"CMa", "Canis Major"},
        {"CMi", "Canis Minor"},
        {"CVn", "Canes Venatici"},
        {"Cae", "Caelum"},
        {"Cam", "Camelopardalis"},
        {"Cap", "Capricornus"},
        {"Car", "Carina"},
        {"Cas", "Cassiopeia"},
        {"Cen", "Centaurus"},
        {"Cep", "Cepheus"},
        {"Cet", "Cetus"},
        {"Cha", "Chamaeleon"},
        {"Cir", "Circinus"},
        {"Cnc", "Cancer"},
        {"Col", "Columba"},
        {"Com", "Coma Berenices"},
        {"CrA", "Corona Australis"},
        {"CrB", "Corona Borealis"},
        {"Crt", "Crater"},
        {"Cru", "Crux"},
        {"Crv", "Corvus"},
        {"Cyg", "Cygnus"},
        {"Del", "Delphinus"},
        {"Dor", "Dorado"},
        {"Dra", "Draco"},
        {"Equ", "Equuleus"},
        {"Eri", "Eridanus"},
        {"For", "Fornax"},
        {"Gem", "Gemini"},
        {"Gru", "Grus"},
        {"Her", "Hercules"},
        {"Hor", "Horologium"},
        {"Hya", "Hydra"},
        {"Hyi", "Hydrus"},
        {"Ind", "Indus"},
        {"LMi", "Leo Minor"},
        {"Lac", "Lacerta"},
        {"Leo", "Leo"},
        {"Lep", "Lepus"},
        {"Lib", "Libra"},
        {"Lup", "Lupus"},
        {"Lyn", "Lynx"},
        {"Lyr", "Lyra"},
        {"Men", "Mensa"},
        {"Mic", "Microscopium"},
        {"Mon", "Monoceros"},
        {"Mus", "Musca"},
        {"Nor", "Norma"},
        {"Oct", "Octans"},
        {"Oph", "Ophiuchus"},
        {"Ori", "Orion"},
        {"Pav", "Pavo"},
        {"Peg", "Pegasus"},
        {"Per", "Perseus"},
        {"Phe", "Phoenix"},
        {"Pic", "Pictor"},
        {"PsA", "Piscis Austrinus"},
        {"Psc", "Pisces"},
        {"Pup", "Puppis"},
        {"Pyx", "Pyxis"},
        {"Ret", "Reticulum"},
        {"Scl", "Sculptor"},
        {"Sco", "Scorpius"},
        {"Sct", "Scutum"},
        {"Ser", "Serpens"},
        {"Sex", "Sextans"},
        {"Sge", "Sagitta"},
        {"Sgr", "Sagittarius"},
        {"Tau", "Taurus"},
        {"Tel", "Telescopium"},
        {"TrA", "Triangulum Australe"},
        {"Tri", "Triangulum"},
        {"Tuc", "Tucana"},
        {"UMa", "Ursa Major"},
        {"UMi", "Ursa Minor"},
        {"Vel", "Vela"},
        {"Vir", "Virgo"},
        {"Vol", "Volans"},
        {"Vul", "Vulpecula"},
    };
}

} // namespace parallax::universe
