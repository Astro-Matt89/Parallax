/// @file data_archive.cpp
/// @brief DataArchive — implementation.

#include "observation/data_archive.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace parallax::observation
{

// =============================================================================
// Internal helpers
// =============================================================================

namespace
{

/// Schema version written to / expected in saved JSON files.
constexpr int kSchemaVersion = 1;

/// Convert a DataType enum value to its canonical string representation.
std::string_view data_type_to_string(DataType t)
{
    switch (t)
    {
        case DataType::PhotometricMeasurement: return "PhotometricMeasurement";
        case DataType::LightCurve:             return "LightCurve";
        case DataType::Spectrum:               return "Spectrum";
        case DataType::Image:                  return "Image";
        case DataType::SurveySourceList:       return "SurveySourceList";
        case DataType::Mock:                   return "Mock";
    }
    return "Mock"; // unreachable, but satisfies compilers without default:
}

/// Parse a DataType from its canonical string.  Falls back to DataType::Mock
/// for unknown strings and logs a warning to stderr.
DataType data_type_from_string(std::string_view s)
{
    if (s == "PhotometricMeasurement") { return DataType::PhotometricMeasurement; }
    if (s == "LightCurve")             { return DataType::LightCurve;             }
    if (s == "Spectrum")               { return DataType::Spectrum;               }
    if (s == "Image")                  { return DataType::Image;                  }
    if (s == "SurveySourceList")       { return DataType::SurveySourceList;       }
    if (s == "Mock")                   { return DataType::Mock;                   }

    // Unknown type string — fall back gracefully.
    spdlog::warn("[DataArchive] unknown DataType string \"{}\"; falling back to Mock",
                 std::string(s));
    return DataType::Mock;
}

/// Serialize a single DataRecord to a JSON object.
nlohmann::json record_to_json(const DataRecord& r)
{
    using json = nlohmann::json;

    json obj;
    obj["id"]                = r.id;
    obj["session_id"]        = r.session_id;
    obj["target_object_id"]  = r.target_object_id;
    obj["type"]              = data_type_to_string(r.type);
    obj["technique"]         = r.technique;
    obj["observation_jd"]    = r.observation_jd;
    obj["duration_hours"]    = r.duration_hours;
    obj["achieved_snr"]      = r.achieved_snr;

    json meas = json::object();
    for (const auto& [key, val] : r.measurements)
    {
        meas[key] = val;
    }
    obj["measurements"] = std::move(meas);

    json unc = json::object();
    for (const auto& [key, val] : r.uncertainties)
    {
        unc[key] = val;
    }
    obj["uncertainties"] = std::move(unc);

    // raw_data serialized as an array of integers even when empty.
    obj["raw_data"] = r.raw_data;

    return obj;
}

/// Deserialize a DataRecord from a JSON object.  Missing optional fields are
/// given sensible defaults; unknown DataType strings fall back to Mock.
DataRecord record_from_json(const nlohmann::json& obj)
{
    DataRecord r;
    r.id               = obj.value("id",               std::uint64_t{0});
    r.session_id       = obj.value("session_id",       std::uint64_t{0});
    r.target_object_id = obj.value("target_object_id", std::uint64_t{0});
    r.type             = data_type_from_string(obj.value("type", std::string{"Mock"}));
    r.technique        = obj.value("technique",        std::string{});
    r.observation_jd   = obj.value("observation_jd",   0.0);
    r.duration_hours   = obj.value("duration_hours",   0.0);
    r.achieved_snr     = obj.value("achieved_snr",     0.0);

    if (obj.contains("measurements"))
    {
        for (const auto& [key, val] : obj["measurements"].items())
        {
            r.measurements.emplace(key, val.get<double>());
        }
    }

    if (obj.contains("uncertainties"))
    {
        for (const auto& [key, val] : obj["uncertainties"].items())
        {
            r.uncertainties.emplace(key, val.get<float>());
        }
    }

    if (obj.contains("raw_data"))
    {
        r.raw_data = obj["raw_data"].get<std::vector<std::uint8_t>>();
    }

    return r;
}

} // anonymous namespace

// =============================================================================
// Mutation
// =============================================================================

void DataArchive::add(std::unique_ptr<DataRecord> record)
{
    if (!record)
    {
        return;
    }
    // Overwrite any existing record with the same ID.
    m_records.insert_or_assign(record->id, std::move(record));
}

void DataArchive::clear()
{
    m_records.clear();
}

bool DataArchive::remove_record(std::uint64_t record_id)
{
    const auto erased = m_records.erase(record_id);
    return erased > 0;
}

// =============================================================================
// Queries
// =============================================================================

const DataRecord* DataArchive::get_by_id(std::uint64_t record_id) const
{
    const auto it = m_records.find(record_id);
    if (it == m_records.end())
    {
        return nullptr;
    }
    return it->second.get();
}

std::vector<const DataRecord*> DataArchive::get_by_target(std::uint64_t object_id) const
{
    std::vector<const DataRecord*> result;
    for (const auto& [id, rec] : m_records)
    {
        if (rec->target_object_id == object_id)
        {
            result.push_back(rec.get());
        }
    }
    return result;
}

std::vector<const DataRecord*> DataArchive::get_all() const
{
    std::vector<const DataRecord*> result;
    result.reserve(m_records.size());
    for (const auto& [id, rec] : m_records)
    {
        result.push_back(rec.get());
    }
    return result;
}

std::size_t DataArchive::size() const noexcept
{
    return m_records.size();
}

// =============================================================================
// Persistence — save
// =============================================================================

bool DataArchive::save(const std::filesystem::path& path) const
{
    try
    {
        using json = nlohmann::json;

        json root;
        root["schema"]  = "parallax.data_archive";
        root["version"] = kSchemaVersion;

        json records_arr = json::array();
        for (const auto& [id, rec] : m_records)
        {
            records_arr.push_back(record_to_json(*rec));
        }
        root["records"] = std::move(records_arr);

        std::ofstream ofs(path);
        if (!ofs.is_open())
        {
            return false;
        }
        ofs << root.dump(4);
        return ofs.good();
    }
    catch (...)
    {
        return false;
    }
}

// =============================================================================
// Persistence — load
// =============================================================================

bool DataArchive::load(const std::filesystem::path& path)
{
    try
    {
        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            return false;
        }

        using json = nlohmann::json;
        const json root = json::parse(ifs);

        const int version = root.value("version", 0);
        if (version != kSchemaVersion)
        {
            return false;
        }

        clear();

        if (root.contains("records"))
        {
            for (const auto& obj : root["records"])
            {
                auto rec = std::make_unique<DataRecord>(record_from_json(obj));
                const std::uint64_t key = rec->id;
                m_records.emplace(key, std::move(rec));
            }
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace parallax::observation
