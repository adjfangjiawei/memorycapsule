#pragma once
#include <optional>
#include <string>
#include <vector>

#include "sqldriver/sql_value.h"  // For SqlValue::ChronoDate etc.

namespace cpporm_sqldriver {
    namespace detail {

        template <typename IntType>
        std::optional<IntType> stringToInteger(const std::string& s, bool* ok);

        template <typename FloatType>
        std::optional<FloatType> stringToFloat(const std::string& s, bool* ok);

        bool isValidChronoDate(const SqlValue::ChronoDate& cd);
        bool isValidChronoDateTime(const SqlValue::ChronoDateTime& cdt);

        std::string blobToHexString(const std::vector<unsigned char>& blob);

    }  // namespace detail
}  // namespace cpporm_sqldriver