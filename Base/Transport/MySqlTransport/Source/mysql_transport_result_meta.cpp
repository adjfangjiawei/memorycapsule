// cpporm_mysql_transport/mysql_transport_result_meta.cpp
#include <mysql/mysql.h>

#include "cpporm_mysql_transport/mysql_transport_result.h"

namespace cpporm_mysql_transport {

    void MySqlTransportResult::populateFieldsMeta() {
        if (m_meta_populated || !m_mysql_res_metadata || m_field_count == 0) {
            if (!m_mysql_res_metadata && m_field_count > 0 && m_is_valid) {
                m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "MYSQL_RES metadata handle is null in populateFieldsMeta when fields expected.");
                m_is_valid = false;
            }
            return;
        }

        m_fields_meta.clear();
        m_fields_meta.resize(m_field_count);
        MYSQL_FIELD* fields_raw = mysql_fetch_fields(m_mysql_res_metadata);
        if (!fields_raw) {
            m_error_collector = MySqlTransportError(MySqlTransportError::Category::InternalError, "mysql_fetch_fields returned null.");
            m_field_count = 0;
            m_is_valid = false;
            return;
        }

        for (unsigned int i = 0; i < m_field_count; ++i) {
            m_fields_meta[i].name = fields_raw[i].name ? fields_raw[i].name : "";
            m_fields_meta[i].original_name = fields_raw[i].org_name ? fields_raw[i].org_name : "";
            m_fields_meta[i].table = fields_raw[i].table ? fields_raw[i].table : "";
            m_fields_meta[i].original_table = fields_raw[i].org_table ? fields_raw[i].org_table : "";
            m_fields_meta[i].db = fields_raw[i].db ? fields_raw[i].db : "";
            m_fields_meta[i].catalog = fields_raw[i].catalog ? fields_raw[i].catalog : "def";
            m_fields_meta[i].native_type_id = fields_raw[i].type;
            m_fields_meta[i].charsetnr = fields_raw[i].charsetnr;
            m_fields_meta[i].length = fields_raw[i].length;
            m_fields_meta[i].max_length = fields_raw[i].max_length;
            m_fields_meta[i].flags = fields_raw[i].flags;
            m_fields_meta[i].decimals = fields_raw[i].decimals;
        }
        m_meta_populated = true;
        // Do not set m_is_valid = true here unconditionally.
        // It should be set by the constructor after all initialization steps, including this one, are successful.
    }

    const std::vector<MySqlTransportFieldMeta>& MySqlTransportResult::getFieldsMeta() const {
        return m_fields_meta;
    }

    std::optional<MySqlTransportFieldMeta> MySqlTransportResult::getFieldMeta(unsigned int col_idx) const {
        if (!m_is_valid || col_idx >= m_field_count) {
            return std::nullopt;
        }
        if (m_fields_meta.size() <= col_idx) return std::nullopt;
        return m_fields_meta[col_idx];
    }

    std::optional<MySqlTransportFieldMeta> MySqlTransportResult::getFieldMeta(const std::string& col_name) const {
        if (!m_is_valid) return std::nullopt;
        int idx = getFieldIndex(col_name);
        if (idx == -1) return std::nullopt;
        return m_fields_meta[static_cast<size_t>(idx)];
    }

    int MySqlTransportResult::getFieldIndex(const std::string& col_name) const {
        if (!m_is_valid || !m_meta_populated) return -1;  // Ensure meta is populated
        for (size_t i = 0; i < m_fields_meta.size(); ++i) {
            if (m_fields_meta[i].name == col_name || (!m_fields_meta[i].original_name.empty() && m_fields_meta[i].original_name == col_name)) {  // Check original_name too
                return static_cast<int>(i);
            }
        }
        return -1;
    }

}  // namespace cpporm_mysql_transport