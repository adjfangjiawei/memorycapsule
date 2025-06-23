#include "sqldriver/mysql/mysql_specific_result.h"

// No special includes needed for these simple option setters

namespace cpporm_sqldriver {

    bool MySqlSpecificResult::setQueryTimeout(int /*seconds*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "setQueryTimeout is not supported by this driver.");
        return false;
    }

    bool MySqlSpecificResult::setNumericalPrecisionPolicy(NumericalPrecisionPolicy policy) {
        m_precision_policy = policy;
        // This policy would be checked in data conversion functions,
        // but setting it is always considered successful.
        return true;
    }

    bool MySqlSpecificResult::setPrefetchSize(int rows) {
        m_prefetch_size_hint = rows;
        // This is just a hint, driver doesn't actually support it, so return false.
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "setPrefetchSize is not supported by this driver.");
        return false;
    }

    int MySqlSpecificResult::prefetchSize() const {
        return m_prefetch_size_hint;
    }

    bool MySqlSpecificResult::setForwardOnly(bool forward) {
        if (forward) {
            m_scroll_mode_hint = SqlResultNs::ScrollMode::ForwardOnly;
        } else {
            // A request for scrollable might be made, but we don't support it.
            // Let's set the hint but return false to indicate non-support.
            m_scroll_mode_hint = SqlResultNs::ScrollMode::Scrollable;
            m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "Scrollable cursors are not supported.");
            return false;
        }
        return true;
    }

    bool MySqlSpecificResult::setNamedBindingSyntax(SqlResultNs::NamedBindingSyntax syntax) {
        m_named_binding_syntax = syntax;
        return true;
    }

}  // namespace cpporm_sqldriver