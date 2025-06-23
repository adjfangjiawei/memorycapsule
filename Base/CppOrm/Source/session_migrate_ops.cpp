#include <QDebug>
#include <algorithm>
#include <set>

#include "cpporm/db_manager.h"
#include "cpporm/model_base.h"
#include "cpporm/query_builder.h"
#include "cpporm/session.h"
#include "cpporm/session_migrate_priv.h"
#include "sqldriver/sql_database.h"

namespace cpporm {

    Error Session::AutoMigrate(const ModelMeta &meta) {
        if (!db_handle_.isOpen()) {
            if (!db_handle_.open()) {
                cpporm_sqldriver::SqlError err = db_handle_.lastError();
                return Error(ErrorCode::ConnectionNotOpen,
                             "Cannot AutoMigrate: Database connection is not open and "
                             "failed to open: " +
                                 err.text());
            }
        }
        if (meta.table_name.empty()) {
            return Error(ErrorCode::InvalidConfiguration, "Cannot AutoMigrate: ModelMeta has no table name.");
        }
        qInfo() << "AutoMigrate: Starting migration for table '" << QString::fromStdString(meta.table_name) << "'...";

        QString driverNameUpper = QString::fromStdString(db_handle_.driverName()).toUpper();

        Error table_err = internal::migrateCreateTable(*this, meta, driverNameUpper);
        if (table_err) {
            qWarning() << "AutoMigrate: Failed during table creation for '" << QString::fromStdString(meta.table_name) << "': " << QString::fromStdString(table_err.toString());
            return table_err;
        }
        qInfo() << "AutoMigrate: Table creation/check phase completed for '" << QString::fromStdString(meta.table_name) << "'.";

        Error column_err = internal::migrateModifyColumns(*this, meta, driverNameUpper);
        if (column_err) {
            qWarning() << "AutoMigrate: Failed during column modification for '" << QString::fromStdString(meta.table_name) << "': " << QString::fromStdString(column_err.toString());
            return column_err;
        }
        qInfo() << "AutoMigrate: Column modification phase completed for '" << QString::fromStdString(meta.table_name) << "'.";

        Error index_err = internal::migrateManageIndexes(*this, meta, driverNameUpper);
        if (index_err) {
            qWarning() << "AutoMigrate: Failed during index management for '" << QString::fromStdString(meta.table_name) << "': " << QString::fromStdString(index_err.toString());
            return index_err;
        }
        qInfo() << "AutoMigrate: Index management phase completed for '" << QString::fromStdString(meta.table_name) << "'.";

        qInfo() << "AutoMigrate: Migration successfully completed for table '" << QString::fromStdString(meta.table_name) << "'.";
        return make_ok();
    }

    Error Session::AutoMigrate(const std::vector<const ModelMeta *> &metas_vec) {
        for (const auto *m_ptr : metas_vec) {
            if (m_ptr) {
                if (auto e_obj = AutoMigrate(*m_ptr)) {
                    return e_obj;
                }
            } else {
                qWarning() << "AutoMigrate (vector): Encountered a null ModelMeta "
                              "pointer. Skipping.";
            }
        }
        qInfo() << "AutoMigrate: Batch migration completed for" << metas_vec.size() << "models.";
        return make_ok();
    }

}  // namespace cpporm