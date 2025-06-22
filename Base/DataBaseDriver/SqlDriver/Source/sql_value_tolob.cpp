// SqlDriver/Source/sql_value_tolob.cpp
#include <sstream>
#include <variant>

#include "sqldriver/sql_value.h"
// QByteArray 的包含已移至 sql_value.h 或其他相关文件

namespace cpporm_sqldriver {

    SqlValue::InputStreamPtr SqlValue::toInputStream(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return nullptr;

        if (std::holds_alternative<InputStreamPtr>(m_value_storage)) {
            // 只有当类型确实是 BLOB 或 CLOB 时才返回，否则可能是意外的流
            if (m_current_type_enum == SqlValueType::BinaryLargeObject || m_current_type_enum == SqlValueType::CharacterLargeObject) {
                if (ok) *ok = true;
                return std::get<InputStreamPtr>(m_value_storage);
            }
        }
        // 从 std::vector<unsigned char> (代表 ByteArray 或内部 BLOB 数据) 创建流
        if (std::holds_alternative<std::vector<unsigned char>>(m_value_storage)) {
            const auto& vec = std::get<std::vector<unsigned char>>(m_value_storage);
            auto ss = std::make_shared<std::stringstream>();  // 使用 stringstream 作为通用的 istream
            // stringstream 需要 char*, 所以进行转换
            ss->write(reinterpret_cast<const char*>(vec.data()), static_cast<std::streamsize>(vec.size()));
            if (ss->good()) {  // 检查流状态
                if (ok) *ok = true;
                return ss;
            }
        }
        // 从 std::string (代表 CLOB 数据或可转换为流的字符串) 创建流
        if (std::holds_alternative<std::string>(m_value_storage)) {
            auto ss = std::make_shared<std::stringstream>();
            *ss << std::get<std::string>(m_value_storage);
            if (ss->good()) {
                if (ok) *ok = true;
                return ss;
            }
        }
        return nullptr;
    }

    // 旧的 BlobInputStream 和 ClobInputStream typedef 已被 InputStreamPtr 取代，
    // 因此 toBlobInputStream 和 toClobInputStream 方法应在 sql_value.h 中移除或调整为调用 toInputStream 并检查类型。
    // 这里假设它们已从头文件中移除，或者如果保留，它们的实现将如下：
    /*
    SqlValue::InputStreamPtr SqlValue::toBlobInputStream(bool* ok) const {
        if (m_current_type_enum == SqlValueType::BinaryLargeObject || m_current_type_enum == SqlValueType::ByteArray) {
            return toInputStream(ok); // 调用通用方法
        }
        if (ok) *ok = false;
        return nullptr;
    }

    SqlValue::InputStreamPtr SqlValue::toClobInputStream(bool* ok) const {
         if (m_current_type_enum == SqlValueType::CharacterLargeObject || m_current_type_enum == SqlValueType::String || m_current_type_enum == SqlValueType::FixedString) {
            return toInputStream(ok); // 调用通用方法
        }
        if (ok) *ok = false;
        return nullptr;
    }
    */

}  // namespace cpporm_sqldriver