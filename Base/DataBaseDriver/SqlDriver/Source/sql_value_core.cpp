// SqlDriver/Source/sql_value_core.cpp
#include <QDate>  // For isValid checks in constructor/isValid
#include <QDateTime>
#include <QTime>
#include <any>
#include <cstring>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    // --- 核心构造函数 ---
    SqlValue::SqlValue() : m_value_storage(std::monostate{}), m_current_type_enum(SqlValueType::Null), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(std::nullptr_t) : m_value_storage(std::monostate{}), m_current_type_enum(SqlValueType::Null), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(bool val) : m_value_storage(val), m_current_type_enum(SqlValueType::Bool), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(int8_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::Int8), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(uint8_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::UInt8), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(int16_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::Int16), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(uint16_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::UInt16), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(int32_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::Int32), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(uint32_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::UInt32), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(int64_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::Int64), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(uint64_t val) : m_value_storage(val), m_current_type_enum(SqlValueType::UInt64), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(float val) : m_value_storage(val), m_current_type_enum(SqlValueType::Float), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(double val) : m_value_storage(val), m_current_type_enum(SqlValueType::Double), m_lob_size_hint(-1) {
    }
    SqlValue::SqlValue(long double val) : m_value_storage(val), m_current_type_enum(SqlValueType::LongDouble), m_lob_size_hint(-1) {
    }

    SqlValue::SqlValue(const char* val, SqlValueType type_hint) : m_current_type_enum(type_hint), m_lob_size_hint(-1) {
        if (val) {
            if (type_hint == SqlValueType::ByteArray || type_hint == SqlValueType::BinaryLargeObject) {
                m_value_storage = std::vector<unsigned char>(reinterpret_cast<const unsigned char*>(val), reinterpret_cast<const unsigned char*>(val + std::strlen(val)));
                m_current_type_enum = SqlValueType::ByteArray;
            } else {
                m_value_storage = std::string(val);
                // m_current_type_enum 保持用户提供的 type_hint
            }
        } else {
            m_value_storage = std::monostate{};
            m_current_type_enum = SqlValueType::Null;
        }
    }

    SqlValue::SqlValue(const std::string& val, SqlValueType type_hint) : m_current_type_enum(type_hint), m_lob_size_hint(-1) {
        if (type_hint == SqlValueType::ByteArray || type_hint == SqlValueType::BinaryLargeObject) {
            m_value_storage = std::vector<unsigned char>(val.begin(), val.end());
            m_current_type_enum = SqlValueType::ByteArray;
        } else {
            m_value_storage = val;
            // m_current_type_enum 保持用户提供的 type_hint
        }
    }

    SqlValue::SqlValue(const std::vector<unsigned char>& val) : m_value_storage(val), m_current_type_enum(SqlValueType::ByteArray), m_lob_size_hint(-1) {
    }

    // --- 拷贝和移动语义 ---
    SqlValue::SqlValue(const SqlValue& other) : m_value_storage(other.m_value_storage), m_current_type_enum(other.m_current_type_enum), m_driver_type_name_cache(other.m_driver_type_name_cache), m_lob_size_hint(other.m_lob_size_hint) {
    }

    SqlValue& SqlValue::operator=(const SqlValue& other) {
        if (this != &other) {
            m_value_storage = other.m_value_storage;
            m_current_type_enum = other.m_current_type_enum;
            m_driver_type_name_cache = other.m_driver_type_name_cache;
            m_lob_size_hint = other.m_lob_size_hint;
        }
        return *this;
    }

    SqlValue::SqlValue(SqlValue&& other) noexcept : m_value_storage(std::move(other.m_value_storage)), m_current_type_enum(other.m_current_type_enum), m_driver_type_name_cache(std::move(other.m_driver_type_name_cache)), m_lob_size_hint(other.m_lob_size_hint) {
        other.m_current_type_enum = SqlValueType::Null;
        other.m_value_storage = std::monostate{};
        other.m_lob_size_hint = -1;
    }

    SqlValue& SqlValue::operator=(SqlValue&& other) noexcept {
        if (this != &other) {
            m_value_storage = std::move(other.m_value_storage);
            m_current_type_enum = other.m_current_type_enum;
            m_driver_type_name_cache = std::move(other.m_driver_type_name_cache);
            m_lob_size_hint = other.m_lob_size_hint;
            other.m_current_type_enum = SqlValueType::Null;
            other.m_value_storage = std::monostate{};
            other.m_lob_size_hint = -1;
        }
        return *this;
    }

    SqlValue::~SqlValue() = default;

    bool SqlValue::isNull() const {
        return std::holds_alternative<std::monostate>(m_value_storage);
    }

    bool SqlValue::isValid() const {
        if (isNull()) return false;
        bool structurally_valid = true;
        std::visit(
            [&structurally_valid](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, QDate>) {
                    if (arg.isNull())
                        structurally_valid = false;
                    else
                        structurally_valid = arg.isValid();
                } else if constexpr (std::is_same_v<T, QTime>) {
                    if (arg.isNull())
                        structurally_valid = false;
                    else
                        structurally_valid = arg.isValid();
                } else if constexpr (std::is_same_v<T, QDateTime>) {
                    if (arg.isNull())
                        structurally_valid = false;
                    else
                        structurally_valid = arg.isValid();
                } else if constexpr (std::is_same_v<T, ChronoDate>) {
                    structurally_valid = arg.ok();
                } else if constexpr (std::is_same_v<T, InputStreamPtr>) {
                    structurally_valid = (arg != nullptr);
                } else if constexpr (std::is_same_v<T, std::any>) {
                    structurally_valid = arg.has_value();
                }
            },
            m_value_storage);
        return structurally_valid;
    }

    SqlValueType SqlValue::type() const {
        return m_current_type_enum;
    }

    void SqlValue::updateCurrentTypeEnumFromStorage() {
        std::visit(
            [this](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    m_current_type_enum = SqlValueType::Null;
                else if constexpr (std::is_same_v<T, bool>)
                    m_current_type_enum = SqlValueType::Bool;
                else if constexpr (std::is_same_v<T, int8_t>)
                    m_current_type_enum = SqlValueType::Int8;
                else if constexpr (std::is_same_v<T, uint8_t>)
                    m_current_type_enum = SqlValueType::UInt8;
                else if constexpr (std::is_same_v<T, int16_t>)
                    m_current_type_enum = SqlValueType::Int16;
                else if constexpr (std::is_same_v<T, uint16_t>)
                    m_current_type_enum = SqlValueType::UInt16;
                else if constexpr (std::is_same_v<T, int32_t>)
                    m_current_type_enum = SqlValueType::Int32;
                else if constexpr (std::is_same_v<T, uint32_t>)
                    m_current_type_enum = SqlValueType::UInt32;
                else if constexpr (std::is_same_v<T, int64_t>)
                    m_current_type_enum = SqlValueType::Int64;
                else if constexpr (std::is_same_v<T, uint64_t>)
                    m_current_type_enum = SqlValueType::UInt64;
                else if constexpr (std::is_same_v<T, float>)
                    m_current_type_enum = SqlValueType::Float;
                else if constexpr (std::is_same_v<T, double>)
                    m_current_type_enum = SqlValueType::Double;
                else if constexpr (std::is_same_v<T, long double>)
                    m_current_type_enum = SqlValueType::LongDouble;
                else if constexpr (std::is_same_v<T, std::string>) {
                    // 保持 m_current_type_enum 的原样，因为它可能已经是 FixedString, CLOB, Json 等
                    if (m_current_type_enum != SqlValueType::FixedString && m_current_type_enum != SqlValueType::CharacterLargeObject && m_current_type_enum != SqlValueType::Json && m_current_type_enum != SqlValueType::Xml && m_current_type_enum != SqlValueType::Decimal &&
                        m_current_type_enum != SqlValueType::Numeric &&
                        // 也检查Date/Time/DateTime/Timestamp，因为它们可能从字符串构造
                        m_current_type_enum != SqlValueType::Date && m_current_type_enum != SqlValueType::Time && m_current_type_enum != SqlValueType::DateTime && m_current_type_enum != SqlValueType::Timestamp && m_current_type_enum != SqlValueType::Interval) {
                        m_current_type_enum = SqlValueType::String;
                    }
                } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                    if (m_current_type_enum != SqlValueType::BinaryLargeObject) {
                        m_current_type_enum = SqlValueType::ByteArray;
                    }
                } else if constexpr (std::is_same_v<T, InputStreamPtr>) {
                    // m_current_type_enum 应该已在构造时被正确设置为 BinaryLargeObject 或 CharacterLargeObject
                } else if constexpr (std::is_same_v<T, QDate>)
                    m_current_type_enum = SqlValueType::Date;
                else if constexpr (std::is_same_v<T, QTime>)
                    m_current_type_enum = SqlValueType::Time;
                else if constexpr (std::is_same_v<T, QDateTime>) {
                    if (m_current_type_enum != SqlValueType::Timestamp) {
                        m_current_type_enum = SqlValueType::DateTime;
                    }
                } else if constexpr (std::is_same_v<T, ChronoDate>)
                    m_current_type_enum = SqlValueType::Date;
                else if constexpr (std::is_same_v<T, ChronoTime>)
                    m_current_type_enum = SqlValueType::Time;
                else if constexpr (std::is_same_v<T, ChronoDateTime>) {
                    if (m_current_type_enum != SqlValueType::Timestamp) {
                        m_current_type_enum = SqlValueType::DateTime;
                    }
                } else if constexpr (std::is_same_v<T, std::any>)
                    m_current_type_enum = SqlValueType::Custom;
                else
                    m_current_type_enum = SqlValueType::Unknown;
            },
            m_value_storage);
    }

    const char* SqlValue::typeName() const {
        switch (m_current_type_enum) {
            case SqlValueType::Null:
                return "Null";
            case SqlValueType::Bool:
                return "Bool";
            case SqlValueType::Int8:
                return "Int8";
            case SqlValueType::UInt8:
                return "UInt8";
            case SqlValueType::Int16:
                return "Int16";
            case SqlValueType::UInt16:
                return "UInt16";
            case SqlValueType::Int32:
                return "Int32";
            case SqlValueType::UInt32:
                return "UInt32";
            case SqlValueType::Int64:
                return "Int64";
            case SqlValueType::UInt64:
                return "UInt64";
            case SqlValueType::Float:
                return "Float";
            case SqlValueType::Double:
                return "Double";
            case SqlValueType::LongDouble:
                return "LongDouble";
            case SqlValueType::String:
                return "String";
            case SqlValueType::FixedString:
                return "FixedString";
            case SqlValueType::ByteArray:
                return "ByteArray";
            case SqlValueType::BinaryLargeObject:
                return "BLOB";
            case SqlValueType::CharacterLargeObject:
                return "CLOB";
            case SqlValueType::Date:
                return "Date";
            case SqlValueType::Time:
                return "Time";
            case SqlValueType::DateTime:
                return "DateTime";
            case SqlValueType::Timestamp:
                return "Timestamp";
            case SqlValueType::Interval:
                return "Interval";
            case SqlValueType::Decimal:
                return "Decimal";
            case SqlValueType::Numeric:
                return "Numeric";
            case SqlValueType::Json:
                return "Json";
            case SqlValueType::Xml:
                return "Xml";
            case SqlValueType::Array:
                return "Array";
            case SqlValueType::RowId:
                return "RowId";
            case SqlValueType::Custom:
                {
                    if (std::holds_alternative<std::any>(m_value_storage)) {
                        const std::any& a = std::get<std::any>(m_value_storage);
                        if (a.has_value()) return a.type().name();
                        return "Custom (empty std::any)";
                    }
                    return "Custom (invalid state)";
                }
            default:
                return "Unknown";
        }
    }

    std::string SqlValue::driverTypeName() const {
        return m_driver_type_name_cache;
    }
    void SqlValue::setDriverTypeName(const std::string& name) {
        m_driver_type_name_cache = name;
    }
    long long SqlValue::lobSizeHint() const {
        return m_lob_size_hint;
    }

    void SqlValue::clear() {
        m_value_storage = std::monostate{};
        m_current_type_enum = SqlValueType::Null;
        m_driver_type_name_cache.clear();
        m_lob_size_hint = -1;
    }

    bool SqlValue::operator==(const SqlValue& other) const {
        if (m_value_storage.index() != other.m_value_storage.index()) {
            return false;
        }
        if (isNull()) {  // Both are null due to index check above
            return true;
        }

        return std::visit(
            [&other](auto&& lhs_arg) -> bool {
                using LhsT = std::decay_t<decltype(lhs_arg)>;
                // We know other.m_value_storage holds the same alternative type due to index check
                auto const& rhs_arg = std::get<LhsT>(other.m_value_storage);

                if constexpr (std::is_same_v<LhsT, std::monostate>) {
                    return true;  // Both null
                } else if constexpr (std::is_same_v<LhsT, InputStreamPtr>) {
                    return lhs_arg.get() == rhs_arg.get();  // Compare shared_ptr raw pointers
                } else if constexpr (std::is_same_v<LhsT, std::any>) {
                    if (lhs_arg.has_value() && rhs_arg.has_value()) {
                        return lhs_arg.type() == rhs_arg.type();  // Basic type comparison for std::any
                        // Content comparison for std::any is non-trivial and type-dependent
                    }
                    return !lhs_arg.has_value() && !rhs_arg.has_value();
                } else {
                    // For other types that have operator== defined
                    return lhs_arg == rhs_arg;
                }
            },
            m_value_storage);
    }

    bool SqlValue::operator!=(const SqlValue& other) const {
        return !(*this == other);
    }

}  // namespace cpporm_sqldriver