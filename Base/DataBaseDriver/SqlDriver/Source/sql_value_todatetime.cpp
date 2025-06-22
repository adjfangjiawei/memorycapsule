// SqlDriver/Source/sql_value_todatetime.cpp
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>
#include <QTimeZone>  // For Qt 6
#include <chrono>
#include <cstdio>  // for sscanf in string parsing (alternative to from_chars for simplicity here)
#include <variant>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {
    namespace detail {
#ifndef SQLVALUE_HELPERS_DEFINED
#define SQLVALUE_HELPERS_DEFINED
        // Forward declare or include helpers if they are in a separate file
        bool isValidChronoDate(const SqlValue::ChronoDate& cd);
        bool isValidChronoDateTime(const SqlValue::ChronoDateTime& cdt);
#endif
    }  // namespace detail

    QDate SqlValue::toDate(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return QDate();

        if (std::holds_alternative<QDate>(m_value_storage)) {
            const QDate& qd = std::get<QDate>(m_value_storage);
            if (qd.isValid()) {
                if (ok) *ok = true;
                return qd;
            }
        }
        if (std::holds_alternative<std::string>(m_value_storage)) {
            const std::string& s = std::get<std::string>(m_value_storage);
            QDate d = QDate::fromString(QString::fromStdString(s), Qt::ISODate);
            if (d.isValid()) {
                if (ok) *ok = true;
                return d;
            }
            d = QDate::fromString(QString::fromStdString(s), "yyyy-MM-dd");
            if (d.isValid()) {
                if (ok) *ok = true;
                return d;
            }
        }
        if (std::holds_alternative<ChronoDate>(m_value_storage)) {
            const auto& cd = std::get<ChronoDate>(m_value_storage);
            if (cd.ok()) {
                QDate qd(static_cast<int>(cd.year()), static_cast<int>(static_cast<unsigned>(cd.month())), static_cast<int>(static_cast<unsigned>(cd.day())));
                if (qd.isValid()) {
                    if (ok) *ok = true;
                    return qd;
                }
            }
        }
        if (std::holds_alternative<QDateTime>(m_value_storage)) {
            const QDateTime& qdt = std::get<QDateTime>(m_value_storage);
            if (qdt.isValid()) {
                if (ok) *ok = true;
                return qdt.date();
            }
        }
        if (std::holds_alternative<ChronoDateTime>(m_value_storage)) {
            const auto& cdt = std::get<ChronoDateTime>(m_value_storage);
            std::time_t time_t_val = std::chrono::system_clock::to_time_t(cdt);
            std::tm tm_val{};
#ifdef _WIN32
            if (gmtime_s(&tm_val, &time_t_val) == 0) {  // UTC based
#else
            if (gmtime_r(&time_t_val, &tm_val) != nullptr) {  // UTC based
#endif
                QDate qd(tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday);
                if (qd.isValid()) {
                    if (ok) *ok = true;
                    return qd;
                }
            }
        }
        return QDate();
    }

    QTime SqlValue::toTime(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return QTime();

        if (std::holds_alternative<QTime>(m_value_storage)) {
            const QTime& qt = std::get<QTime>(m_value_storage);
            if (qt.isValid()) {
                if (ok) *ok = true;
                return qt;
            }
        }
        if (std::holds_alternative<std::string>(m_value_storage)) {
            const std::string& s = std::get<std::string>(m_value_storage);
            QTime t = QTime::fromString(QString::fromStdString(s), Qt::ISODateWithMs);
            if (!t.isValid()) t = QTime::fromString(QString::fromStdString(s), Qt::ISODate);
            if (!t.isValid()) t = QTime::fromString(QString::fromStdString(s), "HH:mm:ss.zzz");
            if (t.isValid()) {
                if (ok) *ok = true;
                return t;
            }
        }
        if (std::holds_alternative<ChronoTime>(m_value_storage)) {
            auto ct_ns = std::get<ChronoTime>(m_value_storage);
            auto ct_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ct_ns);
            qint64 total_ms = ct_ms.count() % (24LL * 3600 * 1000);
            if (total_ms < 0) total_ms += (24LL * 3600 * 1000);
            QTime qt = QTime::fromMSecsSinceStartOfDay(static_cast<int>(total_ms));
            if (qt.isValid()) {
                if (ok) *ok = true;
                return qt;
            }
        }
        if (std::holds_alternative<QDateTime>(m_value_storage)) {
            const QDateTime& qdt = std::get<QDateTime>(m_value_storage);
            if (qdt.isValid()) {
                if (ok) *ok = true;
                return qdt.time();
            }
        }
        if (std::holds_alternative<ChronoDateTime>(m_value_storage)) {
            const auto& cdt = std::get<ChronoDateTime>(m_value_storage);
            auto time_since_epoch_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cdt.time_since_epoch());
            auto ns_in_day = time_since_epoch_ns % std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::hours(24));
            if (ns_in_day.count() < 0) ns_in_day += std::chrono::hours(24);
            auto ms_in_day = std::chrono::duration_cast<std::chrono::milliseconds>(ns_in_day).count();
            QTime qt = QTime::fromMSecsSinceStartOfDay(static_cast<int>(ms_in_day));
            if (qt.isValid()) {
                if (ok) *ok = true;
                return qt;
            }
        }
        return QTime();
    }

    QDateTime SqlValue::toDateTime(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return QDateTime();

        if (std::holds_alternative<QDateTime>(m_value_storage)) {
            const QDateTime& qdt = std::get<QDateTime>(m_value_storage);
            if (qdt.isValid()) {
                if (ok) *ok = true;
                return qdt;
            }
        }
        if (std::holds_alternative<std::string>(m_value_storage)) {
            const std::string& s = std::get<std::string>(m_value_storage);
            QDateTime dt = QDateTime::fromString(QString::fromStdString(s), Qt::ISODateWithMs);
            if (!dt.isValid()) dt = QDateTime::fromString(QString::fromStdString(s), Qt::ISODate);
            if (!dt.isValid()) {
                dt = QDateTime::fromString(QString::fromStdString(s), "yyyy-MM-dd HH:mm:ss.zzz");
                if (!dt.isValid()) dt = QDateTime::fromString(QString::fromStdString(s), "yyyy-MM-dd HH:mm:ss");
            }
            if (dt.isValid()) {
                if (ok) *ok = true;
                return dt;
            }
        }
        if (std::holds_alternative<ChronoDateTime>(m_value_storage)) {
            const auto& cdt = std::get<ChronoDateTime>(m_value_storage);
            auto secs_since_epoch = std::chrono::time_point_cast<std::chrono::seconds>(cdt).time_since_epoch().count();
            auto micros_part = std::chrono::duration_cast<std::chrono::microseconds>(cdt.time_since_epoch() % std::chrono::seconds(1)).count();

            QDateTime qdt = QDateTime::fromSecsSinceEpoch(secs_since_epoch, QTimeZone::utc());  // 使用 QTimeZone::utc()
            qdt = qdt.addMSecs(static_cast<qint64>(micros_part / 1000));
            if (qdt.isValid()) {
                if (ok) *ok = true;
                return qdt;
            }
        }
        if (std::holds_alternative<QDate>(m_value_storage)) {
            const QDate& qd = std::get<QDate>(m_value_storage);
            if (qd.isValid()) {
                if (ok) *ok = true;
                return QDateTime(qd, QTime(0, 0, 0), QTimeZone::utc());  // 从 QDate 构造 QDateTime
            }
        }
        if (std::holds_alternative<ChronoDate>(m_value_storage)) {
            const auto& cd = std::get<ChronoDate>(m_value_storage);
            if (cd.ok()) {
                QDate qd(static_cast<int>(cd.year()), static_cast<int>(static_cast<unsigned>(cd.month())), static_cast<int>(static_cast<unsigned>(cd.day())));
                if (qd.isValid()) {
                    if (ok) *ok = true;
                    return QDateTime(qd, QTime(0, 0, 0), QTimeZone::utc());
                }
            }
        }
        return QDateTime();
    }

}  // namespace cpporm_sqldriver