// SqlDriver/Include/sqldriver/mysql/mysql_driver_helper.h
#pragma once

#include "sqldriver/sql_connection_parameters.h"
#include "sqldriver/sql_enums.h"
#include "sqldriver/sql_error.h"
#include "sqldriver/sql_field.h"
#include "sqldriver/sql_index.h"
#include "sqldriver/sql_record.h"
#include "sqldriver/sql_value.h"

// Transport 层暴露的类型 - 需要包含这些头文件以获得完整类型定义
#include "cpporm_mysql_transport/mysql_transport_connection.h"
#include "cpporm_mysql_transport/mysql_transport_metadata.h"
#include "cpporm_mysql_transport/mysql_transport_result.h"
#include "cpporm_mysql_transport/mysql_transport_statement.h"
#include "cpporm_mysql_transport/mysql_transport_types.h"

// Protocol 层暴露的类型 (作为与 SqlValue 转换的桥梁)
#include "mysql_protocol/mysql_type_converter.h"

// 前向声明 MYSQL 结构体 (来自 C API)
struct st_mysql;  // MYSQL 由 mysql.h 定义，通常由 transport 层头文件包含

namespace cpporm_sqldriver {

    namespace mysql_helper {

        // --- 在 mysql_param_converter.cpp 中实现的声明 ---
        // 将 ConnectionParameters (SqlDriver 层) 转换为 MySqlTransportConnectionParams (Transport 层)
        ::cpporm_mysql_transport::MySqlTransportConnectionParams toMySqlTransportParams(const ConnectionParameters& params);

        // --- 在 mysql_error_converter.cpp 中实现的声明 ---
        // 将 MySqlTransportError (Transport 层) 转换为 SqlError (SqlDriver 层)
        SqlError transportErrorToSqlError(const ::cpporm_mysql_transport::MySqlTransportError& transportError);
        // 将 MySqlProtocolError (Protocol 层) 转换为 SqlError (SqlDriver 层)
        SqlError protocolErrorToSqlError(const mysql_protocol::MySqlProtocolError& protocolError, const std::string& context_message = "");

        // --- 在 mysql_value_converter.cpp 中实现的声明 ---
        // 将 SqlValue (SqlDriver 层) 转换为 MySqlNativeValue (Protocol 层，用于 Transport)
        mysql_protocol::MySqlNativeValue sqlValueToMySqlNativeValue(const SqlValue& value);
        // 将 MySqlNativeValue (Protocol 层) 转换为 SqlValue (SqlDriver 层)
        SqlValue mySqlNativeValueToSqlValue(const mysql_protocol::MySqlNativeValue& nativeValue);

        // --- 在 mysql_metadata_converter.cpp 中实现的声明 ---
        // 将 MySqlTransportFieldMeta (Transport 层) 转换为 SqlField (SqlDriver 层)
        SqlField metaToSqlField(const ::cpporm_mysql_transport::MySqlTransportFieldMeta& transportMeta);
        // 将 MySqlTransportFieldMeta 向量转换为 SqlRecord (SqlDriver 层)
        SqlRecord metasToSqlRecord(const std::vector<::cpporm_mysql_transport::MySqlTransportFieldMeta>& transportMetas);
        // 将 MySqlTransportIndexInfo (Transport 层) 转换为 SqlIndex (SqlDriver 层)
        SqlIndex metaToSqlIndex(const ::cpporm_mysql_transport::MySqlTransportIndexInfo& transportIndexInfo);
        // 将 MySqlTransportIndexInfo 向量转换为 SqlIndex 向量 (SqlDriver 层)
        std::vector<SqlIndex> metasToSqlIndexes(const std::vector<::cpporm_mysql_transport::MySqlTransportIndexInfo>& transportIndexInfos);

        // --- 在 mysql_enum_converter.cpp 中实现的声明 ---
        // 事务隔离级别枚举转换
        ::cpporm_mysql_transport::TransactionIsolationLevel toMySqlTransportIsolationLevel(TransactionIsolationLevel driverLevel);
        TransactionIsolationLevel fromMySqlTransportIsolationLevel(::cpporm_mysql_transport::TransactionIsolationLevel transportLevel);

        // --- 在 mysql_placeholder_processor.cpp 中实现的声明 ---
        // 处理命名占位符的结构体和函数
        struct NamedPlaceholderInfo {
            std::string processedQuery;                                // 处理后（通常替换为 '?'）的查询字符串
            std::vector<std::string> orderedParamNames;                // 命名参数按其在查询中出现的顺序
            std::map<std::string, std::vector<int>> nameToIndicesMap;  // 参数名到其在原始查询中出现位置的映射 (0-based)
            bool hasNamedPlaceholders = false;                         // 原始查询是否包含命名占位符
        };
        NamedPlaceholderInfo processQueryForPlaceholders(const std::string& originalQuery, SqlResultNs::NamedBindingSyntax syntax);

        // --- 在 mysql_type_mapper.cpp 中实现的声明 ---
        // 将 MySQL C API 的列类型 ID (enum_field_types) 映射到 SqlValueType (SqlDriver 层)
        SqlValueType mySqlColumnTypeToSqlValueType(int mysql_col_type_id, unsigned int mysql_flags);

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver