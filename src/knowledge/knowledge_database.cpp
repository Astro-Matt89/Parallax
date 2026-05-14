/// @file knowledge_database.cpp
/// @brief KnowledgeDatabase — core implementation (no Universe dependency).
///
/// initialize_from_historical_catalogs is in knowledge_database_init.cpp to
/// keep this translation unit free of the full Universe header chain, allowing
/// unit tests to link against this file without pulling in the Universe stack.

#include "knowledge/knowledge_database.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace parallax::knowledge
{

// =============================================================================
// Internal constants
// =============================================================================

namespace
{
/// Schema version written to saved JSON files.
constexpr int kSchemaVersion = 1;

} // anonymous namespace

// =============================================================================
// Private helpers
// =============================================================================

ObjectKnowledge& KnowledgeDatabase::ensure_entry(std::uint64_t id)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end())
    {
        ObjectKnowledge ok;
        ok.object_id = id;
        auto [inserted_it, _] = m_entries.emplace(id, std::move(ok));
        return inserted_it->second;
    }
    return it->second;
}

// =============================================================================
// Queries
// =============================================================================

bool KnowledgeDatabase::is_known(std::uint64_t id) const
{
    return m_entries.contains(id);
}

KnowledgeLevel KnowledgeDatabase::get_level(std::uint64_t id) const
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end())
    {
        return KnowledgeLevel::Unknown;
    }
    return it->second.current_level;
}

bool KnowledgeDatabase::is_confirmed(std::uint64_t id) const
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end())
    {
        return false;
    }
    return it->second.is_confirmed;
}

std::optional<MeasurementRecord>
KnowledgeDatabase::get_measurement(std::uint64_t id, std::string_view property) const
{
    const auto eit = m_entries.find(id);
    if (eit == m_entries.end())
    {
        return std::nullopt;
    }
    const auto& meas = eit->second.measurements;
    const auto  mit  = meas.find(std::string(property));
    if (mit == meas.end())
    {
        return std::nullopt;
    }
    return mit->second;
}

std::vector<std::uint64_t> KnowledgeDatabase::get_all_known_ids() const
{
    std::vector<std::uint64_t> ids;
    ids.reserve(m_entries.size());
    for (const auto& [id, _] : m_entries)
    {
        ids.push_back(id);
    }
    return ids;
}

// =============================================================================
// Mutation
// =============================================================================

void KnowledgeDatabase::add_detection(std::uint64_t id, std::uint64_t session_id)
{
    ObjectKnowledge& ok = ensure_entry(id);

    const auto it = std::find(ok.session_ids.begin(), ok.session_ids.end(), session_id);
    if (it == ok.session_ids.end())
    {
        ok.session_ids.push_back(session_id);
        ok.independent_detections++;
    }

    if (ok.independent_detections >= 2u)
    {
        ok.is_confirmed = true;
    }
}

void KnowledgeDatabase::record_measurement(std::uint64_t    id,
                                           std::string_view property,
                                           double           value,
                                           double           uncertainty,
                                           double           snr,
                                           std::uint64_t    session_id)
{
    ObjectKnowledge& ok = ensure_entry(id);

    MeasurementRecord rec;
    rec.value          = value;
    rec.uncertainty    = static_cast<float>(uncertainty);
    rec.snr            = static_cast<float>(snr);
    rec.session_id     = session_id;
    rec.observation_jd = 0.0;

    ok.measurements.insert_or_assign(std::string(property), rec);

    const auto sit = std::find(ok.session_ids.begin(), ok.session_ids.end(), session_id);
    if (sit == ok.session_ids.end())
    {
        ok.session_ids.push_back(session_id);
    }
}

// =============================================================================
// Persistence — save
// =============================================================================

bool KnowledgeDatabase::save(const std::filesystem::path& path) const
{
    try
    {
        using json = nlohmann::json;

        json root;
        root["version"] = kSchemaVersion;

        json entries_arr = json::array();

        for (const auto& [id, ok] : m_entries)
        {
            json entry;
            entry["object_id"]              = ok.object_id;
            entry["current_level"]          = static_cast<std::uint8_t>(ok.current_level);
            entry["independent_detections"] = ok.independent_detections;
            entry["is_confirmed"]           = ok.is_confirmed;
            entry["is_historical"]          = ok.is_historical;
            entry["session_ids"]            = ok.session_ids;

            json meas_obj = json::object();
            for (const auto& [name, rec] : ok.measurements)
            {
                json m;
                m["value"]          = rec.value;
                m["uncertainty"]    = rec.uncertainty;
                m["snr"]            = rec.snr;
                m["session_id"]     = rec.session_id;
                m["observation_jd"] = rec.observation_jd;
                meas_obj[name]      = std::move(m);
            }
            entry["measurements"] = std::move(meas_obj);

            entries_arr.push_back(std::move(entry));
        }

        root["entries"] = std::move(entries_arr);

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

bool KnowledgeDatabase::load(const std::filesystem::path& path)
{
    try
    {
        std::ifstream ifs(path);
        if (!ifs.is_open())
        {
            return false;
        }

        using json = nlohmann::json;
        json root  = json::parse(ifs);

        const int version = root.value("version", 1);
        if (version != kSchemaVersion)
        {
            return false;
        }

        m_entries.clear();

        const auto& entries_arr = root["entries"];
        for (const auto& entry : entries_arr)
        {
            ObjectKnowledge ok;
            ok.object_id              = entry.value("object_id",              std::uint64_t{0});
            ok.current_level          = static_cast<KnowledgeLevel>(
                                            entry.value("current_level",      std::uint8_t{0}));
            ok.independent_detections = entry.value("independent_detections", std::uint32_t{0});
            ok.is_confirmed           = entry.value("is_confirmed",           false);
            ok.is_historical          = entry.value("is_historical",          false);

            if (entry.contains("session_ids"))
            {
                ok.session_ids = entry["session_ids"].get<std::vector<std::uint64_t>>();
            }

            if (entry.contains("measurements"))
            {
                for (const auto& [name, m] : entry["measurements"].items())
                {
                    MeasurementRecord rec;
                    rec.value          = m.value("value",          0.0);
                    rec.uncertainty    = m.value("uncertainty",    0.0f);
                    rec.snr            = m.value("snr",            0.0f);
                    rec.session_id     = m.value("session_id",     std::uint64_t{0});
                    rec.observation_jd = m.value("observation_jd", 0.0);
                    ok.measurements.emplace(name, rec);
                }
            }

            m_entries.emplace(ok.object_id, std::move(ok));
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace parallax::knowledge
