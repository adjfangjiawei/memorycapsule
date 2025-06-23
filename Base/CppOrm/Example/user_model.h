#pragma once

#include <QDateTime>
#include <QDebug>  // For print() method

#include "cpporm/model_base.h"
#include "cpporm/model_definition_macros.h"

// 1. 定义一个示例 enum class
// 显式指定底层类型是个好习惯，便于数据库映射
enum class UserStatus : int { Pending = 0, Active = 1, Inactive = 2 };

class User : public cpporm::Model<User> {
    cpporm_DEFINE_MODEL_CLASS_NAME(User);

  public:
    cpporm_AUTO_INCREMENT_PRIMARY_KEY(int64_t, id, "id");

    cpporm_FIELD_TYPE(std::string, name, "name", "VARCHAR(255)");
    cpporm_FIELD_TYPE(int, age, "age", "INT");
    cpporm_FIELD_TYPE(std::string, email, "email", "VARCHAR(255)");

    // 2. 使用新的宏来定义 enum class 字段
    // 第一个参数是 enum class 类型
    // 第二个参数是 C++ 成员名
    // 第三个参数是数据库列名
    // 第四个参数是数据库类型提示（对于 TINYINT、INT 等非常重要）
    cpporm_FIELD_ENUM(UserStatus, status, "status", "TINYINT");

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
    // cpporm_UNIQUE_INDEX("uix_users_email", "email");
    cpporm_INDEX("idx_users_name_age", "name", "age");
    cpporm_MODEL_END()
};