// SqlDriver/Include/sqldriver/sql_value.h
#pragma once
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>  // For Qt 6 deprecated API fixes
#include <QVariant>
#include <any>
#include <chrono>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <variant>
#include <vector>

namespace cpporm_sqldriver {

    enum class SqlValueType {
        Null,
        Bool,
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float,
        Double,
        LongDouble,
        String,
        FixedString,
        ByteArray,             // Represents a byte sequence, from QByteArray or std::vector<unsigned char>
        BinaryLargeObject,     // Semantic type for BLOB stream (uses InputStreamPtr in variant)
        CharacterLargeObject,  // Semantic type for CLOB stream (uses InputStreamPtr in variant)
        Date,
        Time,
        DateTime,
        Timestamp,
        Interval,
        Decimal,
        Numeric,
        Json,
        Xml,
        Array,
        RowId,
        Custom,
        Unknown
    };

    enum class NumericalPrecisionPolicy { LowPrecision, HighPrecision, ExactRepresentation };

    class SqlValue {
      public:
        using ChronoDate = std::chrono::year_month_day;
        using ChronoTime = std::chrono::nanoseconds;
        using ChronoDateTime = std::chrono::system_clock::time_point;

        using InputStreamPtr = std::shared_ptr<std::istream>;  // 通用输入流指针

        SqlValue();
        SqlValue(std::nullptr_t);
        SqlValue(bool val);
        SqlValue(int8_t val);
        SqlValue(uint8_t val);
        SqlValue(int16_t val);
        SqlValue(uint16_t val);
        SqlValue(int32_t val);
        SqlValue(uint32_t val);
        SqlValue(int64_t val);
        SqlValue(uint64_t val);
        SqlValue(float val);
        SqlValue(double val);
        SqlValue(long double val);
        SqlValue(const char* val, SqlValueType type_hint = SqlValueType::String);
        SqlValue(const std::string& val, SqlValueType type_hint = SqlValueType::String);
        SqlValue(const std::vector<unsigned char>& val);  // 用于原始字节

        // LOB 流构造函数 (使用通用 InputStreamPtr 和一个类型提示)
        SqlValue(InputStreamPtr stream_handle, SqlValueType lob_type /* 必须是 BinaryLargeObject 或 CharacterLargeObject */, long long size = -1);

        SqlValue(const QByteArray& val);
        SqlValue(const QDate& val);
        SqlValue(const QTime& val);
        SqlValue(const QDateTime& val);

        SqlValue(const ChronoDate& val);
        SqlValue(const ChronoTime& val);
        SqlValue(const ChronoDateTime& val);

        SqlValue(const SqlValue& other);
        SqlValue& operator=(const SqlValue& other);
        SqlValue(SqlValue&& other) noexcept;
        SqlValue& operator=(SqlValue&& other) noexcept;
        ~SqlValue();

        bool isNull() const;
        bool isValid() const;
        SqlValueType type() const;
        const char* typeName() const;
        std::string driverTypeName() const;
        void setDriverTypeName(const std::string& name);
        long long lobSizeHint() const;

        bool toBool(bool* ok = nullptr) const;
        int8_t toInt8(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        uint8_t toUInt8(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        int16_t toInt16(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        uint16_t toUInt16(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        int32_t toInt32(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        uint32_t toUInt32(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        int64_t toInt64(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        uint64_t toUInt64(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        float toFloat(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        double toDouble(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        long double toLongDouble(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        std::string toString(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        std::vector<unsigned char> toStdVectorUChar(bool* ok = nullptr) const;

        InputStreamPtr toInputStream(bool* ok = nullptr) const;  // 通用获取输入流方法

        QByteArray toByteArray(bool* ok = nullptr) const;
        QDate toDate(bool* ok = nullptr) const;
        QTime toTime(bool* ok = nullptr) const;
        QDateTime toDateTime(bool* ok = nullptr) const;

        ChronoDate toChronoDate(bool* ok = nullptr) const;
        ChronoTime toChronoTime(bool* ok = nullptr) const;
        ChronoDateTime toChronoDateTime(bool* ok = nullptr) const;

        bool operator==(const SqlValue& other) const;
        bool operator!=(const SqlValue& other) const;

        void clear();

        QVariant toQVariant() const;
        static SqlValue fromQVariant(const QVariant& qv);

        std::any toStdAny() const;
        static SqlValue fromStdAny(const std::any& val, SqlValueType type_hint = SqlValueType::Custom);

      private:
        // StorageType 确保类型不重复
        using StorageType = std::variant<  // 索引从0开始
            std::monostate,                // 0: Null
            bool,                          // 1
            int8_t,                        // 2
            uint8_t,                       // 3
            int16_t,                       // 4
            uint16_t,                      // 5
            int32_t,                       // 6
            uint32_t,                      // 7
            int64_t,                       // 8
            uint64_t,                      // 9
            float,                         // 10
            double,                        // 11
            long double,                   // 12
            std::string,                   // 13: String, FixedString, CLOB data, Json, Xml, Decimal, Numeric
            std::vector<unsigned char>,    // 14: ByteArray, BLOB data (non-stream)
            InputStreamPtr,                // 15: BLOB/CLOB streams
            QDate,                         // 16
            QTime,                         // 17
            QDateTime,                     // 18
            ChronoDate,                    // 19
            ChronoTime,                    // 20
            ChronoDateTime,                // 21
            std::any                       // 22: Custom
            >;

        StorageType m_value_storage;
        SqlValueType m_current_type_enum;
        std::string m_driver_type_name_cache;
        long long m_lob_size_hint;

        void updateCurrentTypeEnumFromStorage();
    };

}  // namespace cpporm_sqldriver