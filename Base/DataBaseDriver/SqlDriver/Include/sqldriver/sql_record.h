// SqlDriver/Include/sqldriver/sql_record.h
#pragma once
#include <memory>  // For std::unique_ptr in PImpl if used, or for direct members
#include <optional>
#include <string>
#include <vector>

#include "sqldriver/sql_field.h"  // SqlField is part of SqlRecord
#include "sqldriver/sql_value.h"  // For SqlValue as return type of value()

namespace cpporm_sqldriver {

    class SqlRecord {
      public:
        SqlRecord();
        ~SqlRecord();

        // Copy and Move semantics
        SqlRecord(const SqlRecord& other);
        SqlRecord& operator=(const SqlRecord& other);
        SqlRecord(SqlRecord&& other) noexcept;
        SqlRecord& operator=(SqlRecord&& other) noexcept;

        // Status and Count
        bool isEmpty() const;
        int count() const;

        // Field access by index
        SqlField field(int index) const;  // Returns a copy of the SqlField object
        std::string fieldName(int index) const;
        SqlValue value(int index) const;
        bool isNull(int index) const;
        void setValue(int index, const SqlValue& val);
        void setNull(int index);

        // Field access by name
        SqlField field(const std::string& name) const;
        SqlValue value(const std::string& name) const;
        bool isNull(const std::string& name) const;
        void setValue(const std::string& name, const SqlValue& val);
        void setNull(const std::string& name);

        // Lookup and containment
        int indexOf(const std::string& name) const;  // Returns -1 if not found
        bool contains(const std::string& name) const;

        // Modification (primarily for driver internal use or manual record construction)
        void append(const SqlField& field);
        void insert(int pos, const SqlField& field);
        void remove(int pos);
        void replace(int pos, const SqlField& field);  // Replaces field at pos
        void clear();                                  // Removes all fields

        // Direct access to fields vector (use with caution)
        // const std::vector<SqlField>& fields() const; // Might be useful for iteration

      private:
        // Direct member implementation (no PImpl as per last directive)
        std::vector<SqlField> m_fields;
        // For faster name lookup, an optional map could be used, built on demand or kept in sync.
        // mutable std::map<std::string, int> m_name_to_index_cache;
        // mutable bool m_cache_is_dirty;
        // void rebuildNameCache() const;
    };

}  // namespace cpporm_sqldriver