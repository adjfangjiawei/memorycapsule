// SqlDriver/Source/sql_value_lob_constructors.cpp
#include <istream>
#include <memory>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    // LOB 流构造函数
    SqlValue::SqlValue(InputStreamPtr stream_handle, SqlValueType lob_type, long long size)
        : m_value_storage(std::monostate{}),  // 先初始化为 null
          m_current_type_enum(SqlValueType::Null),
          m_lob_size_hint(size) {
        if (stream_handle) {  // 只有当流指针有效时才设置
            if (lob_type == SqlValueType::BinaryLargeObject || lob_type == SqlValueType::CharacterLargeObject) {
                m_value_storage = std::move(stream_handle);
                m_current_type_enum = lob_type;
            } else {
                // 如果 lob_type 无效，则此构造函数不应被调用，或应抛出异常/记录错误
                // 为保持健壮性，如果 stream_handle 有效但 lob_type 无效，则将其视为通用 BLOB
                m_value_storage = std::move(stream_handle);
                m_current_type_enum = SqlValueType::BinaryLargeObject;
                // 或者可以设置错误状态，但这通常在构造函数中不方便
            }
        }
        // 如果 stream_handle 为空，它将保持为 Null 状态
    }

}  // namespace cpporm_sqldriver