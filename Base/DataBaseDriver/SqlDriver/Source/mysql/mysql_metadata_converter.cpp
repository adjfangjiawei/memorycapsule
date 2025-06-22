// SqlDriver/Source/mysql/mysql_metadata_converter.cpp
#include "sqldriver/mysql/mysql_driver_helper.h"
#include "sqldriver/sql_field.h"
#include "sqldriver/sql_index.h"
#include "sqldriver/sql_record.h"
#include "sqldriver/sql_value.h"  // For SqlValueType

// Make sure these are included for the transport types
#include "cpporm_mysql_transport/mysql_transport_types.h"

namespace cpporm_sqldriver {
    namespace mysql_helper {

        // --- Implementation for metaToSqlField ---
        SqlField metaToSqlField(const ::cpporm_mysql_transport::MySqlTransportFieldMeta& transportMeta) {
            SqlField field(transportMeta.name,
                           mySqlColumnTypeToSqlValueType(transportMeta.native_type_id, transportMeta.flags),  // Assuming this helper exists and is correct
                           "");                                                                               // SqlField's db_type_name will be set if parseMySQLTypeStringInternal in transport layer was more detailed

            // Basic properties from transportMeta
            field.setLength(static_cast<int>(transportMeta.length));       // MySQL length might be bigger
            field.setPrecision(static_cast<int>(transportMeta.decimals));  // For numeric types, decimals is precision for MySQL
                                                                           // Scale would be part of decimals if it's like DECIMAL(P,S)
                                                                           // For simplicity, using decimals as precision.
                                                                           // A more detailed parsing of original type string is needed for true P,S.
            field.setRequiredStatus(transportMeta.isNotNull() ? RequiredStatus::Required : RequiredStatus::Optional);
            field.setAutoValue(transportMeta.isAutoIncrement());
            field.setPrimaryKeyPart(transportMeta.isPrimaryKey());
            // transportMeta doesn't directly tell if it's FK, read-only, or generated in a simple way.
            // These might need more complex schema introspection or be set based on convention/hints.

            // Set original database type name if available from transportMeta's parsing of type string
            // (Assuming MySqlTransportFieldMeta might store the full original type string if needed)
            // For now, we derive a generic SqlValueType. SqlField's databaseTypeName could be set
            // if MySqlTransportFieldMeta retained the raw type string.

            // Example for default value (if transportMeta.default_value is MySqlNativeValue)
            if (!transportMeta.default_value.is_null()) {
                field.setDefaultValue(mySqlNativeValueToSqlValue(transportMeta.default_value));  // Assuming this helper exists
            }

            // Set flags in SqlField based on transportMeta.flags
            // (This is a bit redundant if SqlField properties are set directly,
            //  but good for completeness if SqlField has its own internal flag system)

            return field;
        }

        // --- Implementation for metasToSqlRecord ---
        SqlRecord metasToSqlRecord(const std::vector<::cpporm_mysql_transport::MySqlTransportFieldMeta>& transportMetas) {
            SqlRecord record;
            for (const auto& tm : transportMetas) {
                record.append(metaToSqlField(tm));
            }
            return record;
        }

        // --- Implementation for metaToSqlIndex ---
        SqlIndex metaToSqlIndex(const ::cpporm_mysql_transport::MySqlTransportIndexInfo& transportIndexInfo) {
            SqlIndex index(transportIndexInfo.indexName, transportIndexInfo.tableName);
            index.setUnique(!transportIndexInfo.isNonUnique);
            index.setPrimaryKey(transportIndexInfo.indexName == "PRIMARY");  // MySQL convention
            index.setTypeMethod(transportIndexInfo.indexType);

            for (const auto& tCol : transportIndexInfo.columns) {
                IndexColumnDefinition colDef;
                colDef.fieldName = tCol.columnName;
                // MySQL SHOW INDEX doesn't directly give ASC/DESC for columns, typically ASC
                colDef.sortOrder = IndexSortOrder::Default;  // Or Ascending
                if (tCol.expression.has_value()) {
                    colDef.expression = tCol.expression;
                    index.setFunctional(true);
                }
                // Other properties like opClass, subPart, collation can be mapped if needed
                index.appendColumn(colDef);
            }
            // transportIndexInfo.comment and indexComment can be stored if SqlIndex supports them.
            return index;
        }

        // --- Implementation for metasToSqlIndexes ---
        std::vector<SqlIndex> metasToSqlIndexes(const std::vector<::cpporm_mysql_transport::MySqlTransportIndexInfo>& transportIndexInfos) {
            std::vector<SqlIndex> indexes;
            indexes.reserve(transportIndexInfos.size());
            for (const auto& ti : transportIndexInfos) {
                indexes.push_back(metaToSqlIndex(ti));
            }
            return indexes;
        }

    }  // namespace mysql_helper
}  // namespace cpporm_sqldriver