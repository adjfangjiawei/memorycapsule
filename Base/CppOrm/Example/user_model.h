#pragma once

#include <QDateTime>  // For timestamps

#include "cpporm/model_base.h"
#include "cpporm/model_definition_macros.h"

// 假设我们有一个名为 User 的模型
class User : public cpporm::Model<User> {
    // 必须首先定义模型类名宏
  cpporm_DEFINE_MODEL_CLASS_NAME(User)

      public :
      // 字段定义 (使用宏)
      cpporm_AUTO_INCREMENT_PRIMARY_KEY(long long, id, "id")  // 自增主键 id
      cpporm_FIELD(std::string, name, "name")                 // 姓名
      cpporm_FIELD_TYPE(int, age, "age", "INT")               // 年龄，并指定数据库类型提示
      cpporm_FIELD(std::string, email, "email")               // 邮箱

      // 时间戳 (假设使用 QDateTime)
      cpporm_TIMESTAMPS(QDateTime)
      // cpporm_SOFT_DELETE(QDateTime) // 如果需要软删除

      // 构造函数 (如果需要)
      User() = default;

    // 可以添加自定义方法
    void print() const {
        qDebug() << "User - ID:" << id << "Name:" << QString::fromStdString(name) << "Age:" << age << "Email:" << QString::fromStdString(email) << "Created At:" << created_at.toString(Qt::ISODate) << "Updated At:" << updated_at.toString(Qt::ISODate);
    }

    // 模型定义开始和结束宏
    cpporm_MODEL_BEGIN(User, "users")                      // "users" 是数据库中的表名
                                                           // 索引定义 (可选)
        cpporm_UNIQUE_INDEX("uix_users_email", "email")    // email 字段的唯一索引
        cpporm_INDEX("idx_users_name_age", "name", "age")  // name 和 age 的复合索引
        cpporm_MODEL_END()
};