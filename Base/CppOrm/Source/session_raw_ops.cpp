// cpporm/session_raw_ops.cpp
#include "cpporm/error.h"
#include "cpporm/session.h"

#include <QDebug>    // For qWarning
#include <QSqlError> // For QSqlError details
#include <QSqlQuery>
#include <QString>
#include <QVariantList>

namespace cpporm {

std::expected<long long, Error> Session::ExecRaw(const QString &sql,
                                                 const QVariantList &args) {
  if (sql.isEmpty()) {
    return std::unexpected(Error(ErrorCode::StatementPreparationError,
                                 "Raw SQL query string is empty."));
  }
  // `execute_query_internal` is a static private helper method of Session,
  // defined in session.cpp
  auto [query_obj, exec_err] =
      execute_query_internal(this->db_handle_, sql, args);

  if (exec_err) {
    qWarning() << "Session::ExecRaw: Execution failed for SQL:" << sql
               << "Args:" << args
               << "Error:" << QString::fromStdString(exec_err.toString());
    return std::unexpected(exec_err);
  }

  // QSqlQuery::numRowsAffected() behavior:
  // - For DML (INSERT, UPDATE, DELETE): Returns the number of rows affected.
  // - For SELECT: Behavior is driver-dependent. Some return -1, some the number
  // of rows fetched so far.
  // - For DDL (CREATE, ALTER, DROP): Behavior is driver-dependent. Often -1 or
  // 0. GORM's Exec() is typically for DML/DDL and returns RowsAffected. If the
  // user intends to fetch rows from a SELECT, they should use a different raw
  // query method (e.g., one that returns QSqlQuery or maps to models/structs).
  long long rows_affected = query_obj.numRowsAffected();

  // It's possible that for some DDL statements, numRowsAffected might be -1
  // even if successful. We might want to consider query.isActive() as a sign of
  // success for DDL if numRowsAffected is inconclusive. However, for a generic
  // ExecRaw, returning numRowsAffected is the most common ORM behavior.
  if (rows_affected == -1) {
    // This could be a SELECT statement, or a DDL statement on some drivers.
    // Or it could be an actual error state not caught by query.exec() returning
    // false (rare). If it's a non-DML statement that succeeded, -1 might be
    // "normal". We can't easily distinguish without knowing the type of SQL
    // statement. For now, we return it as is. The user of ExecRaw needs to be
    // aware of this. qInfo() << "Session::ExecRaw: numRowsAffected is -1. SQL:
    // " << sql << ". This may be normal for SELECT or DDL statements.";
  }

  return rows_affected;
}

} // namespace cpporm