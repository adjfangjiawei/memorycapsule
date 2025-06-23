#pragma once

#include <QDateTime>
#include <QDebug>

#include "cpporm/model_base.h"
#include "cpporm/model_definition_macros.h"

enum class UserStatus : int { Pending = 0, Active = 1, Inactive = 2 };

class User : public cpporm::Model<User> {
    cpporm_DEFINE_MODEL_CLASS_NAME(User);

  public:
    // 为了兼容新的宏，PRIMARY_KEY 也需要一个空的 comment 占位符
    // (或者我们可以为 PRIMARY_KEY 创建一个不带注释的重载，但为了简单，这里直接加空字符串)
    cpporm_AUTO_INCREMENT_PRIMARY_KEY(int64_t, id, "id");

    // ***** 使用更新后的宏，第五个参数是注释 *****
    cpporm_FIELD_TYPE(std::string, name, "name", "VARCHAR(255)", "User's full name");
    cpporm_FIELD_TYPE(int, age, "age", "INT", "User's age");
    cpporm_FIELD_TYPE(std::string, email, "email", "VARCHAR(255)", "User's unique email address");
    cpporm_FIELD_ENUM(UserStatus, status, "status", "TINYINT", "User account status (0: Pending, 1: Active, 2: Inactive)");

    cpporm_TIMESTAMPS(QDateTime);
    cpporm_SOFT_DELETE(QDateTime);

    User() = default;

    void print() const {
        QString statusStr;
        switch (status) {
            case UserStatus::Pending:
                statusStr = "Pending";
                break;
            case UserStatus::Active:
                statusStr = "Active";
                break;
            case UserStatus::Inactive:
                statusStr = "Inactive";
                break;
            default:
                statusStr = "Unknown";
                break;
        }

        qDebug().nospace() << "User - ID: " << id << ", Name: " << QString::fromStdString(name) << ", Age: " << age << ", Email: " << QString::fromStdString(email) << ", Status: " << statusStr << ", Created At: " << created_at.toString(Qt::ISODateWithMs)
                           << ", Updated At: " << updated_at.toString(Qt::ISODateWithMs);
    }

    cpporm_MODEL_BEGIN(User, "users");
    cpporm_UNIQUE_INDEX("uix_users_email", "email");
    cpporm_INDEX("idx_users_name_age", "name", "age") cpporm_MODEL_END()
};