// SqlDriver/Source/mysql/mysql_sql_formatter.cpp
// 内容已迁移到 Transport/MySqlTransport/Source/mysql_transport_connection_utility.cpp
// 中的 MySqlTransportConnection::formatNativeValueAsLiteral 和 MySqlTransportConnection::escapeSqlIdentifier 方法。
// 此文件现在为空，可以在后续从构建系统中移除。

#include "sqldriver/mysql/mysql_driver_helper.h"  // 仍然包含以避免某些编译器/构建系统关于空翻译单元的警告

namespace cpporm_sqldriver {
    namespace mysql_helper {

        // formatMySqlValueLiteral 函数已移至 MySqlTransportConnection::formatNativeValueAsLiteral
        // escapeMySqlIdentifier 函数已移至 MySqlTransportConnection::escapeSqlIdentifier

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver