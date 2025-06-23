#ifndef cpporm_MODEL_TYPES_H
#define cpporm_MODEL_TYPES_H

#include <cstdint>

namespace cpporm {

    // --- Association Related Enums and Structs ---
    enum class AssociationType { None, HasOne, BelongsTo, HasMany, ManyToMany };

    // --- Field Flags ---
    enum class FieldFlag : uint32_t { None = 0, PrimaryKey = 1 << 0, AutoIncrement = 1 << 1, NotNull = 1 << 2, Unique = 1 << 3, HasDefault = 1 << 4, Indexed = 1 << 5, CreatedAt = 1 << 6, UpdatedAt = 1 << 7, DeletedAt = 1 << 8, Association = 1 << 9 };

    inline FieldFlag operator|(FieldFlag a, FieldFlag b) {
        return static_cast<FieldFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline FieldFlag operator&(FieldFlag a, FieldFlag b) {
        return static_cast<FieldFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    inline FieldFlag &operator|=(FieldFlag &a, FieldFlag b) {
        a = a | b;
        return a;
    }
    inline bool has_flag(FieldFlag flags, FieldFlag flag_to_check) {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag_to_check)) != 0;
    }

}  // namespace cpporm

#endif  // cpporm_MODEL_TYPES_H