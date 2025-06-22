#include <QDebug>        // qWarning
#include <QString>       // For QString in ExecRaw interface
#include <QVariantList>  // For QVariantList in ExecRaw interface

#include "cpporm/error.h"
#include "cpporm/session.h"
#include "cpporm_sqldriver/sql_query.h"  // SqlQuery
#include "cpporm_sqldriver/sql_value.h"  // SqlValue

namespace cpporm {

    // ExecRaw 保持接收 QString 和 QVariantList 的接口以方便 Qt 用户，内部转换为 SqlDriver 类型。
    std::expected<long long, Error> Session::ExecRaw(const QString &sql_qstr, const QVariantList &args_qvariantlist) {
        std::string sql_std_str = sql_qstr.toStdString();
        if (sql_std_str.empty()) {
            return std::unexpected(Error(ErrorCode::StatementPreparationError, "Raw SQL query string is empty."));
        }

        std::vector<cpporm_sqldriver::SqlValue> args_sqlvalue;
        args_sqlvalue.reserve(args_qvariantlist.size());
        for (const QVariant &qv : args_qvariantlist) {
            // QueryBuilder::qvariantToQueryValue 应该是一个公共静态方法
            // Session::queryValueToSqlValue 也是公共静态方法
            args_sqlvalue.push_back(Session::queryValueToSqlValue(QueryBuilder::qvariantToQueryValue(qv)));
        }

        auto [sql_query_obj, exec_err] = execute_query_internal(this->db_handle_, sql_std_str, args_sqlvalue);

        if (exec_err) {
            qWarning() << "Session::ExecRaw: Execution failed for SQL:" << sql_qstr << "Args:" << args_qvariantlist << "Error:" << QString::fromStdString(exec_err.toString());
            return std::unexpected(exec_err);
        }

        long long rows_affected = sql_query_obj.numRowsAffected();

        // numRowsAffected() 对于非 DML 语句（如 SELECT）可能返回 -1，这是正常的。
        // 对于 DDL，行为可能因驱动而异。
        // if (rows_affected == -1) {
        //     qInfo() << "Session::ExecRaw: numRowsAffected is -1. SQL: " << sql_qstr
        //             << ". This may be normal for SELECT or DDL statements.";
        // }
        return rows_affected;
    }

}  // namespace cpporm