// cpporm_sqldriver/sql_value.h
#pragma once
#include <QByteArray>  // Assuming Qt Core types are permissible for CppOrm transition
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QVariant>
#include <any>
#include <chrono>
#include <iosfwd>
#include <optional>
#include <string>
#include <typeinfo>
#include <variant>
#include <vector>

namespace cpporm_sqldriver {

    enum class SqlValueType { Null, Bool, Int, UInt, LongLong, ULongLong, Float, Double, String, ByteArray, Date, Time, DateTime, Decimal, Json, Xml, Array, Stream, Custom };

    // NumericalPrecisionPolicy 定义在此处，因为它与 SqlValue 的转换行为紧密相关
    enum class NumericalPrecisionPolicy {
        LowPrecision,        // 类似 QVariant 的行为，可能损失精度
        HighPrecision,       // 尝试保持精度，例如数字可能转为字符串
        ExactRepresentation  // 要求精确表示，否则转换失败
    };

    class SqlValue {
      public:
        using ChronoDate = std::chrono::year_month_day;
        using ChronoTime = std::chrono::nanoseconds;  // 代表从午夜开始的持续时间，纳秒精度
        using ChronoDateTime = std::chrono::system_clock::time_point;

        // 构造函数
        SqlValue();                // Null 状态
        SqlValue(std::nullptr_t);  // 显式 Null
        SqlValue(bool val);
        SqlValue(int val);
        SqlValue(unsigned int val);
        SqlValue(long long val);
        SqlValue(unsigned long long val);
        SqlValue(float val);  // 内部存储为 double
        SqlValue(double val);
        SqlValue(const char* val);
        SqlValue(const std::string& val);
        SqlValue(const std::vector<unsigned char>& val);  // 原生字节数组

        // Qt types for easier CppOrm transition
        SqlValue(const QByteArray& val);
        SqlValue(const QDate& val);
        SqlValue(const QTime& val);
        SqlValue(const QDateTime& val);

        // Chrono types
        SqlValue(const ChronoDate& val);
        SqlValue(const ChronoTime& val);  // Duration
        SqlValue(const ChronoDateTime& val);

        // 拷贝和移动
        SqlValue(const SqlValue& other);
        SqlValue& operator=(const SqlValue& other);
        SqlValue(SqlValue&& other) noexcept;
        SqlValue& operator=(SqlValue&& other) noexcept;
        ~SqlValue();

        // 类型检查和元数据
        bool isNull() const;
        bool isValid() const;          // SqlValue 通常总是有效的，除非是默认构造且未设置
        SqlValueType type() const;     // 返回 SqlValueType 枚举
        const char* typeName() const;  // 返回类型的C字符串名称 (类似 QVariant::typeName)

        // 类型转换方法 (ok 参数表示转换是否成功)
        bool toBool(bool* ok = nullptr) const;
        int toInt(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        unsigned int toUInt(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        long long toLongLong(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        unsigned long long toULongLong(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        float toFloat(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        double toDouble(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        std::string toString(bool* ok = nullptr, NumericalPrecisionPolicy policy = NumericalPrecisionPolicy::LowPrecision) const;
        std::vector<unsigned char> toStdVectorUChar(bool* ok = nullptr) const;

        QByteArray toByteArray(bool* ok = nullptr) const;
        QDate toDate(bool* ok = nullptr) const;
        QTime toTime(bool* ok = nullptr) const;
        QDateTime toDateTime(bool* ok = nullptr) const;

        ChronoDate toChronoDate(bool* ok = nullptr) const;
        ChronoTime toChronoTime(bool* ok = nullptr) const;
        ChronoDateTime toChronoDateTime(bool* ok = nullptr) const;

        // 比较操作符
        bool operator==(const SqlValue& other) const;
        bool operator!=(const SqlValue& other) const;

        void clear();  // 重置为 Null 状态

        // 转换为 QVariant (为了 CppOrm 层的兼容性)
        QVariant toQVariant() const;
        static SqlValue fromQVariant(const QVariant& qv);

        std::any toStdAny() const;  // 转换为 std::any
        static SqlValue fromStdAny(const std::any& val);

      private:
        // 使用 std::variant 存储实际数据
        using StorageType = std::variant<std::monostate,  // 代表 Null
                                         bool,
                                         int,
                                         unsigned int,
                                         long long,
                                         unsigned long long,
                                         double,  // float is stored as double
                                         std::string,
                                         std::vector<unsigned char>,  // For native byte array
                                         QByteArray,
                                         QDate,
                                         QTime,
                                         QDateTime,  // Qt types
                                         ChronoDate,
                                         ChronoTime,
                                         ChronoDateTime,  // Chrono types
                                         std::any         // 用于存储驱动特定的或非常规类型 (谨慎使用)
                                         >;
        StorageType value_;
        SqlValueType current_type_enum_ = SqlValueType::Null;
        void updateCurrentTypeEnum();  // 辅助函数，根据 value_ 更新 current_type_enum_
    };

}  // namespace cpporm_sqldriver