// SqlDriver/Source/sql_value_chrono_constructors.cpp
#include <chrono>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    // 辅助函数声明（如果它们在 helpers 文件中）
    namespace detail {
        bool isValidChronoDate(const SqlValue::ChronoDate& cd);
        bool isValidChronoDateTime(const SqlValue::ChronoDateTime& cdt);
    }  // namespace detail

    SqlValue::SqlValue(const ChronoDate& val) : m_value_storage(val), m_current_type_enum(detail::isValidChronoDate(val) ? SqlValueType::Date : SqlValueType::Null) {
        if (!detail::isValidChronoDate(val)) {
            m_value_storage = std::monostate{};
        }
    }

    SqlValue::SqlValue(const ChronoTime& val) : m_value_storage(val), m_current_type_enum(SqlValueType::Time) {
    }

    SqlValue::SqlValue(const ChronoDateTime& val) : m_value_storage(val), m_current_type_enum(detail::isValidChronoDateTime(val) ? SqlValueType::DateTime : SqlValueType::Null) {
        if (!detail::isValidChronoDateTime(val)) {
            m_value_storage = std::monostate{};
        }
    }

}  // namespace cpporm_sqldriver