// sqldriver/sql_value.h
#pragma once
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QTime>
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
        ByteArray,
        BinaryLargeObject,
        CharacterLargeObject,
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
        using BlobInputStream = std::shared_ptr<std::istream>;
        using BlobOutputStream = std::shared_ptr<std::ostream>;
        using ClobInputStream = std::shared_ptr<std::basic_istream<char>>;
        using ClobOutputStream = std::shared_ptr<std::basic_ostream<char>>;

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
        SqlValue(const std::vector<unsigned char>& val);

        // SqlValue(const SqlDecimal& val);
        // SqlValue(const SqlJsonDocument& val);
        // SqlValue(const SqlXmlDocument& val);
        // template<typename T> SqlValue(const SqlArray<T>& val);

        SqlValue(BlobInputStream stream_handle, long long size = -1);
        SqlValue(ClobInputStream stream_handle, long long size = -1, const std::string& charset = "UTF-8");

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

        bool toBool(bool* ok = nullptr) const;
        int8_t toInt8(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        uint8_t toUInt8(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        int16_t toInt16(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        uint16_t toUInt16(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        int32_t toInt32(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;    // Corresponds to 'int'
        uint32_t toUInt32(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;  // Corresponds to 'unsigned int'
        int64_t toInt64(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;    // Corresponds to 'long long'
        uint64_t toUInt64(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;  // Corresponds to 'unsigned long long'
        float toFloat(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        double toDouble(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        long double toLongDouble(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        std::string toString(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        std::vector<unsigned char> toStdVectorUChar(bool* ok = nullptr) const;

        // SqlDecimal toDecimal(bool* ok = nullptr) const;
        // SqlJsonDocument toJsonDocument(bool* ok = nullptr) const;
        // SqlXmlDocument toXmlDocument(bool* ok = nullptr) const;
        // template<typename T> std::optional<SqlArray<T>> toArray(bool* ok = nullptr) const;

        BlobInputStream toBlobInputStream(bool* ok = nullptr) const;
        ClobInputStream toClobInputStream(bool* ok = nullptr) const;

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
        using StorageType = std::variant<std::monostate,
                                         bool,
                                         int8_t,
                                         uint8_t,
                                         int16_t,
                                         uint16_t,
                                         int32_t,
                                         uint32_t,
                                         int64_t,
                                         uint64_t,
                                         float,
                                         double,
                                         long double,
                                         std::string,
                                         std::vector<unsigned char>,
                                         BlobInputStream,
                                         ClobInputStream,
                                         QByteArray,
                                         QDate,
                                         QTime,
                                         QDateTime,
                                         ChronoDate,
                                         ChronoTime,
                                         ChronoDateTime,
                                         std::any>;
        StorageType value_;
        SqlValueType current_type_enum_ = SqlValueType::Null;
        std::string driver_type_name_;
        void updateCurrentTypeEnum();
    };

}  // namespace cpporm_sqldriver