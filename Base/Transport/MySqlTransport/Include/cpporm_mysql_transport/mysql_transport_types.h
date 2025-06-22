#pragma once

#include <mysql/mysql.h>

#include <algorithm>  // For std::transform in toString example (if any)
#include <chrono>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "mysql_protocol/mysql_type_converter.h"

namespace cpporm_mysql_transport {

    // --- 错误处理 ---
    struct MySqlTransportError {
        enum class Category { NoError, ConnectionError, QueryError, DataError, ResourceError, TransactionError, InternalError, ProtocolError, ApiUsageError };

        Category category = Category::NoError;
        int native_mysql_errno = 0;
        std::string native_mysql_sqlstate;
        std::string native_mysql_error_msg;
        unsigned int protocol_internal_errc = 0;
        std::string message;
        std::string failed_query;

        MySqlTransportError() = default;
        MySqlTransportError(Category cat, std::string msg, int mysql_err = 0, const char* mysql_state = nullptr, const char* mysql_msg = nullptr, unsigned int proto_errc = 0, std::string query = "");
        bool isOk() const {
            return category == Category::NoError;
        }
        std::string toString() const;
    };

    // --- 连接参数 ---
    struct MySqlTransportConnectionParams {
        std::string host = "localhost";
        unsigned int port = 3306;
        std::string user;
        std::string password;
        std::string db_name;
        std::string unix_socket;
        unsigned long client_flag = 0;
        std::optional<unsigned int> connect_timeout_seconds;
        std::optional<unsigned int> read_timeout_seconds;
        std::optional<unsigned int> write_timeout_seconds;
        std::optional<std::string> charset;
        std::map<std::string, std::string> ssl_options;
        std::map<mysql_option, std::string> generic_options;
        std::map<std::string, std::string> init_commands;
    };

    // --- 事务隔离级别 ---
    enum class TransactionIsolationLevel { None, ReadUncommitted, ReadCommitted, RepeatableRead, Serializable };

    // --- 字段元数据 ---
    struct MySqlTransportFieldMeta {
        std::string name;
        std::string original_name;
        std::string table;
        std::string original_table;
        std::string db;
        std::string catalog = "def";
        enum enum_field_types native_type_id = MYSQL_TYPE_NULL;
        uint16_t charsetnr = 0;
        unsigned long length = 0;
        unsigned long max_length = 0;
        unsigned int flags = 0;
        unsigned int decimals = 0;
        mysql_protocol::MySqlNativeValue default_value;

        bool isPrimaryKey() const {
            return flags & PRI_KEY_FLAG;
        }
        bool isNotNull() const {
            return flags & NOT_NULL_FLAG;
        }
        bool isUniqueKey() const {
            return flags & UNIQUE_KEY_FLAG;
        }
        bool isMultipleKey() const {
            return flags & MULTIPLE_KEY_FLAG;
        }
        bool isAutoIncrement() const {
            return flags & AUTO_INCREMENT_FLAG;
        }
        bool isUnsigned() const {
            return flags & UNSIGNED_FLAG;
        }
        bool isZerofill() const {
            return flags & ZEROFILL_FLAG;
        }
        bool isBinary() const {
            return flags & BINARY_FLAG;
        }
        bool isEnum() const {
            return flags & ENUM_FLAG;
        }
        bool isSet() const {
            return flags & SET_FLAG;
        }
        bool isBlob() const {
            return flags & BLOB_FLAG;
        }
        bool isTimestamp() const {
            return flags & TIMESTAMP_FLAG;
        }
        bool isPartOfKey() const {
            return flags & PART_KEY_FLAG;
        }
        bool isGroup() const {
            return flags & GROUP_FLAG;
        }

        bool isGenerallyNumeric() const;
        bool isGenerallyString() const;
        bool isGenerallyDateTime() const;
    };

    // --- 参数绑定类型 ---
    struct MySqlTransportBindParam {
        mysql_protocol::MySqlNativeValue value;

        MySqlTransportBindParam();
        MySqlTransportBindParam(const mysql_protocol::MySqlNativeValue& v);
        MySqlTransportBindParam(mysql_protocol::MySqlNativeValue&& v);
        MySqlTransportBindParam(std::nullptr_t);
        MySqlTransportBindParam(bool val);
        MySqlTransportBindParam(int8_t val);
        MySqlTransportBindParam(uint8_t val);
        MySqlTransportBindParam(int16_t val);
        MySqlTransportBindParam(uint16_t val);
        MySqlTransportBindParam(int32_t val);
        MySqlTransportBindParam(uint32_t val);
        MySqlTransportBindParam(int64_t val);
        MySqlTransportBindParam(uint64_t val);
        MySqlTransportBindParam(float val);
        MySqlTransportBindParam(double val);
        MySqlTransportBindParam(const char* val);
        MySqlTransportBindParam(const std::string& val);
        MySqlTransportBindParam(std::string&& val);
        MySqlTransportBindParam(std::string_view val);
        MySqlTransportBindParam(const std::vector<unsigned char>& val);
        MySqlTransportBindParam(std::vector<unsigned char>&& val);
        MySqlTransportBindParam(const MYSQL_TIME& val);
        MySqlTransportBindParam(const std::chrono::system_clock::time_point& tp);
        MySqlTransportBindParam(const std::chrono::year_month_day& ymd);
        MySqlTransportBindParam(const std::chrono::microseconds& duration);
    };

    // --- 索引信息 (moved from mysql_transport_metadata.h for proper declaration order) ---
    struct MySqlTransportIndexColumn {
        std::string columnName;
        unsigned int sequenceInIndex = 0;
        std::optional<std::string> collation;
        std::optional<long long> cardinality;
        std::optional<unsigned int> subPart;
        bool isNullable = false;
        std::optional<std::string> expression;
    };

    struct MySqlTransportIndexInfo {
        std::string tableName;
        bool isNonUnique = true;
        std::string indexName;
        std::string indexType;
        std::vector<MySqlTransportIndexColumn> columns;
        std::string comment;
        std::string indexComment;
        bool isVisible = true;
    };

}  // namespace cpporm_mysql_transport