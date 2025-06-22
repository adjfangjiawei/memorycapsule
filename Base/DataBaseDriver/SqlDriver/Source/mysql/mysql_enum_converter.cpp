#include "sqldriver/mysql/mysql_driver_helper.h"
// 不需要其他特定包含，因为枚举都在头文件中定义

namespace cpporm_sqldriver {
    namespace mysql_helper {

        cpporm_mysql_transport::TransactionIsolationLevel toMySqlTransportIsolationLevel(TransactionIsolationLevel driverLevel) {
            switch (driverLevel) {
                case TransactionIsolationLevel::ReadUncommitted:
                    return cpporm_mysql_transport::TransactionIsolationLevel::ReadUncommitted;
                case TransactionIsolationLevel::ReadCommitted:
                    return cpporm_mysql_transport::TransactionIsolationLevel::ReadCommitted;
                case TransactionIsolationLevel::RepeatableRead:
                    return cpporm_mysql_transport::TransactionIsolationLevel::RepeatableRead;
                case TransactionIsolationLevel::Serializable:
                    return cpporm_mysql_transport::TransactionIsolationLevel::Serializable;
                case TransactionIsolationLevel::Snapshot:
                    // MySQL 的 REPEATABLE READ 通过 MVCC 提供了快照隔离
                    return cpporm_mysql_transport::TransactionIsolationLevel::RepeatableRead;
                case TransactionIsolationLevel::Default:
                default:
                    // 让 Transport 层决定使用服务器的默认隔离级别
                    return cpporm_mysql_transport::TransactionIsolationLevel::None;
            }
        }

        TransactionIsolationLevel fromMySqlTransportIsolationLevel(cpporm_mysql_transport::TransactionIsolationLevel transportLevel) {
            switch (transportLevel) {
                case cpporm_mysql_transport::TransactionIsolationLevel::ReadUncommitted:
                    return TransactionIsolationLevel::ReadUncommitted;
                case cpporm_mysql_transport::TransactionIsolationLevel::ReadCommitted:
                    return TransactionIsolationLevel::ReadCommitted;
                case cpporm_mysql_transport::TransactionIsolationLevel::RepeatableRead:
                    // MySQL 的 REPEATABLE READ 行为上是快照隔离
                    return TransactionIsolationLevel::RepeatableRead;  // 或者 Snapshot，取决于您想如何映射
                case cpporm_mysql_transport::TransactionIsolationLevel::Serializable:
                    return TransactionIsolationLevel::Serializable;
                case cpporm_mysql_transport::TransactionIsolationLevel::None:  // 表示 Transport 层未指定或未知
                default:
                    return TransactionIsolationLevel::Default;  // 映射回 Driver 的默认/未知状态
            }
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver