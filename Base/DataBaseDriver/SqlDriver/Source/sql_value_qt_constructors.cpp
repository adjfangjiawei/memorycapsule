// SqlDriver/Source/sql_value_qt_constructors.cpp
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QTime>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    SqlValue::SqlValue(const QByteArray& val) : m_value_storage(std::vector<unsigned char>(reinterpret_cast<const unsigned char*>(val.constData()), reinterpret_cast<const unsigned char*>(val.constData() + val.size()))), m_current_type_enum(SqlValueType::ByteArray) {
    }

    SqlValue::SqlValue(const QDate& val) : m_value_storage(val), m_current_type_enum(val.isValid() ? SqlValueType::Date : SqlValueType::Null) {
        if (!val.isValid()) {
            m_value_storage = std::monostate{};
        }
    }

    SqlValue::SqlValue(const QTime& val) : m_value_storage(val), m_current_type_enum(val.isValid() ? SqlValueType::Time : SqlValueType::Null) {
        if (!val.isValid()) {
            m_value_storage = std::monostate{};
        }
    }

    SqlValue::SqlValue(const QDateTime& val) : m_value_storage(val), m_current_type_enum(val.isValid() ? SqlValueType::DateTime : SqlValueType::Null) {
        if (!val.isValid()) {
            m_value_storage = std::monostate{};
        }
    }

}  // namespace cpporm_sqldriver