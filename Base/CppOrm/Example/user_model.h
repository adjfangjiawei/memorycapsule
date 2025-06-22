// Base/CppOrm/Include/user_model.h
#pragma once

#include <QDateTime>
#include <QDebug>  // For print() method

#include "cpporm/model_base.h"
#include "cpporm/model_definition_macros.h"

class User : public cpporm::Model<User> {
  cpporm_DEFINE_MODEL_CLASS_NAME(User)

      public :
      // Switched id to BIGINT AUTO_INCREMENT which is common for MySQL.
      // If your DB needs SERIAL or other, adjust getSqlTypeForCppType accordingly or use explicit DB type hint.
      cpporm_AUTO_INCREMENT_PRIMARY_KEY(long long, id, "id")

      // Changed TEXT to VARCHAR(255) for name and email to allow indexing without prefix length on MySQL
      cpporm_FIELD_TYPE(std::string, name, "name", "VARCHAR(255)") cpporm_FIELD_TYPE(int, age, "age", "INT") cpporm_FIELD_TYPE(std::string, email, "email", "VARCHAR(255)")

          cpporm_TIMESTAMPS(QDateTime)
      // cpporm_SOFT_DELETE(QDateTime)

      User() = default;

    void print() const {
        qDebug() << "User - ID:" << id << "Name:" << QString::fromStdString(name) << "Age:" << age << "Email:" << QString::fromStdString(email) << "Created At:" << created_at.toString(Qt::ISODateWithMs)  // Use ISODateWithMs for more precision if needed
                 << "Updated At:" << updated_at.toString(Qt::ISODateWithMs);
    }

    cpporm_MODEL_BEGIN(User, "users") cpporm_UNIQUE_INDEX("uix_users_email", "email") cpporm_INDEX("idx_users_name_age", "name", "age") cpporm_MODEL_END()
};