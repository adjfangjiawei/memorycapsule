// cpporm_mysql_transport/mysql_transport_index_lister.cpp
#include "cpporm_mysql_transport/mysql_transport_index_lister.h"

#include <mysql/mysql.h>

#include <algorithm>
#include <map>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "mysql_protocol/mysql_type_converter.h"  // For MySqlNativeValue

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
        if (!stmt || (stmt->getNativeStatementHandle() == nullptr && stmt->getError().category != MySqlTransportError::Category::NoError)) {
            setErrorFromConnection_("Failed to create statement for getTableIndexes for " + fq_table_name);
            if (stmt) m_last_error = stmt->getError();  // Prefer statement's error if available
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
        int idx_visible = result->getFieldIndex("Visible");
        int idx_expression = result->getFieldIndex("Expression");

        if (idx_key_name == -1 || idx_column_name == -1 || idx_seq_in_index == -1 || idx_table == -1 || idx_non_unique == -1 || idx_index_type == -1 || idx_null == -1) {
            setError_(MySqlTransportError::Category::InternalError, "Could not find one or more required columns in SHOW INDEX output.");
            return std::nullopt;
        }

        while (result->fetchNextRow()) {
            std::string key_name;
            std::optional<mysql_protocol::MySqlNativeValue> key_name_native_val_opt = result->getValue(static_cast<unsigned int>(idx_key_name));
            if (key_name_native_val_opt) {
                auto key_name_str_opt = key_name_native_val_opt->get_if<std::string>();
                if (key_name_str_opt) {
                    key_name = *key_name_str_opt;
                } else
                    continue;
            } else
                continue;

            auto it = index_map.find(key_name);
            if (it == index_map.end()) {
                MySqlTransportIndexInfo index_info;

                if (auto table_native_opt = result->getValue(static_cast<unsigned int>(idx_table))) {
                    if (auto table_str_opt = table_native_opt->get_if<std::string>()) index_info.tableName = *table_str_opt;
                }

                if (auto non_unique_native_opt = result->getValue(static_cast<unsigned int>(idx_non_unique))) {
                    bool val_set = false;
                    if (auto pval_u64_opt = non_unique_native_opt->get_if<uint64_t>()) {
                        index_info.isNonUnique = (*pval_u64_opt == 1);
                        val_set = true;
                    } else if (auto pval_i64_opt = non_unique_native_opt->get_if<int64_t>()) {
                        index_info.isNonUnique = (*pval_i64_opt == 1);
                        val_set = true;
                    } else if (auto pval_u32_opt = non_unique_native_opt->get_if<uint32_t>()) {
                        index_info.isNonUnique = (*pval_u32_opt == 1);
                        val_set = true;
                    } else if (auto pval_i32_opt = non_unique_native_opt->get_if<int32_t>()) {
                        index_info.isNonUnique = (*pval_i32_opt == 1);
                        val_set = true;
                    }
                    // Add other int types if MySqlNativeValue can hold them for Non_unique
                    if (!val_set) index_info.isNonUnique = true;  // Default if type mismatch
                }
                index_info.indexName = key_name;

                if (auto type_native_opt = result->getValue(static_cast<unsigned int>(idx_index_type))) {
                    if (auto type_str_opt = type_native_opt->get_if<std::string>()) index_info.indexType = *type_str_opt;
                }

                if (idx_comment != -1) {
                    if (auto comment_native_opt = result->getValue(static_cast<unsigned int>(idx_comment))) {
                        if (!comment_native_opt->is_null())
                            if (auto comment_str_opt = comment_native_opt->get_if<std::string>()) index_info.comment = *comment_str_opt;
                    }
                }
                if (idx_index_comment != -1) {
                    if (auto idx_comment_native_opt = result->getValue(static_cast<unsigned int>(idx_index_comment))) {
                        if (!idx_comment_native_opt->is_null())
                            if (auto idx_comment_str_opt = idx_comment_native_opt->get_if<std::string>()) index_info.indexComment = *idx_comment_str_opt;
                    }
                }
                if (idx_visible != -1) {
                    if (auto visible_native_opt = result->getValue(static_cast<unsigned int>(idx_visible))) {
                        if (!visible_native_opt->is_null())
                            if (auto visible_str_opt = visible_native_opt->get_if<std::string>())
                                index_info.isVisible = (*visible_str_opt == "YES" || *visible_str_opt == "1");  // MySQL 8 uses YES/NO
                            else if (auto visible_int_opt = visible_native_opt->get_if<int64_t>())
                                index_info.isVisible = (*visible_int_opt == 1);  // Older might use 1/0
                            else if (auto visible_uint_opt = visible_native_opt->get_if<uint64_t>())
                                index_info.isVisible = (*visible_uint_opt == 1);
                        // Add other numeric types if necessary
                    }
                } else {
                    index_info.isVisible = true;  // Default if column not present
                }
                it = index_map.insert({key_name, index_info}).first;
            }

            MySqlTransportIndexColumn col_def;
            if (auto col_name_native_opt = result->getValue(static_cast<unsigned int>(idx_column_name))) {
                if (auto col_name_str_opt = col_name_native_opt->get_if<std::string>())
                    col_def.columnName = *col_name_str_opt;
                else
                    continue;
            } else
                continue;

            if (auto seq_native_opt = result->getValue(static_cast<unsigned int>(idx_seq_in_index))) {
                if (auto pval_u64_opt = seq_native_opt->get_if<uint64_t>())
                    col_def.sequenceInIndex = static_cast<unsigned int>(*pval_u64_opt);
                else if (auto pval_i64_opt = seq_native_opt->get_if<int64_t>())
                    col_def.sequenceInIndex = static_cast<unsigned int>(*pval_i64_opt);
                else if (auto pval_u32_opt = seq_native_opt->get_if<uint32_t>())
                    col_def.sequenceInIndex = *pval_u32_opt;
                else if (auto pval_i32_opt = seq_native_opt->get_if<int32_t>())
                    col_def.sequenceInIndex = static_cast<unsigned int>(*pval_i32_opt);
            }

            if (idx_collation != -1) {
                if (auto coll_native_opt = result->getValue(static_cast<unsigned int>(idx_collation))) {
                    if (!coll_native_opt->is_null())
                        if (auto coll_str_opt = coll_native_opt->get_if<std::string>()) col_def.collation = *coll_str_opt;
                }
            }
            if (idx_cardinality != -1) {
                if (auto card_native_opt = result->getValue(static_cast<unsigned int>(idx_cardinality))) {
                    if (!card_native_opt->is_null()) {
                        if (auto pval_u64_opt = card_native_opt->get_if<uint64_t>())
                            col_def.cardinality = static_cast<long long>(*pval_u64_opt);
                        else if (auto pval_i64_opt = card_native_opt->get_if<int64_t>())
                            col_def.cardinality = *pval_i64_opt;
                        else if (auto pval_u32_opt = card_native_opt->get_if<uint32_t>())
                            col_def.cardinality = static_cast<long long>(*pval_u32_opt);
                        else if (auto pval_i32_opt = card_native_opt->get_if<int32_t>())
                            col_def.cardinality = *pval_i32_opt;
                    }
                }
            }
            if (idx_sub_part != -1) {
                if (auto sub_part_native_opt = result->getValue(static_cast<unsigned int>(idx_sub_part))) {
                    if (!sub_part_native_opt->is_null()) {
                        if (auto pval_u64_opt = sub_part_native_opt->get_if<uint64_t>())
                            col_def.subPart = static_cast<unsigned int>(*pval_u64_opt);
                        else if (auto pval_i64_opt = sub_part_native_opt->get_if<int64_t>())
                            col_def.subPart = static_cast<unsigned int>(*pval_i64_opt);
                        else if (auto pval_u32_opt = sub_part_native_opt->get_if<uint32_t>())
                            col_def.subPart = *pval_u32_opt;
                        else if (auto pval_i32_opt = sub_part_native_opt->get_if<int32_t>())
                            col_def.subPart = static_cast<unsigned int>(*pval_i32_opt);
                    }
                }
            }

            if (auto null_col_native_opt = result->getValue(static_cast<unsigned int>(idx_null))) {
                if (!null_col_native_opt->is_null()) {
                    if (auto null_str_opt = null_col_native_opt->get_if<std::string>()) col_def.isNullable = (*null_str_opt == "YES");
                } else
                    col_def.isNullable = false;
            } else
                col_def.isNullable = false;

            if (idx_expression != -1) {
                if (auto expr_native_opt = result->getValue(static_cast<unsigned int>(idx_expression))) {
                    if (!expr_native_opt->is_null())
                        if (auto expr_str_opt = expr_native_opt->get_if<std::string>()) col_def.expression = *expr_str_opt;
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