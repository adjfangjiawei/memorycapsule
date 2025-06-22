// SqlDriver/Source/mysql/mysql_specific_driver_utility.cpp
#include <sstream>  // For sqlStatement

#include "cpporm_mysql_transport/mysql_transport_connection.h"  // For full type definition
#include "cpporm_mysql_transport/mysql_transport_types.h"       // For MySqlNativeValue
#include "mysql_protocol/mysql_type_converter.h"                // For MySqlNativeValue definition
#include "sqldriver/mysql/mysql_driver_helper.h"                // For value converters and SQL formatting
#include "sqldriver/mysql/mysql_specific_driver.h"
#include "sqldriver/mysql/mysql_specific_result.h"  // For nextSequenceValue which uses createResult

namespace cpporm_sqldriver {

    std::string MySqlSpecificDriver::formatValue(const SqlValue& value, SqlValueType /*type_hint*/, const SqlField* /*field_meta_hint*/) const {
        if (!m_transport_connection) {
            // 如果没有 transport 连接，进行基本的、不安全的格式化
            if (value.isNull()) return "NULL";
            bool conv_ok = false;
            std::string s_val = value.toString(&conv_ok);  // 尝试转换为字符串
            if (!conv_ok) return "NULL /* CONVERSION ERROR TO STRING */";

            SqlValueType v_type = value.type();
            // 对于字符串和日期时间类型，简单地用单引号包围
            // 注意：这种转义非常不安全，容易受到 SQL 注入攻击
            if (v_type == SqlValueType::String || v_type == SqlValueType::FixedString || v_type == SqlValueType::CharacterLargeObject || v_type == SqlValueType::Json || v_type == SqlValueType::Xml || v_type == SqlValueType::Date || v_type == SqlValueType::Time || v_type == SqlValueType::DateTime ||
                v_type == SqlValueType::Timestamp) {
                std::string temp_s_val;
                temp_s_val.reserve(s_val.length() + 2);
                temp_s_val += '\'';
                for (char c : s_val) {
                    if (c == '\'') temp_s_val += "''";  // 简单的单引号转义
                    // 没有处理反斜杠等其他特殊字符
                    else
                        temp_s_val += c;
                }
                temp_s_val += '\'';
                return temp_s_val + " /* NO_CONN_LITERAL_UNSAFE_ESCAPE */";
            } else if (v_type == SqlValueType::ByteArray || v_type == SqlValueType::BinaryLargeObject) {
                // 对于 BLOB，没有连接无法安全格式化为 X'...'
                return "'BLOB_DATA_UNFORMATTED_NO_CONN_UNSAFE'";
            }
            // 对于数字和布尔值，直接返回字符串形式
            return s_val;
        }
        // 使用 transport 连接进行安全的格式化
        mysql_protocol::MySqlNativeValue native_value = mysql_helper::sqlValueToMySqlNativeValue(value);
        return m_transport_connection->formatNativeValueAsLiteral(native_value);
    }

    std::string MySqlSpecificDriver::escapeIdentifier(const std::string& identifier, IdentifierType /*type*/) const {
        if (!m_transport_connection) {
            // 如果没有 transport 连接，进行基本的、不安全的标识符转义
            if (identifier.empty()) return "``";  // 空标识符
            std::string escaped_id = "`";
            for (char c : identifier) {
                if (c == '`')
                    escaped_id += "``";  // ` 转义为 ``
                else
                    escaped_id += c;
            }
            escaped_id += '`';
            return escaped_id + " /* NO_CONN_BASIC_ESCAPE */";
        }
        // 使用 transport 连接进行安全的标识符转义
        return m_transport_connection->escapeSqlIdentifier(identifier);
    }

    std::string MySqlSpecificDriver::sqlStatement(StatementType type, const std::string& tableName, const SqlRecord& rec, bool prepared, const std::string& schema) const {
        if (tableName.empty()) return "";

        std::string current_schema_resolved = resolveSchemaName(schema);
        std::string fq_table_name_part = escapeIdentifier(tableName, IdentifierType::Table);
        if (!current_schema_resolved.empty()) {
            fq_table_name_part = escapeIdentifier(current_schema_resolved, IdentifierType::Schema) + "." + fq_table_name_part;
        }
        // 注意：上面的 fq_table_name_part 可能已经被反引号包围，再次调用 escapeIdentifier 可能导致双重包围。
        // escapeIdentifier 应该能处理已经被引用的情况，或者这里逻辑调整。
        // 假设 escapeIdentifier 是幂等的或能正确处理。
        // 或者，我们只对未引用的部分调用 escapeIdentifier:
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
                // WHERE 子句通常不在这里生成，而是由查询构建器处理
                break;
            case StatementType::Insert:
                {
                    oss << "INSERT INTO " << fq_table_name;
                    if (rec.isEmpty() || rec.count() == 0) {  // 插入默认值
                        oss << " () VALUES ()";               // 或者 " DEFAULT VALUES" 取决于数据库
                    } else {
                        std::string columns_part = " (";
                        std::string values_part = ") VALUES (";
                        bool first_col = true;

                        for (int i = 0; i < rec.count(); ++i) {
                            const SqlField& field = rec.field(i);
                            // 对于 INSERT，通常不应跳过主键，除非它是自动生成的且值为 null
                            if (field.isAutoValue() && field.isPrimaryKeyPart() && field.value().isNull()) {
                                // 如果是自增主键且值为 NULL，则不应包含在列列表和值列表中，让数据库生成
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
                        if (first_col) {             // 没有列被添加到语句中（例如，只有一个自增主key）
                            oss << " () VALUES ()";  // MySQL/MariaDB 允许此语法
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
                        if (field.isPrimaryKeyPart() || field.isReadOnly()) {  // 不更新主键或只读字段
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
                    if (!has_updatable_column) {  // 没有可更新的列
                        return "";                // 返回空字符串表示无法生成有效的 UPDATE 语句
                    }
                    // WHERE 子句通常不在这里生成
                }
                break;
            case StatementType::Delete:
                oss << "DELETE FROM " << fq_table_name;
                // WHERE 子句通常不在这里生成
                break;
            default:
                return "";  // 不支持的语句类型
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
            // 更新缓存的连接参数
            m_current_params_cache.setClientCharset(charsetName);
        }
        return success;
    }

    std::string MySqlSpecificDriver::clientCharset() const {
        if (!isOpen() || !m_transport_connection) {  // 确保连接已打开
            // 如果未连接，从缓存参数中返回
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
        std::string fq_seq_name_part = escapeIdentifier(sequenceName, IdentifierType::Sequence);
        if (!current_schema_resolved.empty()) {
            fq_seq_name_part = escapeIdentifier(current_schema_resolved, IdentifierType::Schema) + "." + fq_seq_name_part;
        }
        // 与 sqlStatement 中的 fq_table_name 逻辑类似，确保正确引用
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

        // 使用 SqlResultNs::NamedBindingSyntax::QuestionMark，因为这是简单查询
        if (!result->prepare(query_str, nullptr, SqlResultNs::ScrollMode::ForwardOnly, SqlResultNs::ConcurrencyMode::ReadOnly)) {
            m_last_error_cache = result->error();
            return SqlValue();
        }
        if (!result->exec()) {
            m_last_error_cache = result->error();
            return SqlValue();
        }
        SqlRecord temp_rec;  // 缓冲区
        if (result->fetchNext(temp_rec) && temp_rec.count() > 0) {
            m_last_error_cache = SqlError();  // 清除之前的错误
            return temp_rec.value(0);         // 返回序列值
        } else {
            m_last_error_cache = result->error();                           // 获取 fetchNext 或 exec 的错误
            if (m_last_error_cache.category() == ErrorCategory::NoError) {  // 如果没有错误但没有行
                m_last_error_cache = SqlError(ErrorCategory::DataRelated, "Sequence query returned no rows or no value.", "nextSequenceValue", "", 0, query_str);
            }
        }
        return SqlValue();  // 返回空 SqlValue 表示失败
    }

}  // namespace cpporm_sqldriver