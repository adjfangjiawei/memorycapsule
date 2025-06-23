#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"

namespace cpporm_sqldriver {

    bool MySqlSpecificResult::fetchNext(SqlRecord& record_buffer) {
        if (!ensureResultSet()) return false;
        clearLastErrorCache();
        record_buffer.clear();

        if (m_transport_result_set->fetchNextRow()) {
            m_current_row_index++;
            const auto& fields_meta_transport = m_transport_result_set->getFieldsMeta();
            for (unsigned int i = 0; i < fields_meta_transport.size(); ++i) {
                SqlField sql_driver_field = mysql_helper::metaToSqlField(fields_meta_transport[i]);
                auto native_val_opt = m_transport_result_set->getValue(i);
                if (native_val_opt) {
                    sql_driver_field.setValue(mysql_helper::mySqlNativeValueToSqlValue(*native_val_opt));
                } else {
                    sql_driver_field.setValue(SqlValue());  // Null
                }
                record_buffer.append(sql_driver_field);
            }
            m_current_record_buffer_cache = record_buffer;
            return true;
        } else {
            updateLastErrorCacheFromTransportResult();
            m_current_row_index = -1;
            m_current_record_buffer_cache.clear();
            return false;
        }
    }

    bool MySqlSpecificResult::fetchPrevious(SqlRecord& /*record_buffer*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "fetchPrevious is not supported by this driver.");
        return false;
    }

    bool MySqlSpecificResult::fetchFirst(SqlRecord& /*record_buffer*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "fetchFirst is not supported by this driver.");
        return false;
    }

    bool MySqlSpecificResult::fetchLast(SqlRecord& /*record_buffer*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "fetchLast is not supported by this driver.");
        return false;
    }

    bool MySqlSpecificResult::fetch(int /*index*/, SqlRecord& /*record_buffer*/, CursorMovement /*movement*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "fetch(index) is not supported by this driver.");
        return false;
    }

}  // namespace cpporm_sqldriver