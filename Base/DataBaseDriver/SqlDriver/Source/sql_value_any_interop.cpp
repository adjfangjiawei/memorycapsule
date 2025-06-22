// SqlDriver/Source/sql_value_any_interop.cpp
#include <any>
#include <variant>

#include "sqldriver/sql_value.h"

namespace cpporm_sqldriver {

    std::any SqlValue::toStdAny() const {
        if (isNull()) return std::any{};

        return std::visit(
            [](auto&& arg) -> std::any {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return std::any{};
                }
                // For types directly storable in std::any and not streams or std::any itself
                else if constexpr (!std::is_same_v<T, InputStreamPtr> &&  // InputStreamPtr is the variant type
                                   !std::is_same_v<T, std::any>) {
                    return arg;
                } else if constexpr (std::is_same_v<T, InputStreamPtr>) {
                    return std::any(arg);  // Store the shared_ptr<istream>
                } else if constexpr (std::is_same_v<T, std::any>) {
                    return arg;
                }
                return std::any{};
            },
            m_value_storage);
    }

    SqlValue SqlValue::fromStdAny(const std::any& val, SqlValueType type_hint) {
        if (!val.has_value()) return SqlValue();

        const auto& typeInfo = val.type();

        if (typeInfo == typeid(std::nullptr_t)) return SqlValue(nullptr);
        if (typeInfo == typeid(bool)) return SqlValue(std::any_cast<bool>(val));
        if (typeInfo == typeid(int8_t)) return SqlValue(std::any_cast<int8_t>(val));
        if (typeInfo == typeid(uint8_t)) return SqlValue(std::any_cast<uint8_t>(val));
        if (typeInfo == typeid(int16_t)) return SqlValue(std::any_cast<int16_t>(val));
        if (typeInfo == typeid(uint16_t)) return SqlValue(std::any_cast<uint16_t>(val));
        if (typeInfo == typeid(int32_t) || typeInfo == typeid(int)) return SqlValue(std::any_cast<int32_t>(val));
        if (typeInfo == typeid(uint32_t) || typeInfo == typeid(unsigned int)) return SqlValue(std::any_cast<uint32_t>(val));
        if (typeInfo == typeid(int64_t) || typeInfo == typeid(long long)) return SqlValue(std::any_cast<int64_t>(val));
        if (typeInfo == typeid(uint64_t) || typeInfo == typeid(unsigned long long)) return SqlValue(std::any_cast<uint64_t>(val));
        if (typeInfo == typeid(float)) return SqlValue(std::any_cast<float>(val));
        if (typeInfo == typeid(double)) return SqlValue(std::any_cast<double>(val));
        if (typeInfo == typeid(long double)) return SqlValue(std::any_cast<long double>(val));
        if (typeInfo == typeid(std::string)) return SqlValue(std::any_cast<std::string>(val), type_hint);
        if (typeInfo == typeid(const char*)) return SqlValue(std::any_cast<const char*>(val), type_hint);
        if (typeInfo == typeid(std::vector<unsigned char>)) return SqlValue(std::any_cast<std::vector<unsigned char>>(val));

        if (typeInfo == typeid(QByteArray)) return SqlValue(std::any_cast<QByteArray>(val));
        if (typeInfo == typeid(QDate)) return SqlValue(std::any_cast<QDate>(val));
        if (typeInfo == typeid(QTime)) return SqlValue(std::any_cast<QTime>(val));
        if (typeInfo == typeid(QDateTime)) return SqlValue(std::any_cast<QDateTime>(val));

        if (typeInfo == typeid(ChronoDate)) return SqlValue(std::any_cast<ChronoDate>(val));
        if (typeInfo == typeid(ChronoTime)) return SqlValue(std::any_cast<ChronoTime>(val));
        if (typeInfo == typeid(ChronoDateTime)) return SqlValue(std::any_cast<ChronoDateTime>(val));

        // 修正：检查 std::any 是否持有 InputStreamPtr
        if (typeInfo == typeid(InputStreamPtr)) {
            // type_hint 必须是 BinaryLargeObject 或 CharacterLargeObject 之一
            if (type_hint == SqlValueType::BinaryLargeObject || type_hint == SqlValueType::CharacterLargeObject) {
                return SqlValue(std::any_cast<InputStreamPtr>(val), type_hint);
            } else {
                // 如果 type_hint 无效，则可能需要默认或抛出错误
                return SqlValue(std::any_cast<InputStreamPtr>(val), SqlValueType::BinaryLargeObject);  // 默认为 BLOB
            }
        }

        SqlValue custom_val;
        custom_val.m_value_storage = val;
        custom_val.m_current_type_enum = (type_hint != SqlValueType::Unknown && type_hint != SqlValueType::Null) ? type_hint : SqlValueType::Custom;
        // 如果 type_hint 本身就是 Custom 或 Unknown，则已经是正确的
        if (custom_val.m_current_type_enum != SqlValueType::Custom && custom_val.m_current_type_enum != SqlValueType::Unknown) {
            // 如果 type_hint 是一个具体的非 Custom 类型，但上面没有匹配到，这说明 std::any 内部类型与 hint 不符
            // 这种情况下，将其标记为 Custom 可能更安全，或者根据策略报错
            // 这里保持了之前的逻辑，即如果 hint 不是 Custom/Unknown，则使用 hint
        } else if (custom_val.m_current_type_enum == SqlValueType::Unknown) {
            custom_val.updateCurrentTypeEnumFromStorage();  // 尝试从 std::any 的内容推断
        }
        return custom_val;
    }

}  // namespace cpporm_sqldriver