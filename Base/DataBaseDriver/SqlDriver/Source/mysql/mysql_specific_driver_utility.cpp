// SqlDriver/Source/mysql/mysql_specific_driver_utility.cpp
#include <sstream>

#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_types.h"
#include "mysql_protocol/mysql_type_converter.h"
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"

namespace cpporm_sqldriver {

    std::string MySqlSpecificDriver::formatValue(const SqlValue& value, SqlValueType, const SqlField*) const {
        if (!m_transport_connection) {
            if (value.isNull()) return "NULL";
            bool conv_ok = false;
            std::string s_val = value.toString(&conv_ok);
            if (!conv_ok) return "NULL /* CONVERSION ERROR TO STRING */";

            SqlValueType v_type = value.type();
            if (v_type == SqlValueType::String || v_type == SqlValueType::FixedString || v_type == SqlValueType::CharacterLargeObject || v_type == SqlValueType::Json || v_type == SqlValueType::Xml || v_type == SqlValueType::Date || v_type == SqlValueType::Time || v_type == SqlValueType::DateTime ||
                v_type == SqlValueType::Timestamp) {
                std::string temp_s_val;
                temp_s_val.reserve(s_val.length() + 2);
                temp_s_val += '\'';
                for (char c : s_val) {
                    if (c == '\'')
                        temp_s_val += "''";
                    else
                        temp_s_val += c;
                }
                temp_s_val += '\'';
                return temp_s_val + " /* NO_CONN_LITERAL_UNSAFE_ESCAPE */";
            } else if (v_type == SqlValueType::ByteArray || v_type == SqlValueType::BinaryLargeObject) {
                return "'BLOB_DATA_UNFORMATTED_NO_CONN_UNSAFE'";
            }
            return s_val;
        }
        mysql_protocol::MySqlNativeValue native_value = mysql_helper::sqlValueToMySqlNativeValue(value);
        return m_transport_connection->formatNativeValueAsLiteral(native_value);
    }

    std::string MySqlSpecificDriver::escapeIdentifier(const std::string& identifier, IdentifierType) const {
        if (!m_transport_connection) {
            if (identifier.empty()) return "``";
            std::string escaped_id = "`";
            for (char c : identifier) {
                if (c == '`')
                    escaped_id += "``";
                else
                    escaped_id += c;
            }
            escaped_id += '`';
            return escaped_id + " /* NO_CONN_BASIC_ESCAPE */";
        }
        return m_transport_connection->escapeSqlIdentifier(identifier);
    }

    // ***** 新增: escapeString 实现 *****
    std::string MySqlSpecificDriver::escapeString(const std::string& unescaped_string) {
        if (!isOpen() || !m_transport_connection) {
            // 提供一个基本的、不安全的备用方案
            std::string escaped_str;
            for (char c : unescaped_string) {
                if (c == '\'') {
                    escaped_str += "''";
                } else if (c == '\\') {
                    escaped_str += "\\\\";
                } else {
                    escaped_str += c;
                }
            }
            return escaped_str;
        }
        // 调用 transport 层的实现
        return m_transport_connection->escapeString(unescaped_string, true);
    }

    std::string MySqlSpecificDriver::sqlStatement(StatementType type, const std::string& tableName, const SqlRecord& rec, bool prepared, const std::string& schema) const {
        // ... (此函数其余部分不变)
        if (tableName.empty()) return "";

        std::string current_schema_resolved = resolveSchemaName(schema);
        std::string fq_table_name;
        if (!current_schema_resolved.empty()) {
            fq_table_name = escapeIdentifier(current_schema_resolved, IdentifierType::Schema) + "." + escapeIdentifier(tableName, IdentifierType::Table);
        } else {
            fq_table_name = escapeIdentifier(tableName, IdentifierType::Table);
        }

        std::ostringstream oss;
        switch (type) {
            case StatementType::Select:
                oss << "SELECT ";
                if (rec.isEmpty() || rec.count() == 0) {
                    oss << "*";
                } else {
                    for (int i = 0; i < rec.count(); ++i) {
                        oss << (i > 0 ? ", " : "") << escapeIdentifier(rec.fieldName(i), IdentifierType::Field);
                    }
                }
                oss << " FROM " << fq_table_name;
                break;
            case StatementType::Insert:
                {
                    oss << "INSERT INTO " << fq_table_name;
                    if (rec.isEmpty() || rec.count() == 0) {
                        oss << " () VALUES ()";
                    } else {
                        std::string columns_part = " (";
                        std::string values_part = ") VALUES (";
                        bool first_col = true;

                        for (int i = 0; i < rec.count(); ++i) {
                            const SqlField& field = rec.field(i);
                            if (field.isAutoValue() && field.isPrimaryKeyPart() && field.value().isNull()) {
                                continue;
                            }

                            if (!first_col) {
                                columns_part += ", ";
                                values_part += ", ";
                            }
                            columns_part += escapeIdentifier(field.name(), IdentifierType::Field);
                            if (prepared) {
                                values_part += "?";
                            } else {
                                values_part += formatValue(field.value(), field.type(), &field);
                            }
                            first_col = false;
                        }
                        if (first_col) {
                            oss << " () VALUES ()";
                        } else {
                            oss << columns_part << values_part << ")";
                        }
                    }
                }
                break;
            case StatementType::Update:
                {
                    oss << "UPDATE " << fq_table_name << " SET ";
                    bool first_set = true;
                    bool has_updatable_column = false;
                    for (int i = 0; i < rec.count(); ++i) {
                        const SqlField& field = rec.field(i);
                        if (field.isPrimaryKeyPart() || field.isReadOnly()) {
                            continue;
                        }
                        has_updatable_column = true;
                        if (!first_set) oss << ", ";
                        oss << escapeIdentifier(field.name(), IdentifierType::Field) << " = ";
                        if (prepared) {
                            oss << "?";
                        } else {
                            oss << formatValue(field.value(), field.type(), &field);
                        }
                        first_set = false;
                    }
                    if (!has_updatable_column) {
                        return "";
                    }
                }
                break;
            case StatementType::Delete:
                oss << "DELETE FROM " << fq_table_name;
                break;
            default:
                return "";
        }
        return oss.str();
    }

    bool MySqlSpecificDriver::setClientCharset(const std::string& charsetName) {
        if (!m_transport_connection) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Transport connection not initialized.", "setClientCharset");
            return false;
        }
        bool success = m_transport_connection->setClientCharset(charsetName);
        updateLastErrorCacheFromTransport(success);
        if (success) {
            m_current_params_cache.setClientCharset(charsetName);
        }
        return success;
    }

    std::string MySqlSpecificDriver::clientCharset() const {
        if (!isOpen() || !m_transport_connection) {
            auto charset_opt_val = m_current_params_cache.clientCharset();
            return charset_opt_val.value_or("");
        }
        std::optional<std::string> charset_opt = m_transport_connection->getClientCharset();
        return charset_opt.value_or("");
    }

    SqlValue MySqlSpecificDriver::nextSequenceValue(const std::string& sequenceName, const std::string& schema) {
        if (!isOpen()) {
            m_last_error_cache = SqlError(ErrorCategory::Connectivity, "Connection not open.", "nextSequenceValue");
            return SqlValue();
        }
        if (sequenceName.empty()) {
            m_last_error_cache = SqlError(ErrorCategory::Syntax, "Sequence name cannot be empty.", "nextSequenceValue");
            return SqlValue();
        }
        if (!hasFeature(Feature::SequenceOperations)) {
            m_last_error_cache = SqlError(ErrorCategory::FeatureNotSupported, "Sequence operations not supported by this driver/database version.", "nextSequenceValue");
            return SqlValue();
        }

        std::string current_schema_resolved = resolveSchemaName(schema);
        std::string fq_seq_name;
        if (!current_schema_resolved.empty()) {
            fq_seq_name = escapeIdentifier(current_schema_resolved, IdentifierType::Schema) + "." + escapeIdentifier(sequenceName, IdentifierType::Sequence);
        } else {
            fq_seq_name = escapeIdentifier(sequenceName, IdentifierType::Sequence);
        }

        std::string query_str = "SELECT NEXT VALUE FOR " + fq_seq_name;

        std::unique_ptr<SqlResult> result = createResult();
        if (!result) {
            m_last_error_cache = SqlError(ErrorCategory::DriverInternal, "Failed to create result object for sequence.", "nextSequenceValue");
            return SqlValue();
        }

        if (!result->prepare(query_str, nullptr, SqlResultNs::ScrollMode::ForwardOnly, SqlResultNs::ConcurrencyMode::ReadOnly)) {
            m_last_error_cache = result->error();
            return SqlValue();
        }
        if (!result->exec()) {
            m_last_error_cache = result->error();
            return SqlValue();
        }
        SqlRecord temp_rec;
        if (result->fetchNext(temp_rec) && temp_rec.count() > 0) {
            m_last_error_cache = SqlError();
            return temp_rec.value(0);
        } else {
            m_last_error_cache = result->error();
            if (m_last_error_cache.category() == ErrorCategory::NoError) {
                m_last_error_cache = SqlError(ErrorCategory::DataRelated, "Sequence query returned no rows or no value.", "nextSequenceValue", "", 0, query_str);
            }
        }
        return SqlValue();
    }

}  // namespace cpporm_sqldriver