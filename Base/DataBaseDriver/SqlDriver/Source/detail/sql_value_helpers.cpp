#include "sqldriver/detail/sql_value_helpers.h"  // Corresponding header

#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>  // For std::string_view

namespace cpporm_sqldriver {
    namespace detail {

        template <typename IntType>
        std::optional<IntType> stringToInteger(const std::string& s, bool* ok) {
            if (s.empty()) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            IntType val{};
            size_t first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string::npos) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            size_t last = s.find_last_not_of(" \t\n\r\f\v");
            std::string_view sv_trimmed = std::string_view(s).substr(first, last - first + 1);
            auto [ptr, ec] = std::from_chars(sv_trimmed.data(), sv_trimmed.data() + sv_trimmed.size(), val);
            if (ec == std::errc() && ptr == sv_trimmed.data() + sv_trimmed.size()) {
                if (ok) *ok = true;
                return val;
            }
            if (ok) *ok = false;
            return std::nullopt;
        }

        // Explicit instantiations for common integer types if needed, or keep as full template.
        template std::optional<int8_t> stringToInteger<int8_t>(const std::string&, bool*);
        template std::optional<uint8_t> stringToInteger<uint8_t>(const std::string&, bool*);
        template std::optional<int16_t> stringToInteger<int16_t>(const std::string&, bool*);
        template std::optional<uint16_t> stringToInteger<uint16_t>(const std::string&, bool*);
        template std::optional<int32_t> stringToInteger<int32_t>(const std::string&, bool*);
        template std::optional<uint32_t> stringToInteger<uint32_t>(const std::string&, bool*);
        template std::optional<int64_t> stringToInteger<int64_t>(const std::string&, bool*);
        template std::optional<uint64_t> stringToInteger<uint64_t>(const std::string&, bool*);

        template <typename FloatType>
        std::optional<FloatType> stringToFloat(const std::string& s, bool* ok) {
            if (s.empty()) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            size_t first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string::npos) {
                if (ok) *ok = false;
                return std::nullopt;
            }
            size_t last = s.find_last_not_of(" \t\n\r\f\v");
            std::string s_trimmed = s.substr(first, last - first + 1);
            try {
                size_t idx = 0;
                FloatType val{};
                if constexpr (std::is_same_v<FloatType, float>)
                    val = std::stof(s_trimmed, &idx);
                else if constexpr (std::is_same_v<FloatType, double>)
                    val = std::stod(s_trimmed, &idx);
                else if constexpr (std::is_same_v<FloatType, long double>)
                    val = std::stold(s_trimmed, &idx);
                else
                    static_assert(!std::is_same_v<FloatType, FloatType>, "Unsupported float type");

                if (idx == s_trimmed.length()) {
                    if (ok) *ok = true;
                    return val;
                }
            } catch (...) {
            }
            if (ok) *ok = false;
            return std::nullopt;
        }
        template std::optional<float> stringToFloat<float>(const std::string&, bool*);
        template std::optional<double> stringToFloat<double>(const std::string&, bool*);
        template std::optional<long double> stringToFloat<long double>(const std::string&, bool*);

        bool isValidChronoDate(const SqlValue::ChronoDate& cd) {
            return cd.ok();
        }
        bool isValidChronoDateTime(const SqlValue::ChronoDateTime& cdt) {
            // Simplified check: not epoch (often default for invalid) unless it's explicitly zero epoch
            return cdt.time_since_epoch().count() != std::chrono::system_clock::from_time_t(0).time_since_epoch().count() || (cdt.time_since_epoch() == std::chrono::system_clock::duration::zero());
        }

        std::string blobToHexString(const std::vector<unsigned char>& blob) {
            std::ostringstream oss;
            oss << "0x";
            oss << std::hex << std::setfill('0');
            for (unsigned char byte_val : blob) {
                oss << std::setw(2) << static_cast<int>(byte_val);
            }
            return oss.str();
        }

    }  // namespace detail
}  // namespace cpporm_sqldriver