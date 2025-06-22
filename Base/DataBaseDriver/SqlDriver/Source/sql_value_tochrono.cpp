// SqlDriver/Source/sql_value_tochrono.cpp
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime>
#include <QTimeZone>  // For Qt 6
#include <charconv>
#include <chrono>
#include <cstdio>
#include <variant>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {
    namespace detail {
#ifndef SQLVALUE_HELPERS_DEFINED
#define SQLVALUE_HELPERS_DEFINED
// ...
#endif
    }  // namespace detail

    SqlValue::ChronoDate SqlValue::toChronoDate(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return ChronoDate{};

        if (std::holds_alternative<ChronoDate>(m_value_storage)) {
            const auto& cd = std::get<ChronoDate>(m_value_storage);
            if (cd.ok()) {
                if (ok) *ok = true;
                return cd;
            }
        }
        if (std::holds_alternative<QDate>(m_value_storage)) {
            const QDate& qd = std::get<QDate>(m_value_storage);
            if (qd.isValid()) {
                if (ok) *ok = true;
                return ChronoDate{std::chrono::year(qd.year()), std::chrono::month(static_cast<unsigned>(qd.month())), std::chrono::day(static_cast<unsigned>(qd.day()))};
            }
        }
        if (std::holds_alternative<std::string>(m_value_storage)) {
            const std::string& s = std::get<std::string>(m_value_storage);
            if (s.length() == 10 && s[4] == '-' && s[7] == '-') {
                int y{}, M{}, d{};
                // 使用 sscanf 作为 from_chars 的简单替代，以避免 C++26 对 charconv 的完全依赖（尽管项目标准是26）
                // 如果 from_chars 可靠，则更好。
                if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &M, &d) == 3) {
                    ChronoDate cd_parsed{std::chrono::year(y), std::chrono::month(static_cast<unsigned>(M)), std::chrono::day(static_cast<unsigned>(d))};
                    if (cd_parsed.ok()) {
                        if (ok) *ok = true;
                        return cd_parsed;
                    }
                }
            }
        }
        if (std::holds_alternative<QDateTime>(m_value_storage)) {
            const QDateTime& qdt = std::get<QDateTime>(m_value_storage);
            if (qdt.isValid()) {
                QDate qd = qdt.date();
                if (ok) *ok = true;
                return ChronoDate{std::chrono::year(qd.year()), std::chrono::month(static_cast<unsigned>(qd.month())), std::chrono::day(static_cast<unsigned>(qd.day()))};
            }
        }
        if (std::holds_alternative<ChronoDateTime>(m_value_storage)) {
            const auto& cdt = std::get<ChronoDateTime>(m_value_storage);
            auto dp = std::chrono::floor<std::chrono::days>(cdt);
            std::chrono::year_month_day ymd{dp};
            if (ymd.ok()) {
                if (ok) *ok = true;
                return ymd;
            }
        }
        return ChronoDate{};
    }

    SqlValue::ChronoTime SqlValue::toChronoTime(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return ChronoTime{};

        if (std::holds_alternative<ChronoTime>(m_value_storage)) {
            if (ok) *ok = true;
            return std::get<ChronoTime>(m_value_storage);
        }
        if (std::holds_alternative<QTime>(m_value_storage)) {
            const QTime& qt = std::get<QTime>(m_value_storage);
            if (qt.isValid()) {
                if (ok) *ok = true;
                return std::chrono::hours(qt.hour()) + std::chrono::minutes(qt.minute()) + std::chrono::seconds(qt.second()) + std::chrono::milliseconds(qt.msec());
            }
        }
        if (std::holds_alternative<std::string>(m_value_storage)) {
            const std::string& s = std::get<std::string>(m_value_storage);
            int h{}, m{}, sec{};
            long us = 0;
            if (std::sscanf(s.c_str(), "%d:%d:%d.%6ld", &h, &m, &sec, &us) >= 3 || std::sscanf(s.c_str(), "%d:%d:%d", &h, &m, &sec) == 3) {
                if (s.find('.') == std::string::npos && s.find_last_of(':') != std::string::npos && s.find_last_of(':') > s.find(':')) us = 0;  // 确保无小数部分时 us 为0

                if (h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec < 60 && us >= 0 && us < 1000000) {
                    if (ok) *ok = true;
                    return std::chrono::hours(h) + std::chrono::minutes(m) + std::chrono::seconds(sec) + std::chrono::microseconds(us);
                }
            }
        }
        if (std::holds_alternative<QDateTime>(m_value_storage)) {
            const QDateTime& qdt = std::get<QDateTime>(m_value_storage);
            if (qdt.isValid()) {
                QTime qt = qdt.time();
                if (ok) *ok = true;
                return std::chrono::hours(qt.hour()) + std::chrono::minutes(qt.minute()) + std::chrono::seconds(qt.second()) + std::chrono::milliseconds(qt.msec());
            }
        }
        if (std::holds_alternative<ChronoDateTime>(m_value_storage)) {
            const auto& cdt = std::get<ChronoDateTime>(m_value_storage);
            auto day_point = std::chrono::floor<std::chrono::days>(cdt);
            auto time_in_day = cdt - day_point;
            if (ok) *ok = true;
            return std::chrono::duration_cast<ChronoTime>(time_in_day);
        }
        return ChronoTime{};
    }

    SqlValue::ChronoDateTime SqlValue::toChronoDateTime(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return ChronoDateTime{};

        if (std::holds_alternative<ChronoDateTime>(m_value_storage)) {
            if (ok) *ok = true;
            return std::get<ChronoDateTime>(m_value_storage);
        }
        if (std::holds_alternative<QDateTime>(m_value_storage)) {
            const QDateTime& qdt = std::get<QDateTime>(m_value_storage);
            if (qdt.isValid()) {
                if (ok) *ok = true;
                // 使用 toSecsSinceEpoch，它返回相对于UTC的秒数
                std::chrono::seconds secs(qdt.toSecsSinceEpoch());
                std::chrono::milliseconds msecs_part(qdt.time().msec());
                // QDateTime::offsetFromUtc() 如果是本地时间，则返回与UTC的偏移量（秒）
                // 如果 qdt 本身是UTC，则 offsetFromUtc() 为0.
                // toSecsSinceEpoch 已经考虑了时区（如果是本地时间，会转换成UTC秒数）
                return std::chrono::system_clock::time_point(secs + msecs_part);
            }
        }
        if (std::holds_alternative<std::string>(m_value_storage)) {
            const std::string& s = std::get<std::string>(m_value_storage);
            int y{}, M{}, d{}, h{}, m{}, sec{};
            long us = 0;
            char sep = 0, tz_char_dummy = 0;  // tz_char_dummy 用于捕获可能的 'Z' 或时区指示符，但我们不直接用它来调整
                                              // 因为 system_clock::time_point 通常是 UTC based.
                                              // 如果字符串中没有 'Z' 或时区，它可能被解析为本地时间，
                                              // 然后转换为 time_t (通常是本地)，再转为 system_clock (通常是UTC)。
                                              // 这种转换链条需要小心。
                                              // 一个健壮的解析器会处理时区。
            // ISO 8601-like: YYYY-MM-DD[T| ]HH:MM:SS[.ffffff][Z|+HH:MM|-HH:MM]
            // 简化版，主要处理 YYYY-MM-DD HH:MM:SS[.us]
            int items_read = std::sscanf(s.c_str(), "%d-%d-%d%c%d:%d:%d.%6ld", &y, &M, &d, &sep, &h, &m, &sec, &us);
            if (items_read < 7) {
                us = 0;
                items_read = std::sscanf(s.c_str(), "%d-%d-%d%c%d:%d:%d", &y, &M, &d, &sep, &h, &m, &sec);
            }

            if (items_read >= 7 && (sep == 'T' || sep == ' ')) {
                ChronoDate cd_part{std::chrono::year(y), std::chrono::month(static_cast<unsigned>(M)), std::chrono::day(static_cast<unsigned>(d))};
                if (cd_part.ok() && h >= 0 && h < 24 && m >= 0 && m < 60 && sec >= 0 && sec < 60 && us >= 0 && us < 1000000) {
                    // 将解析的本地时间组件转换为 time_t，然后转换为 system_clock::time_point (通常是UTC)
                    // 这依赖于 mktime 将本地时间转换为 UTC time_t (如果 TZ 环境变量设置正确等)
                    std::tm t{};
                    t.tm_year = y - 1900;
                    t.tm_mon = M - 1;
                    t.tm_mday = d;
                    t.tm_hour = h;
                    t.tm_min = m;
                    t.tm_sec = sec;
                    t.tm_isdst = -1;  // 让 mktime 决定夏令时
                    std::time_t tt = std::mktime(&t);
                    if (tt != -1) {
                        if (ok) *ok = true;
                        return std::chrono::system_clock::from_time_t(tt) + std::chrono::microseconds(us);
                    }
                }
            }
        }
        if (std::holds_alternative<QDate>(m_value_storage)) {
            const QDate& qd = std::get<QDate>(m_value_storage);
            if (qd.isValid()) {
                if (ok) *ok = true;
                // 转换为 UTC 午夜
                QDateTime qdt_utc(qd, QTime(0, 0, 0), QTimeZone::utc());
                return std::chrono::system_clock::from_time_t(qdt_utc.toSecsSinceEpoch());
            }
        }
        if (std::holds_alternative<ChronoDate>(m_value_storage)) {
            const auto& cd = std::get<ChronoDate>(m_value_storage);
            if (cd.ok()) {
                if (ok) *ok = true;
                return std::chrono::sys_days(cd);  // time_point at midnight UTC
            }
        }
        return ChronoDateTime{};
    }

}  // namespace cpporm_sqldriver