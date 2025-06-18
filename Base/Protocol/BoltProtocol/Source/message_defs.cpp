// Base/Protocol/BoltProtocol/Source/message_defs.cpp
#include "boltprotocol/message_defs.h"  // 主头文件，包含 Value, BoltList, BoltMap, PackStreamStructure 等

#include <string>
#include <variant>  // For std::visit

namespace boltprotocol {

    // 定义 DEFAULT_USER_AGENT_FORMAT_STRING
    const std::string DEFAULT_USER_AGENT_FORMAT_STRING = "BoltProtocolCppLib/0.2";  // 您可以自定义版本号

    // 实现 Value::operator==
    // 注意：这个实现假设 BoltList, BoltMap, PackStreamStructure 也有正确的 operator== 实现。
    // BoltList::operator== 和 BoltMap::operator== 已在 bolt_core_types.h 中基于其成员实现。
    // PackStreamStructure::operator== 也在 bolt_core_types.h 中基于其成员实现。
    bool operator==(const Value& lhs, const Value& rhs) {
        if (lhs.index() != rhs.index()) {
            return false;
        }

        // 使用 std::visit 来比较 variant 中实际持有的值
        return std::visit(
            [](const auto& l_val, const auto& r_val) -> bool {
                // 使用 if constexpr 来处理不同类型，特别是 shared_ptr
                using T1 = std::decay_t<decltype(l_val)>;
                using T2 = std::decay_t<decltype(r_val)>;

                if constexpr (std::is_same_v<T1, T2>) {
                    // 处理 shared_ptr 类型，需要比较它们指向的对象
                    if constexpr (std::is_same_v<T1, std::shared_ptr<BoltList>>) {
                        if (l_val && r_val) return *l_val == *r_val;  // 比较指向的对象
                        return !l_val && !r_val;                      // 两者都为空指针则相等
                    } else if constexpr (std::is_same_v<T1, std::shared_ptr<BoltMap>>) {
                        if (l_val && r_val) return *l_val == *r_val;
                        return !l_val && !r_val;
                    } else if constexpr (std::is_same_v<T1, std::shared_ptr<PackStreamStructure>>) {
                        if (l_val && r_val) return *l_val == *r_val;
                        return !l_val && !r_val;
                    } else {
                        // 对于其他非 shared_ptr 类型 (nullptr_t, bool, int64_t, double, std::string)
                        // 直接比较值
                        return l_val == r_val;
                    }
                } else {
                    // Variant 内部类型不匹配，但 index 相同，理论上不应发生
                    return false;
                }
            },
            lhs,
            rhs);
    }

}  // namespace boltprotocol