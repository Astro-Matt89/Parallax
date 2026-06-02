#pragma once

/// @file data_archive.hpp
/// @brief DataArchive — persistent storage for all DataRecord objects produced by observation sessions.

#include "observation/data_record.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace parallax::observation
{

/// @brief Persistent storage container for all DataRecord objects.
///
/// Records are owned by the archive (via unique_ptr) and keyed by their ID.
/// Accessors return non-owning raw pointers; the archive retains ownership.
///
/// Thread-safety: not thread-safe; callers must synchronise externally if needed.
class DataArchive
{
public:
    DataArchive()  = default;
    ~DataArchive() = default;

    DataArchive(const DataArchive&)            = delete;
    DataArchive& operator=(const DataArchive&) = delete;

    DataArchive(DataArchive&&)                 = default;
    DataArchive& operator=(DataArchive&&)      = default;

    // -------------------------------------------------------------------------
    // Mutation
    // -------------------------------------------------------------------------

    /// @brief Take ownership of @p record and store it by its ID.
    ///
    /// Null pointers are silently ignored.  If a record with the same ID already
    /// exists it is overwritten — the new record supersedes the old one.
    void add(std::unique_ptr<DataRecord> record);

    /// @brief Remove all records from the archive.
    void clear();

    /// @brief Remove a record by ID.
    /// @return True if a record existed and was removed.
    [[nodiscard]] bool remove_record(std::uint64_t record_id);

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /// @brief Return a pointer to the record with @p record_id, or nullptr if absent.
    [[nodiscard]] const DataRecord* get_by_id(std::uint64_t record_id) const;

    /// @brief Return all records whose @c target_object_id matches @p object_id.
    ///
    /// Linear scan; order is not specified.
    [[nodiscard]] std::vector<const DataRecord*> get_by_target(std::uint64_t object_id) const;

    /// @brief Return every record in the archive.
    ///
    /// Linear scan; order is not specified.
    [[nodiscard]] std::vector<const DataRecord*> get_all() const;

    /// @brief Return the number of records currently stored.
    [[nodiscard]] std::size_t size() const noexcept;

    // -------------------------------------------------------------------------
    // Persistence
    // -------------------------------------------------------------------------

    /// @brief Serialize the archive to a pretty-printed JSON file at @p path.
    ///
    /// Returns false on any I/O or serialization error.  Does not throw past this boundary.
    [[nodiscard]] bool save(const std::filesystem::path& path) const;

    /// @brief Deserialize the archive from a JSON file at @p path.
    ///
    /// Clears the current contents before loading.  Returns false if the file
    /// does not exist, cannot be parsed, or the schema version is unrecognised.
    /// Does not throw past this boundary.
    [[nodiscard]] bool load(const std::filesystem::path& path);

private:
    /// Record ID → owned DataRecord.
    std::unordered_map<std::uint64_t, std::unique_ptr<DataRecord>> m_records;
};

} // namespace parallax::observation
