// SqlDriver/Source/sql_value_tobytes.cpp
#include <QByteArray>
#include <string>
#include <variant>
#include <vector>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    std::vector<unsigned char> SqlValue::toStdVectorUChar(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return {};

        if (std::holds_alternative<std::vector<unsigned char>>(m_value_storage)) {
            if (ok) *ok = true;
            return std::get<std::vector<unsigned char>>(m_value_storage);
        }
        // QByteArray 在构造时已转换为 std::vector<unsigned char>
        // 因此不需要 std::holds_alternative<QByteArray>

        if (std::holds_alternative<std::string>(m_value_storage)) {
            // 如果字符串代表十六进制编码的字节，这里需要解析逻辑。
            // 如果字符串本身就是字节序列（例如从某些数据库的 TEXT as BLOB），则直接转换。
            // 当前实现假定后者。
            const std::string& s = std::get<std::string>(m_value_storage);
            if (ok) *ok = true;
            return std::vector<unsigned char>(s.begin(), s.end());
        }
        if (std::holds_alternative<InputStreamPtr>(m_value_storage) && (m_current_type_enum == SqlValueType::BinaryLargeObject || m_current_type_enum == SqlValueType::ByteArray)) {
            // 从流中读取所有字节
            auto stream_ptr = std::get<InputStreamPtr>(m_value_storage);
            if (stream_ptr && stream_ptr->good()) {
                stream_ptr->seekg(0, std::ios::end);
                std::streampos fileSize = stream_ptr->tellg();
                if (fileSize > 0) {
                    std::vector<unsigned char> buffer(static_cast<size_t>(fileSize));
                    stream_ptr->seekg(0, std::ios::beg);
                    stream_ptr->read(reinterpret_cast<char*>(buffer.data()), fileSize);
                    if (stream_ptr->good() || stream_ptr->eof()) {  // 允许 eof
                        if (ok) *ok = true;
                        return buffer;
                    }
                } else if (fileSize == 0) {  // 空流
                    if (ok) *ok = true;
                    return {};
                }
            }
        }
        return {};
    }

    QByteArray SqlValue::toByteArray(bool* ok) const {
        if (ok) *ok = false;
        if (isNull()) return QByteArray();

        if (std::holds_alternative<std::vector<unsigned char>>(m_value_storage)) {
            const auto& vec = std::get<std::vector<unsigned char>>(m_value_storage);
            if (ok) *ok = true;
            return QByteArray(reinterpret_cast<const char*>(vec.data()), static_cast<int>(vec.size()));
        }
        // QByteArray 在构造时已转换为 std::vector<unsigned char>

        if (std::holds_alternative<std::string>(m_value_storage)) {
            // 如果字符串是文本，转换为UTF-8 QByteArray
            if (m_current_type_enum == SqlValueType::String || m_current_type_enum == SqlValueType::FixedString || m_current_type_enum == SqlValueType::CharacterLargeObject || m_current_type_enum == SqlValueType::Json || m_current_type_enum == SqlValueType::Xml) {
                if (ok) *ok = true;
                return QString::fromStdString(std::get<std::string>(m_value_storage)).toUtf8();
            }
            // 如果字符串意图是字节（例如，之前未被识别为ByteArray的type_hint），则直接转换
            const std::string& s = std::get<std::string>(m_value_storage);
            if (ok) *ok = true;
            return QByteArray(s.data(), static_cast<int>(s.length()));
        }
        if (std::holds_alternative<InputStreamPtr>(m_value_storage) && (m_current_type_enum == SqlValueType::BinaryLargeObject || m_current_type_enum == SqlValueType::ByteArray)) {
            auto stream_ptr = std::get<InputStreamPtr>(m_value_storage);
            if (stream_ptr && stream_ptr->good()) {
                // 尝试读取整个流到 QByteArray
                // 注意：对于非常大的流，这可能会消耗大量内存
                std::stringbuf sbuf;
                (*stream_ptr) >> &sbuf;  // 读取到 stringbuf
                if (stream_ptr->good() || stream_ptr->eof()) {
                    std::string s = sbuf.str();
                    if (ok) *ok = true;
                    return QByteArray(s.data(), static_cast<int>(s.length()));
                }
            }
        }
        return QByteArray();
    }

}  // namespace cpporm_sqldriver