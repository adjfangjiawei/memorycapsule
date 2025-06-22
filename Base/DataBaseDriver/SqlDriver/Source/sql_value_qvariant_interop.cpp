// SqlDriver/Source/sql_value_qvariant_interop.cpp
#include <QMetaType>
#include <QString>
#include <QTimeZone>
#include <QVariant>
#include <any>
#include <format>  // C++26 (std::format from C++20)

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    QVariant SqlValue::toQVariant() const {
        if (isNull()) return QVariant();

        return std::visit(
            [](auto&& arg) -> QVariant {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return QVariant();
                } else if constexpr (std::is_same_v<T, bool>) {
                    return QVariant(arg);
                } else if constexpr (std::is_same_v<T, int8_t>) {
                    return QVariant(static_cast<int>(arg));
                } else if constexpr (std::is_same_v<T, uint8_t>) {
                    return QVariant(static_cast<unsigned int>(arg));
                } else if constexpr (std::is_same_v<T, int16_t>) {
                    return QVariant(static_cast<int>(arg));
                } else if constexpr (std::is_same_v<T, uint16_t>) {
                    return QVariant(static_cast<unsigned int>(arg));
                } else if constexpr (std::is_same_v<T, int32_t>) {
                    return QVariant(arg);
                } else if constexpr (std::is_same_v<T, uint32_t>) {
                    return QVariant(arg);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    return QVariant(static_cast<qlonglong>(arg));
                } else if constexpr (std::is_same_v<T, uint64_t>) {
                    return QVariant(static_cast<qulonglong>(arg));
                } else if constexpr (std::is_same_v<T, float>) {
                    return QVariant(arg);
                } else if constexpr (std::is_same_v<T, double>) {
                    return QVariant(arg);
                } else if constexpr (std::is_same_v<T, long double>) {
                    return QVariant(static_cast<double>(arg));
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return QVariant(QString::fromStdString(arg));
                } else if constexpr (std::is_same_v<T, std::vector<unsigned char>>) {
                    return QVariant(QByteArray(reinterpret_cast<const char*>(arg.data()), static_cast<int>(arg.size())));
                } else if constexpr (std::is_same_v<T, QDate>) {
                    return QVariant::fromValue(arg);
                } else if constexpr (std::is_same_v<T, QTime>) {
                    return QVariant::fromValue(arg);
                } else if constexpr (std::is_same_v<T, QDateTime>) {
                    return QVariant::fromValue(arg);
                } else if constexpr (std::is_same_v<T, ChronoDate>) {
                    if (arg.ok()) return QVariant::fromValue(QDate(static_cast<int>(arg.year()), static_cast<unsigned>(arg.month()), static_cast<unsigned>(arg.day())));
                } else if constexpr (std::is_same_v<T, ChronoTime>) {
                    auto ct_ms = std::chrono::duration_cast<std::chrono::milliseconds>(arg);
                    qint64 total_ms = ct_ms.count() % (24LL * 3600 * 1000);
                    if (total_ms < 0) total_ms += (24LL * 3600 * 1000);
                    return QVariant::fromValue(QTime::fromMSecsSinceStartOfDay(static_cast<int>(total_ms)));
                } else if constexpr (std::is_same_v<T, ChronoDateTime>) {
                    auto secs = std::chrono::time_point_cast<std::chrono::seconds>(arg).time_since_epoch().count();
                    auto msecs_part = std::chrono::duration_cast<std::chrono::milliseconds>(arg.time_since_epoch() % std::chrono::seconds(1)).count();
                    QDateTime qdt = QDateTime::fromSecsSinceEpoch(secs, QTimeZone::utc());
                    qdt = qdt.addMSecs(msecs_part);
                    return QVariant::fromValue(qdt);
                } else if constexpr (std::is_same_v<T, std::any>) {
                    if (arg.has_value()) return QVariant::fromValue(arg);  // 依赖 Qt 6.2+
                }
                return QVariant();
            },
            m_value_storage);
    }

    SqlValue SqlValue::fromQVariant(const QVariant& qv) {
        if (!qv.isValid() || qv.isNull()) return SqlValue();

        int type_id_int = qv.userType();
        if (type_id_int == QMetaType::UnknownType) {
            type_id_int = qv.typeId();
        }

        switch (static_cast<QMetaType::Type>(type_id_int)) {
            case QMetaType::Bool:
                return SqlValue(qv.toBool());
            case QMetaType::Char:
                return SqlValue(static_cast<int8_t>(qv.toChar().toLatin1()));
            case QMetaType::SChar:
                return SqlValue(qv.value<signed char>());
            case QMetaType::UChar:
                return SqlValue(qv.value<unsigned char>());
            case QMetaType::Short:
                {
                    bool ok = false;
                    int val = qv.toInt(&ok);  // 先转为 int
                    if (ok && val >= std::numeric_limits<short>::min() && val <= std::numeric_limits<short>::max()) {
                        return SqlValue(static_cast<int16_t>(val));
                    }
                    return SqlValue();  // 转换失败或超范围
                }
            case QMetaType::UShort:
                {
                    bool ok = false;
                    unsigned int val = qv.toUInt(&ok);  // 先转为 uint
                    if (ok && val <= std::numeric_limits<unsigned short>::max()) {
                        return SqlValue(static_cast<uint16_t>(val));
                    }
                    return SqlValue();  // 转换失败或超范围
                }
            case QMetaType::Int:
                return SqlValue(qv.toInt());
            case QMetaType::UInt:
                return SqlValue(qv.toUInt());
            case QMetaType::Long:
                return SqlValue(static_cast<int64_t>(qv.toLongLong()));
            case QMetaType::ULong:
                return SqlValue(static_cast<uint64_t>(qv.toULongLong()));
            case QMetaType::LongLong:
                return SqlValue(static_cast<int64_t>(qv.toLongLong()));
            case QMetaType::ULongLong:
                return SqlValue(static_cast<uint64_t>(qv.toULongLong()));
            case QMetaType::Float:
                return SqlValue(qv.toFloat());
            case QMetaType::Double:
                return SqlValue(qv.toDouble());
            case QMetaType::QString:
                return SqlValue(qv.toString().toStdString());
            case QMetaType::QByteArray:
                return SqlValue(qv.toByteArray());
            case QMetaType::QDate:
                return SqlValue(qv.toDate());
            case QMetaType::QTime:
                return SqlValue(qv.toTime());
            case QMetaType::QDateTime:
                return SqlValue(qv.toDateTime());
            // QMetaType::StdAny 不在您提供的 qmetatype.h 的 Type 枚举中
            default:
                // 依赖于 Qt 6.2+ 的 canConvert<std::any> 和 value<std::any>
                if (QMetaType(type_id_int).isValid() && qv.canConvert<std::any>()) {
                    return SqlValue::fromStdAny(qv.value<std::any>());
                }
                break;
        }
        return SqlValue();
    }

}  // namespace cpporm_sqldriver