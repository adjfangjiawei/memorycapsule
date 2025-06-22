// SqlDriver/Source/sql_record.cpp
#include "sqldriver/sql_record.h"

#include <algorithm>  // For std::find_if
#include <stdexcept>  // For std::out_of_range

namespace cpporm_sqldriver {

    SqlRecord::SqlRecord() = default;
    SqlRecord::~SqlRecord() = default;

    // --- Copy and Move semantics ---
    SqlRecord::SqlRecord(const SqlRecord& other) : m_fields(other.m_fields) {
    }

    SqlRecord& SqlRecord::operator=(const SqlRecord& other) {
        if (this != &other) {
            m_fields = other.m_fields;
        }
        return *this;
    }

    SqlRecord::SqlRecord(SqlRecord&& other) noexcept : m_fields(std::move(other.m_fields)) {
    }

    SqlRecord& SqlRecord::operator=(SqlRecord&& other) noexcept {
        if (this != &other) {
            m_fields = std::move(other.m_fields);
        }
        return *this;
    }

    // --- Status and Count ---
    bool SqlRecord::isEmpty() const {
        return m_fields.empty();
    }

    int SqlRecord::count() const {
        return static_cast<int>(m_fields.size());
    }

    // --- Field access by index ---
    SqlField SqlRecord::field(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::field: index out of bounds");
            return SqlField();  // Return an invalid/empty field
        }
        return m_fields[static_cast<size_t>(index)];
    }

    std::string SqlRecord::fieldName(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::fieldName: index out of bounds");
            return "";
        }
        return m_fields[static_cast<size_t>(index)].name();
    }

    SqlValue SqlRecord::value(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::value: index out of bounds");
            return SqlValue();  // Return null SqlValue
        }
        return m_fields[static_cast<size_t>(index)].value();
    }

    bool SqlRecord::isNull(int index) const {
        if (index < 0 || static_cast<size_t>(index) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::isNull: index out of bounds");
            return true;  // Treat out-of-bounds as null-like for safety
        }
        return m_fields[static_cast<size_t>(index)].isNullInValue();
    }

    void SqlRecord::setValue(int index, const SqlValue& val) {
        if (index < 0 || static_cast<size_t>(index) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::setValue: index out of bounds");
            return;
        }
        m_fields[static_cast<size_t>(index)].setValue(val);
    }

    void SqlRecord::setNull(int index) {
        if (index < 0 || static_cast<size_t>(index) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::setNull: index out of bounds");
            return;
        }
        m_fields[static_cast<size_t>(index)].clearValue();  // Or setValue(SqlValue());
    }

    // --- Field access by name ---
    int SqlRecord::indexOf(const std::string& name) const {
        for (size_t i = 0; i < m_fields.size(); ++i) {
            // Case-sensitive comparison. For case-insensitive, convert both to lower/upper.
            if (m_fields[i].name() == name) {
                return static_cast<int>(i);
            }
        }
        return -1;  // Not found
    }

    SqlField SqlRecord::field(const std::string& name) const {
        int idx = indexOf(name);
        if (idx != -1) {
            return m_fields[static_cast<size_t>(idx)];
        }
        return SqlField();  // Return an invalid/empty field
    }

    SqlValue SqlRecord::value(const std::string& name) const {
        int idx = indexOf(name);
        if (idx != -1) {
            return m_fields[static_cast<size_t>(idx)].value();
        }
        return SqlValue();  // Return null SqlValue
    }

    bool SqlRecord::isNull(const std::string& name) const {
        int idx = indexOf(name);
        if (idx != -1) {
            return m_fields[static_cast<size_t>(idx)].isNullInValue();
        }
        return true;  // Not found, treat as null
    }

    void SqlRecord::setValue(const std::string& name, const SqlValue& val) {
        int idx = indexOf(name);
        if (idx != -1) {
            m_fields[static_cast<size_t>(idx)].setValue(val);
        }
        // Else: field not found, could throw or silently ignore
    }

    void SqlRecord::setNull(const std::string& name) {
        int idx = indexOf(name);
        if (idx != -1) {
            m_fields[static_cast<size_t>(idx)].clearValue();  // Or setValue(SqlValue());
        }
        // Else: field not found
    }

    bool SqlRecord::contains(const std::string& name) const {
        return indexOf(name) != -1;
    }

    // --- Modification ---
    void SqlRecord::append(const SqlField& field) {
        m_fields.push_back(field);
    }

    void SqlRecord::insert(int pos, const SqlField& field) {
        if (pos < 0 || static_cast<size_t>(pos) > m_fields.size()) {  // Allow insert at end (pos == size())
            // Or throw std::out_of_range("SqlRecord::insert: position out of bounds");
            if (pos == count()) {  // Append if pos is at the end
                m_fields.push_back(field);
            }
            return;
        }
        m_fields.insert(m_fields.begin() + pos, field);
    }

    void SqlRecord::remove(int pos) {
        if (pos < 0 || static_cast<size_t>(pos) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::remove: position out of bounds");
            return;
        }
        m_fields.erase(m_fields.begin() + pos);
    }

    void SqlRecord::replace(int pos, const SqlField& field) {
        if (pos < 0 || static_cast<size_t>(pos) >= m_fields.size()) {
            // Or throw std::out_of_range("SqlRecord::replace: position out of bounds");
            return;
        }
        m_fields[static_cast<size_t>(pos)] = field;
    }

    void SqlRecord::clear() {
        m_fields.clear();
    }

}  // namespace cpporm_sqldriver