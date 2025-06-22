// cpporm_mysql_transport/mysql_transport_index_lister.cpp
#include <mysql/mysql.h>

#include <algorithm>
#include <map>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_index_lister.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "mysql_protocol/mysql_type_converter.h"

namespace cpporm_mysql_transport {

    MySqlTransportIndexLister::MySqlTransportIndexLister(MySqlTransportConnection* connection_context) : m_conn_ctx(connection_context) {
        if (!m_conn_ctx) {
            setError_(MySqlTransportError::Category::InternalError, "IndexLister: Null connection context provided.");
        }
    }

    void MySqlTransportIndexLister::clearError_() {
        m_last_error = MySqlTransportError();
    }

    void MySqlTransportIndexLister::setError_(MySqlTransportError::Category cat, const std::string& msg) {
        m_last_error = MySqlTransportError(cat, msg);
    }

    void MySqlTransportIndexLister::setErrorFromConnection_(const std::string& context) {
        if (m_conn_ctx) {
            m_last_error = m_conn_ctx->getLastError();
            std::string combined_msg = context;
            if (!m_last_error.message.empty()) {
                if (!combined_msg.empty()) combined_msg += ": ";
                combined_msg += m_last_error.message;
            }
            m_last_error.message = combined_msg;
            if (m_last_error.isOk() && !context.empty() && context.find("Failed to create statement") != std::string::npos) {
                m_last_error.category = MySqlTransportError::Category::QueryError;
            } else if (m_last_error.isOk() && !context.empty()) {
                m_last_error.category = MySqlTransportError::Category::InternalError;
            }
        } else {
            setError_(MySqlTransportError::Category::InternalError, context.empty() ? "Lister: Connection context is null." : context + ": Connection context is null.");
        }
    }

    std::optional<std::vector<MySqlTransportIndexInfo>> MySqlTransportIndexLister::getTableIndexes(const std::string& table_name, const std::string& db_name_filter_param) {
        if (!m_conn_ctx || !m_conn_ctx->isConnected()) {
            setError_(MySqlTransportError::Category::ConnectionError, "Not connected for getTableIndexes.");
            return std::nullopt;
        }
        if (table_name.empty()) {
            setError_(MySqlTransportError::Category::ApiUsageError, "Table name cannot be empty for getTableIndexes.");
            return std::nullopt;
        }
        clearError_();

        std::string db_to_use = db_name_filter_param;
        if (db_to_use.empty()) {
            db_to_use = m_conn_ctx->getCurrentParams().db_name;
            if (db_to_use.empty()) {
                setError_(MySqlTransportError::Category::ApiUsageError, "Database name not specified and not set in connection for getTableIndexes.");
                return std::nullopt;
            }
        }
        std::string fq_table_name = "`" + m_conn_ctx->escapeString(db_to_use, false) + "`.`" + m_conn_ctx->escapeString(table_name, false) + "`";
        std::string query = "SHOW INDEX FROM " + fq_table_name;

        std::unique_ptr<MySqlTransportStatement> stmt = m_conn_ctx->createStatement(query);
        if (!stmt || (stmt->getNativeStatementHandle() == nullptr && !stmt->getError().isOk())) {
            setErrorFromConnection_("Failed to create statement for getTableIndexes for " + fq_table_name);
            if (stmt && !stmt->getError().isOk()) {  // If stmt was created but failed internally
                m_last_error = stmt->getError();
            }
            return std::nullopt;
        }
        std::unique_ptr<MySqlTransportResult> result = stmt->executeQuery();
        if (!result || !result->isValid()) {
            m_last_error = stmt->getError();
            return std::nullopt;
        }

        std::map<std::string, MySqlTransportIndexInfo> index_map;

        int idx_table = result->getFieldIndex("Table");
        int idx_non_unique = result->getFieldIndex("Non_unique");
        int idx_key_name = result->getFieldIndex("Key_name");
        int idx_seq_in_index = result->getFieldIndex("Seq_in_index");
        int idx_column_name = result->getFieldIndex("Column_name");
        int idx_collation = result->getFieldIndex("Collation");
        int idx_cardinality = result->getFieldIndex("Cardinality");
        int idx_sub_part = result->getFieldIndex("Sub_part");
        int idx_null = result->getFieldIndex("Null");
        int idx_index_type = result->getFieldIndex("Index_type");
        int idx_comment = result->getFieldIndex("Comment");
        int idx_index_comment = result->getFieldIndex("Index_comment");
        int idx_visible = result->getFieldIndex("Visible");        // MySQL 8+
        int idx_expression = result->getFieldIndex("Expression");  // MySQL 8+ for functional indexes

        if (idx_key_name == -1 || idx_column_name == -1 || idx_seq_in_index == -1 || idx_table == -1 || idx_non_unique == -1 || idx_index_type == -1) {  // idx_null might not always be present or critical in all versions/outputs
            setError_(MySqlTransportError::Category::InternalError, "Could not find one or more required columns in SHOW INDEX output.");
            return std::nullopt;
        }

        while (result->fetchNextRow()) {
            std::string key_name_str_val;
            if (auto key_name_native_opt = result->getValue(static_cast<unsigned int>(idx_key_name))) {
                if (auto key_name_str_opt = key_name_native_opt->get_if<std::string>()) {
                    key_name_str_val = *key_name_str_opt;
                } else {
                    continue;
                }
            } else {
                continue;
            }

            auto it = index_map.find(key_name_str_val);
            if (it == index_map.end()) {
                MySqlTransportIndexInfo index_info;

                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_table))) {
                    if (auto s_opt = val_opt->get_if<std::string>()) {
                        index_info.tableName = *s_opt;
                    }
                }

                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_non_unique))) {
                    bool nu_val_set = false;
                    if (auto pval_u64_opt = val_opt->get_if<uint64_t>()) {
                        index_info.isNonUnique = (*pval_u64_opt == 1);
                        nu_val_set = true;
                    } else if (auto pval_i64_opt = val_opt->get_if<int64_t>()) {
                        index_info.isNonUnique = (*pval_i64_opt == 1);
                        nu_val_set = true;
                    } else if (auto pval_u32_opt = val_opt->get_if<uint32_t>()) {
                        index_info.isNonUnique = (*pval_u32_opt == 1);
                        nu_val_set = true;
                    } else if (auto pval_i32_opt = val_opt->get_if<int32_t>()) {
                        index_info.isNonUnique = (*pval_i32_opt == 1);
                        nu_val_set = true;
                    }
                    if (!nu_val_set) index_info.isNonUnique = true;  // Default
                }
                index_info.indexName = key_name_str_val;

                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_index_type))) {
                    if (auto s_opt = val_opt->get_if<std::string>()) {
                        index_info.indexType = *s_opt;
                    }
                }

                if (idx_comment != -1) {  // Check if column exists
                    if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_comment))) {
                        if (!val_opt->is_null()) {
                            if (auto s_opt = val_opt->get_if<std::string>()) {
                                index_info.comment = *s_opt;
                            }
                        }
                    }
                }
                if (idx_index_comment != -1) {  // Check if column exists
                    if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_index_comment))) {
                        if (!val_opt->is_null()) {
                            if (auto s_opt = val_opt->get_if<std::string>()) {
                                index_info.indexComment = *s_opt;
                            }
                        }
                    }
                }
                if (idx_visible != -1) {  // Check if column exists (MySQL 8+)
                    if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_visible))) {
                        if (!val_opt->is_null()) {
                            if (auto s_opt = val_opt->get_if<std::string>()) {
                                index_info.isVisible = (*s_opt == "YES" || *s_opt == "1");
                            }
                            // else if other types for visible...
                        } else {
                            index_info.isVisible = true;
                        }  // Assuming NULL means visible or default
                    } else {
                        index_info.isVisible = true;
                    }  // Default if value is not there
                } else {
                    index_info.isVisible = true;  // Default for older MySQL
                }
                it = index_map.insert({key_name_str_val, index_info}).first;
            }

            MySqlTransportIndexColumn col_def;
            if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_column_name))) {
                if (auto s_opt = val_opt->get_if<std::string>()) {
                    col_def.columnName = *s_opt;
                } else {
                    continue;
                }
            } else {
                continue;
            }

            if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_seq_in_index))) {
                if (auto pval_u64_opt = val_opt->get_if<uint64_t>()) {
                    col_def.sequenceInIndex = static_cast<unsigned int>(*pval_u64_opt);
                } else if (auto pval_i64_opt = val_opt->get_if<int64_t>()) {
                    col_def.sequenceInIndex = static_cast<unsigned int>(*pval_i64_opt);
                } else if (auto pval_u32_opt = val_opt->get_if<uint32_t>()) {
                    col_def.sequenceInIndex = *pval_u32_opt;
                } else if (auto pval_i32_opt = val_opt->get_if<int32_t>()) {
                    col_def.sequenceInIndex = static_cast<unsigned int>(*pval_i32_opt);
                }
            }

            if (idx_collation != -1) {  // Check if column exists
                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_collation))) {
                    if (!val_opt->is_null()) {
                        if (auto s_opt = val_opt->get_if<std::string>()) {
                            col_def.collation = *s_opt;
                        }
                    }
                }
            }
            if (idx_cardinality != -1) {  // Check if column exists
                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_cardinality))) {
                    if (!val_opt->is_null()) {
                        if (auto pval_u64_opt = val_opt->get_if<uint64_t>()) {
                            col_def.cardinality = static_cast<long long>(*pval_u64_opt);
                        } else if (auto pval_i64_opt = val_opt->get_if<int64_t>()) {
                            col_def.cardinality = *pval_i64_opt;
                        } else if (auto pval_u32_opt = val_opt->get_if<uint32_t>()) {
                            col_def.cardinality = static_cast<long long>(*pval_u32_opt);
                        } else if (auto pval_i32_opt = val_opt->get_if<int32_t>()) {
                            col_def.cardinality = *pval_i32_opt;
                        }
                    }
                }
            }
            if (idx_sub_part != -1) {  // Check if column exists
                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_sub_part))) {
                    if (!val_opt->is_null()) {
                        if (auto pval_u64_opt = val_opt->get_if<uint64_t>()) {
                            col_def.subPart = static_cast<unsigned int>(*pval_u64_opt);
                        } else if (auto pval_i64_opt = val_opt->get_if<int64_t>()) {
                            col_def.subPart = static_cast<unsigned int>(*pval_i64_opt);
                        } else if (auto pval_u32_opt = val_opt->get_if<uint32_t>()) {
                            col_def.subPart = *pval_u32_opt;
                        } else if (auto pval_i32_opt = val_opt->get_if<int32_t>()) {
                            col_def.subPart = static_cast<unsigned int>(*pval_i32_opt);
                        }
                    }
                }
            }

            // Ensure idx_null is valid before using it
            if (idx_null != -1) {
                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_null))) {
                    if (!val_opt->is_null()) {
                        if (auto s_opt = val_opt->get_if<std::string>()) {
                            col_def.isNullable = (*s_opt == "YES");
                        }
                    } else {  // SQL NULL in "Null" column implies column is NOT NULL by convention of SHOW INDEX
                        col_def.isNullable = false;
                    }
                } else {  // If getValue returns no optional, assume not nullable for safety.
                    col_def.isNullable = false;
                }
            } else {  // If "Null" column is not present, assume not nullable as a fallback.
                col_def.isNullable = false;
            }

            if (idx_expression != -1) {  // Check if column exists (MySQL 8+)
                if (auto val_opt = result->getValue(static_cast<unsigned int>(idx_expression))) {
                    if (!val_opt->is_null()) {
                        if (auto s_opt = val_opt->get_if<std::string>()) {
                            col_def.expression = *s_opt;
                        }
                    }
                }
            }
            it->second.columns.push_back(col_def);
        }

        if (!result->getError().isOk()) {
            m_last_error = result->getError();
        }

        std::vector<MySqlTransportIndexInfo> indexes_vec;
        indexes_vec.reserve(index_map.size());
        for (auto& pair_kv : index_map) {
            std::sort(pair_kv.second.columns.begin(), pair_kv.second.columns.end(), [](const MySqlTransportIndexColumn& a, const MySqlTransportIndexColumn& b) {
                return a.sequenceInIndex < b.sequenceInIndex;
            });
            indexes_vec.push_back(std::move(pair_kv.second));
        }
        return indexes_vec;
    }

    std::optional<MySqlTransportIndexInfo> MySqlTransportIndexLister::getPrimaryIndex(const std::string& table_name, const std::string& db_name_filter) {
        auto indexes_opt = getTableIndexes(table_name, db_name_filter);
        if (indexes_opt) {
            for (const auto& index : indexes_opt.value()) {
                if (index.indexName == "PRIMARY") {
                    return index;
                }
            }
        }
        return std::nullopt;
    }

    MySqlTransportError MySqlTransportIndexLister::getLastError() const {
        return m_last_error;
    }

}  // namespace cpporm_mysql_transport