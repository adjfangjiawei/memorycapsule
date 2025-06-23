#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"

namespace cpporm_sqldriver {

    SqlValue MySqlSpecificResult::data(int column_index) {
        if (!ensureResultSet()) return SqlValue();
        if (m_current_row_index < 0 || column_index < 0 || static_cast<unsigned int>(column_index) >= (m_transport_result_set ? m_transport_result_set->getFieldCount() : 0)) {
            m_last_error_cache = SqlError(ErrorCategory::DataRelated, "Invalid index or no current row for data().", "data");
            return SqlValue();
        }
        clearLastErrorCache();
        auto native_val_opt = m_transport_result_set->getValue(static_cast<unsigned int>(column_index));
        if (native_val_opt) {
            return mysql_helper::mySqlNativeValueToSqlValue(*native_val_opt);
        } else {
            updateLastErrorCacheFromTransportResult();
            return SqlValue();
        }
    }

    bool MySqlSpecificResult::isNull(int column_index) {
        if (!ensureResultSet()) return true;
        if (m_current_row_index < 0 || column_index < 0 || static_cast<unsigned int>(column_index) >= (m_transport_result_set ? m_transport_result_set->getFieldCount() : 0)) {
            return true;
        }
        clearLastErrorCache();
        bool is_null_val = m_transport_result_set->isNull(static_cast<unsigned int>(column_index));
        if (!m_transport_result_set->getError().isOk()) {
            updateLastErrorCacheFromTransportResult();
        }
        return is_null_val;
    }

    SqlRecord MySqlSpecificResult::recordMetadata() const {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return mysql_helper::metasToSqlRecord(m_transport_result_set->getFieldsMeta());
        }
        return SqlRecord();
    }

    SqlRecord MySqlSpecificResult::currentFetchedRow() const {
        return m_current_record_buffer_cache;
    }

    SqlField MySqlSpecificResult::field(int column_index) const {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            auto transport_field_meta_opt = m_transport_result_set->getFieldMeta(static_cast<unsigned int>(column_index));
            if (transport_field_meta_opt) {
                return mysql_helper::metaToSqlField(*transport_field_meta_opt);
            }
        }
        return SqlField();
    }

    long long MySqlSpecificResult::numRowsAffected() {
        return static_cast<long long>(m_num_rows_affected_cache);
    }

    SqlValue MySqlSpecificResult::lastInsertId() {
        return m_last_insert_id_cache;
    }

    int MySqlSpecificResult::columnCount() const {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return static_cast<int>(m_transport_result_set->getFieldCount());
        }
        return 0;
    }

    int MySqlSpecificResult::size() {
        if (m_transport_result_set && m_transport_result_set->isValid()) {
            return static_cast<int>(m_transport_result_set->getRowCount());
        }
        return -1;
    }

    int MySqlSpecificResult::at() const {
        return static_cast<int>(m_current_row_index);
    }

    std::shared_ptr<std::istream> MySqlSpecificResult::openReadableBlobStream(int /*column_index*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "BLOB streaming is not yet implemented.");
        return nullptr;
    }

    std::shared_ptr<std::ostream> MySqlSpecificResult::openWritableBlobStream(int /*column_index*/, long long /*initial_size_hint*/) {
        m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "BLOB streaming is not yet implemented.");
        return nullptr;
    }

}  // namespace cpporm_sqldriver