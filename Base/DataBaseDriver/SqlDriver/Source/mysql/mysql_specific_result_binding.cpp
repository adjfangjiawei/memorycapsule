#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"

namespace cpporm_sqldriver {

    bool MySqlSpecificResult::applyBindingsToTransportStatement() {
        if (!m_transport_statement || !m_transport_statement->isPrepared()) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Statement not prepared for binding.", "applyBindingsToTransportStatement");
            return false;
        }
        m_ordered_transport_bind_params.clear();

        if (m_placeholder_info.hasNamedPlaceholders) {
            m_ordered_transport_bind_params.reserve(m_placeholder_info.orderedParamNames.size());
            for (const std::string& name : m_placeholder_info.orderedParamNames) {
                auto it = m_named_bind_values_map.find(name);
                if (it == m_named_bind_values_map.end()) {
                    m_last_error_cache = SqlError(ErrorCategory::Syntax, "Named parameter ':" + name + "' used in query but not bound.", "applyBindings");
                    return false;
                }
                m_ordered_transport_bind_params.emplace_back(mysql_helper::sqlValueToMySqlNativeValue(it->second));
            }
        } else {
            m_ordered_transport_bind_params.reserve(m_positional_bind_values.size());
            for (const auto& sql_val : m_positional_bind_values) {
                m_ordered_transport_bind_params.emplace_back(mysql_helper::sqlValueToMySqlNativeValue(sql_val));
            }
        }

        bool success = m_transport_statement->bindParams(m_ordered_transport_bind_params);
        if (!success) updateLastErrorCacheFromTransportStatement();
        return success;
    }

    void MySqlSpecificResult::addPositionalBindValue(const SqlValue& value, ParamType /*type*/) {
        m_positional_bind_values.push_back(value);
    }

    void MySqlSpecificResult::setNamedBindValue(const std::string& placeholder, const SqlValue& value, ParamType /*type*/) {
        std::string clean_placeholder = placeholder;
        if (!placeholder.empty() && (placeholder[0] == ':' || placeholder[0] == '@')) {
            clean_placeholder = placeholder.substr(1);
        }
        m_named_bind_values_map[clean_placeholder] = value;
    }

    void MySqlSpecificResult::clearBindValues() {
        m_positional_bind_values.clear();
        m_named_bind_values_map.clear();
        m_ordered_transport_bind_params.clear();
    }

    void MySqlSpecificResult::bindBlobStream(int /*pos*/, std::shared_ptr<std::istream> /*stream*/, long long /*size*/, ParamType /*type*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "BLOB streaming is not yet implemented.");
    }

    void MySqlSpecificResult::bindBlobStream(const std::string& /*placeholder*/, std::shared_ptr<std::istream> /*stream*/, long long /*size*/, ParamType /*type*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "BLOB streaming is not yet implemented.");
    }

    SqlValue MySqlSpecificResult::getOutParameter(int /*pos*/) const {
        return SqlValue();
    }

    SqlValue MySqlSpecificResult::getOutParameter(const std::string& /*name*/) const {
        return SqlValue();
    }

    std::map<std::string, SqlValue> MySqlSpecificResult::getAllOutParameters() const {
        return {};
    }

}  // namespace cpporm_sqldriver