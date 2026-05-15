#pragma once

/// @file knowledge_database.hpp
/// @brief KnowledgeDatabase — the player's complete knowledge state about the universe.
///
/// Separates what the player knows (KnowledgeDatabase) from what exists (Universe).
/// Real catalog objects are pre-populated at L3 (Characterized) on initialization.
/// Procedural objects appear only after the player discovers them.

#include "knowledge/knowledge_level.hpp"
#include "knowledge/measurement_record.hpp"
#include "knowledge/object_knowledge.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace parallax::universe { class Universe; }

namespace parallax::knowledge
{

/// @brief Persistent store of everything the player has learned about every object.
///
/// Thread-safety: not thread-safe; callers must synchronise externally if needed.
class KnowledgeDatabase
{
public:
    KnowledgeDatabase()  = default;
    ~KnowledgeDatabase() = default;

    KnowledgeDatabase(const KnowledgeDatabase&)            = delete;
    KnowledgeDatabase& operator=(const KnowledgeDatabase&) = delete;

    KnowledgeDatabase(KnowledgeDatabase&&)                 = default;
    KnowledgeDatabase& operator=(KnowledgeDatabase&&)      = default;

    // -------------------------------------------------------------------------
    // Initialization
    // -------------------------------------------------------------------------

    /// @brief Pre-populate entries for all real catalog objects at L3 (Characterized).
    ///
    /// Iterates the Universe via a full-sky query, creates ObjectKnowledge for every
    /// object where is_real() is true, marks it is_historical = true and is_confirmed = true,
    /// and records measurements extracted directly from CelestialObject fields.
    /// Procedural objects are skipped.
    ///
    /// @param universe A fully loaded Universe (load_catalogs + update already called).
    void initialize_from_historical_catalogs(const universe::Universe& universe);

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /// @brief Returns true if the object with @p id has any entry in the database.
    [[nodiscard]] bool is_known(std::uint64_t id) const;

    /// @brief Returns the current knowledge level for @p id.
    ///
    /// Returns KnowledgeLevel::Unknown when the object is not in the database.
    [[nodiscard]] KnowledgeLevel get_level(std::uint64_t id) const;

    /// @brief Returns true if the object has been independently detected at least twice.
    ///
    /// Returns false when the object is not in the database.
    [[nodiscard]] bool is_confirmed(std::uint64_t id) const;

    /// @brief Returns the most recent measurement for a named property, or nullopt.
    [[nodiscard]] std::optional<MeasurementRecord>
        get_measurement(std::uint64_t id, std::string_view property) const;

    /// @brief Returns all object IDs currently known to the player.
    [[nodiscard]] std::vector<std::uint64_t> get_all_known_ids() const;

    // -------------------------------------------------------------------------
    // Mutation
    // -------------------------------------------------------------------------

    /// @brief Record an independent detection of an object.
    ///
    /// Increments the detection counter, appends @p session_id to the object's
    /// session list (if not already present), and sets is_confirmed = true once
    /// the counter reaches 2 or more.  Creates the ObjectKnowledge entry if absent.
    void add_detection(std::uint64_t id, std::uint64_t session_id);

    /// @brief Record a scalar measurement for a named property.
    ///
    /// Creates an ObjectKnowledge entry if absent.  If a measurement for the same
    /// property already exists it is replaced by the new record (higher SNR wins
    /// in practice, but the database stores the latest unconditionally for now).
    ///
    /// @param id          Universe Engine object ID.
    /// @param property    Property name (must match PropertyRegistry key).
    /// @param value       Measured scalar value (units defined by PropertyDescriptor).
    /// @param uncertainty 1-sigma uncertainty of the measurement.
    /// @param snr         Signal-to-noise ratio of the observation.
    /// @param session_id  ID of the ObservationSession that produced this record.
    void record_measurement(std::uint64_t  id,
                            std::string_view property,
                            double           value,
                            double           uncertainty,
                            double           snr,
                            std::uint64_t    session_id);

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    /// @brief Serialize the entire database to a JSON file at @p path.
    ///
    /// Writes pretty-printed JSON; returns false on any I/O or serialization error.
    /// Does not throw past this boundary.
    [[nodiscard]] bool save(const std::filesystem::path& path) const;

    /// @brief Deserialize the database from a JSON file at @p path.
    ///
    /// Replaces the current contents.  Tolerates missing optional fields; returns
    /// false on any I/O or parse error.  Does not throw past this boundary.
    [[nodiscard]] bool load(const std::filesystem::path& path);

private:
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    /// @brief Ensure an entry exists for @p id and return a reference to it.
    ObjectKnowledge& ensure_entry(std::uint64_t id);

    // -------------------------------------------------------------------------
    // Storage
    // -------------------------------------------------------------------------

    /// Object ID → knowledge entry.
    std::unordered_map<std::uint64_t, ObjectKnowledge> m_entries;
};

} // namespace parallax::knowledge
