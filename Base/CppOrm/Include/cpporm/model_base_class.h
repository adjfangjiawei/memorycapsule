#ifndef cpporm_MODEL_BASE_CLASS_H
#define cpporm_MODEL_BASE_CLASS_H

#include <QDebug>
#include <any>
#include <map>
#include <string>

#include "cpporm/error.h"
#include "cpporm/model_meta.h"

namespace cpporm {

    // Forward declarations
    class Session;

    // --- ModelBase Definition ---
    class ModelBase {
      public:
        virtual ~ModelBase() = default;

        [[nodiscard]] virtual const ModelMeta &_getOwnModelMeta() const = 0;
        [[nodiscard]] virtual std::string _getTableName() const = 0;
        [[nodiscard]] virtual std::map<std::string, std::any> _getPrimaryKeys() const;

        bool _is_persisted = false;

        std::any getFieldValue(const std::string &cpp_field_name) const;
        Error setFieldValue(const std::string &cpp_field_name, const std::any &value);

        virtual Error beforeCreate(Session & /*session*/) {
            return make_ok();
        }
        virtual Error afterCreate(Session & /*session*/) {
            return make_ok();
        }
        virtual Error beforeUpdate(Session & /*session*/) {
            return make_ok();
        }
        virtual Error afterUpdate(Session & /*session*/) {
            return make_ok();
        }
        virtual Error beforeSave(Session & /*session*/) {
            return make_ok();
        }
        virtual Error afterSave(Session & /*session*/) {
            return make_ok();
        }
        virtual Error beforeDelete(Session & /*session*/) {
            return make_ok();
        }
        virtual Error afterDelete(Session & /*session*/) {
            return make_ok();
        }
        virtual Error afterFind(Session & /*session*/) {
            return make_ok();
        }
    };

    inline std::any ModelBase::getFieldValue(const std::string &cpp_field_name) const {
        const ModelMeta &meta = this->_getOwnModelMeta();
        const FieldMeta *field = meta.findFieldByCppName(cpp_field_name);
        if (!field) {
            const AssociationMeta *assoc = meta.findAssociationByCppName(cpp_field_name);
            if (assoc) {
                qWarning() << "cpporm ModelBase::getFieldValue: Attempted to get association "
                              "collection or object '"
                           << cpp_field_name.c_str() << "' via generic getter. Access the member directly after Preload.";
                return std::any{};
            }
            qWarning() << "cpporm ModelBase::getFieldValue: Field or Association "
                          "placeholder '"
                       << cpp_field_name.c_str() << "' not found in meta for table " << QString::fromStdString(meta.table_name);
            return std::any{};
        }
        if (!field->getter) {
            qWarning() << "cpporm ModelBase::getFieldValue: Getter not found or "
                          "not finalized for field '"
                       << cpp_field_name.c_str() << "' in table " << QString::fromStdString(meta.table_name);
            return std::any{};
        }
        return field->getter(this);
    }

    inline Error ModelBase::setFieldValue(const std::string &cpp_field_name, const std::any &value) {
        const ModelMeta &meta = this->_getOwnModelMeta();
        const FieldMeta *field = meta.findFieldByCppName(cpp_field_name);
        if (!field) {
            const AssociationMeta *assoc = meta.findAssociationByCppName(cpp_field_name);
            if (assoc) {
                qWarning() << "cpporm ModelBase::setFieldValue: Attempted to set "
                              "association collection or object '"
                           << cpp_field_name.c_str()
                           << "' via generic setter. This is usually handled by Preload "
                              "setters or direct member assignment if applicable.";
                return Error(ErrorCode::MappingError, "Cannot set association via generic setFieldValue.");
            }
            qWarning() << "cpporm ModelBase::setFieldValue: Field or Association "
                          "placeholder '"
                       << cpp_field_name.c_str() << "' not found in meta for table " << QString::fromStdString(meta.table_name);
            return Error(ErrorCode::MappingError, "Field or Association placeholder " + cpp_field_name + " not found.");
        }
        if (!field->setter) {
            qWarning() << "cpporm ModelBase::setFieldValue: Setter not found or "
                          "not finalized for field '"
                       << cpp_field_name.c_str() << "' in table " << QString::fromStdString(meta.table_name);
            return Error(ErrorCode::MappingError, "Setter for " + cpp_field_name + " not found/finalized.");
        }
        try {
            field->setter(this, value);
        } catch (const std::bad_any_cast &e) {
            qWarning() << "cpporm ModelBase::setFieldValue: Bad_any_cast for field '" << cpp_field_name.c_str() << "' (table: " << QString::fromStdString(meta.table_name) << ", expected C++ type: " << field->cpp_type.name()
                       << ", value provided type: " << (value.has_value() ? value.type().name() : "empty_any") << "): " << e.what();
            return Error(ErrorCode::MappingError, "Type mismatch for field " + cpp_field_name + ": " + e.what());
        } catch (const std::exception &e) {
            qWarning() << "cpporm ModelBase::setFieldValue: Exception while setting field '" << cpp_field_name.c_str() << "' (table: " << QString::fromStdString(meta.table_name) << "): " << e.what();
            return Error(ErrorCode::MappingError, "Setter failed for field " + cpp_field_name + ": " + e.what());
        }
        return make_ok();
    }

    inline std::map<std::string, std::any> ModelBase::_getPrimaryKeys() const {
        std::map<std::string, std::any> pks;
        const auto &meta = this->_getOwnModelMeta();
        for (const auto &pk_db_name : meta.primary_keys_db_names) {
            const FieldMeta *fm = meta.findFieldByDbName(pk_db_name);
            if (fm && fm->getter) {
                try {
                    pks[pk_db_name] = fm->getter(this);
                } catch (const std::exception &e) {
                    qWarning() << "cpporm ModelBase::_getPrimaryKeys: Getter failed for PK field " << pk_db_name.c_str() << " on table " << QString::fromStdString(meta.table_name) << ": " << e.what();
                }
            } else {
                qWarning() << "cpporm ModelBase::_getPrimaryKeys: Primary key field meta or "
                              "getter not found for DB name: "
                           << pk_db_name.c_str() << " on table " << QString::fromStdString(meta.table_name);
            }
        }
        return pks;
    }

}  // namespace cpporm

#endif  // cpporm_MODEL_BASE_CLASS_H