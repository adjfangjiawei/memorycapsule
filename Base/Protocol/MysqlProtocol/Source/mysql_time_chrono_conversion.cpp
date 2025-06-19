// Source/mysql_protocol/mysql_time_chrono_conversion.cpp
#include <chrono>
#include <ctime>    // For std::mktime, std::gmtime_r/localtime_r (or _s versions on Windows)
#include <iomanip>  // For std::get_time (C++11, but might be less robust than manual parsing for specific formats)
#include <string>   // For std::to_string

#include "mysql_protocol/mysql_type_converter.h"

// <mysql/mysql.h>, <expected>, <vector> are included via mysql_type_converter.h

namespace mysql_protocol {

    // Helper to check if a year is a leap year
    bool is_leap(int year) {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    // Helper to get days in month
    int days_in_month(int year, int month) {
        if (month < 1 || month > 12) return 0;  // Invalid month
        if (month == 2) return is_leap(year) ? 29 : 28;
        if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
        return 31;
    }

    std::expected<std::chrono::system_clock::time_point, MySqlProtocolError> mySqlTimeToSystemClockTimePoint(const MYSQL_TIME& mysql_time) {
        // MYSQL_TIME for TIMESTAMP columns usually has time_type = MYSQL_TIMESTAMP_DATETIME.
        // We accept DATE as well, assuming time part is midnight.
        if (mysql_time.time_type != MYSQL_TIMESTAMP_DATETIME && mysql_time.time_type != MYSQL_TIMESTAMP_DATE) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_UNSUPPORTED_TYPE, "MYSQL_TIME must be a DATETIME or DATE type to convert to system_clock::time_point. Actual type: " + std::to_string(mysql_time.time_type)));
        }
        if (mysql_time.year == 0 && mysql_time.month == 0 && mysql_time.day == 0) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_INVALID_MYSQL_TIME, "Zero date (0000-00-00) in MYSQL_TIME cannot be converted to time_point."));
        }
        if (mysql_time.month == 0 || mysql_time.day == 0) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_INVALID_MYSQL_TIME, "MYSQL_TIME has month or day as 0, invalid for time_point conversion."));
        }
        if (mysql_time.day > days_in_month(mysql_time.year, mysql_time.month)) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_INVALID_MYSQL_TIME, "MYSQL_TIME has invalid day for month/year."));
        }

        std::tm t{};
        t.tm_year = mysql_time.year - 1900;
        t.tm_mon = mysql_time.month - 1;
        t.tm_mday = mysql_time.day;
        t.tm_hour = mysql_time.hour;
        t.tm_min = mysql_time.minute;
        t.tm_sec = mysql_time.second;
        t.tm_isdst = -1;

        std::time_t time_since_epoch = std::mktime(&t);
        if (time_since_epoch == -1) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "Failed to convert MYSQL_TIME to time_t (mktime failed), possibly out of range or invalid. Year: " + std::to_string(mysql_time.year)));
        }

        auto tp = std::chrono::system_clock::from_time_t(time_since_epoch);
        tp += std::chrono::microseconds(mysql_time.second_part);

        return tp;
    }

    std::expected<MYSQL_TIME, MySqlProtocolError> systemClockTimePointToMySqlTime(const std::chrono::system_clock::time_point& time_point, enum enum_field_types target_mysql_type) {
        MYSQL_TIME mt{};
        std::memset(&mt, 0, sizeof(MYSQL_TIME));

        std::time_t time_since_epoch = std::chrono::system_clock::to_time_t(time_point);

        std::tm t{};
#ifdef _WIN32
        if (gmtime_s(&t, &time_since_epoch) != 0) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "gmtime_s failed to convert time_point."));
        }
#else
        if (gmtime_r(&time_since_epoch, &t) == nullptr) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "gmtime_r failed to convert time_point."));
        }
#endif

        mt.year = t.tm_year + 1900;
        mt.month = t.tm_mon + 1;
        mt.day = t.tm_mday;
        mt.hour = t.tm_hour;
        mt.minute = t.tm_min;
        mt.second = t.tm_sec;

        auto micros_duration = std::chrono::duration_cast<std::chrono::microseconds>(time_point.time_since_epoch() % std::chrono::seconds(1));
        long long micros_count = micros_duration.count();
        mt.second_part = (micros_count < 0) ? 0 : static_cast<unsigned long>(micros_count);

        mt.neg = false;

        switch (target_mysql_type) {
            case MYSQL_TYPE_DATETIME:
            case MYSQL_TYPE_DATETIME2:
            case MYSQL_TYPE_TIMESTAMP:
            case MYSQL_TYPE_TIMESTAMP2:
                mt.time_type = MYSQL_TIMESTAMP_DATETIME;
                break;
            case MYSQL_TYPE_DATE:
            case MYSQL_TYPE_NEWDATE:
                mt.time_type = MYSQL_TIMESTAMP_DATE;
                mt.hour = 0;
                mt.minute = 0;
                mt.second = 0;
                mt.second_part = 0;
                break;
            default:
                return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_UNSUPPORTED_TYPE, "Unsupported target MySQL type for system_clock::time_point conversion: " + std::to_string(target_mysql_type)));
        }

        if (mt.year > 9999 || mt.month > 12 || mt.month == 0 || mt.day > days_in_month(mt.year, mt.month) || mt.day == 0 || mt.hour > 23 || mt.minute > 59 || mt.second > 59) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "Converted time_point results in MYSQL_TIME component out of typical range."));
        }

        return mt;
    }

    std::expected<std::chrono::year_month_day, MySqlProtocolError> mySqlTimeToYearMonthDay(const MYSQL_TIME& mysql_time) {
        // A MYSQL_TIME representing a DATE or DATETIME can be converted.
        if (mysql_time.time_type != MYSQL_TIMESTAMP_DATE && mysql_time.time_type != MYSQL_TIMESTAMP_DATETIME) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_UNSUPPORTED_TYPE, "MYSQL_TIME must be DATE or DATETIME compatible for year_month_day. Actual type: " + std::to_string(mysql_time.time_type)));
        }
        if (mysql_time.year == 0 || mysql_time.month == 0 || mysql_time.day == 0) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_INVALID_MYSQL_TIME, "MYSQL_TIME has zero year, month, or day."));
        }
        if (mysql_time.day > days_in_month(mysql_time.year, mysql_time.month)) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_INVALID_MYSQL_TIME, "MYSQL_TIME has invalid day for month/year for ymd conversion."));
        }

        try {
            auto y = std::chrono::year(static_cast<int>(mysql_time.year));
            auto m = std::chrono::month(static_cast<unsigned int>(mysql_time.month));
            auto d = std::chrono::day(static_cast<unsigned int>(mysql_time.day));

            std::chrono::year_month_day ymd(y, m, d);
            if (!ymd.ok()) {
                return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "Constructed chrono::year_month_day is not valid."));
            }
            return ymd;

        } catch (const std::exception& e) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "chrono::year_month_day construction failed: " + std::string(e.what())));
        }
    }

    std::expected<MYSQL_TIME, MySqlProtocolError> yearMonthDayToMySqlDate(const std::chrono::year_month_day& ymd) {
        MYSQL_TIME mt{};
        std::memset(&mt, 0, sizeof(MYSQL_TIME));
        mt.time_type = MYSQL_TIMESTAMP_DATE;

        if (!ymd.ok()) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_INVALID_MYSQL_TIME, "Input std::chrono::year_month_day is invalid."));
        }

        int y = static_cast<int>(ymd.year());
        unsigned int m = static_cast<unsigned int>(ymd.month());
        unsigned int d = static_cast<unsigned int>(ymd.day());

        // MySQL typical range for DATE parts
        if (y < 1000 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "year_month_day components out of typical MySQL DATE range (1000-01-01 to 9999-12-31). Year: " + std::to_string(y)));
        }
        if (d > days_in_month(y, m)) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "Invalid day for month/year in year_month_day for MySQL DATE."));
        }

        mt.year = static_cast<unsigned int>(y);
        mt.month = m;
        mt.day = d;
        return mt;
    }

    std::expected<std::chrono::microseconds, MySqlProtocolError> mySqlTimeToDuration(const MYSQL_TIME& mysql_time) {
        if (mysql_time.time_type != MYSQL_TIMESTAMP_TIME) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_UNSUPPORTED_TYPE, "MYSQL_TIME must be of type MYSQL_TIMESTAMP_TIME for duration conversion. Actual type: " + std::to_string(mysql_time.time_type)));
        }

        if (mysql_time.hour > 838 || mysql_time.minute > 59 || mysql_time.second > 59 || mysql_time.second_part > 999999) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "MYSQL_TIME components for TIME type out of range."));
        }

        std::chrono::microseconds total_micros = std::chrono::hours(mysql_time.hour) + std::chrono::minutes(mysql_time.minute) + std::chrono::seconds(mysql_time.second) + std::chrono::microseconds(mysql_time.second_part);

        if (mysql_time.neg) {
            return -total_micros;
        }
        return total_micros;
    }

    std::expected<MYSQL_TIME, MySqlProtocolError> durationToMySqlTime(std::chrono::microseconds duration) {
        MYSQL_TIME mt{};
        std::memset(&mt, 0, sizeof(MYSQL_TIME));
        mt.time_type = MYSQL_TIMESTAMP_TIME;

        if (duration.count() < 0) {
            mt.neg = true;
            duration = -duration;
        }

        constexpr long long max_mysql_hours = 838LL;
        constexpr std::chrono::microseconds max_mysql_time_duration = std::chrono::hours(max_mysql_hours) + std::chrono::minutes(59) + std::chrono::seconds(59) + std::chrono::microseconds(999999);

        if (duration > max_mysql_time_duration) {
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "Duration exceeds MySQL TIME range (max 838:59:59.999999)."));
        }

        auto hrs_count = std::chrono::duration_cast<std::chrono::hours>(duration).count();
        if (hrs_count > max_mysql_hours) {  // Extra check due to potential large microsecond values that don't roll over hours correctly with simple modulo
            return std::unexpected(MySqlProtocolError(InternalErrc::TIME_CHRONO_CONVERSION_OUT_OF_RANGE, "Duration hours component exceeds MySQL TIME range."));
        }
        mt.hour = static_cast<unsigned int>(hrs_count);
        duration -= std::chrono::hours(hrs_count);

        auto mins_count = std::chrono::duration_cast<std::chrono::minutes>(duration).count();
        mt.minute = static_cast<unsigned int>(mins_count);
        duration -= std::chrono::minutes(mins_count);

        auto secs_count = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        mt.second = static_cast<unsigned int>(secs_count);
        duration -= std::chrono::seconds(secs_count);

        mt.second_part = static_cast<unsigned long>(duration.count());

        return mt;
    }

}  // namespace mysql_protocol