#ifndef cpporm_MODEL_H
#define cpporm_MODEL_H

#include <string>
#include <vector>
#include <any> // For generic field values, or consider std::variant
#include <map>

// Forward declaration if DB operations are directly tied to model methods
// namespace cpporm { class Database; }

namespace cpporm {

// Forward declaration for Session/DB context if models don't own a DB pointer directly
class Session; // Or class DB; (representing an active DB session/transaction context)


// A very basic concept for a model field attribute (e.g., primary key, nullable, etc.)
// This will be expanded significantly.
enum class FieldAttribute {
    None,
    PrimaryKey,
    AutoIncrement,
    NotNull,
    Unique
    // etc.
};

struct FieldDefinition {
    std::string name;
    std::string type_name; // e.g., "INT", "VARCHAR(255)" - for schema generation or reflection
    // std::type_index cpp_type; // For type checking
    std::vector<FieldAttribute> attributes;
    // Potentially default value, etc.
};


// Base class for all ORM models
class Model {
public:
    virtual ~Model() = default;

    // Method to get the table name for the model
    // GORM uses struct tags or conventions; C++ needs a different approach.
    // Pure virtual makes subclasses implement it.
    [[nodiscard]] virtual std::string getTableName() const = 0;

    // Method to get primary key field name(s)
    // For simplicity, starting with a single string, could be a vector for composite keys.
    [[nodiscard]] virtual std::string getPrimaryKeyName() const { return "id"; } // Default

    // Placeholder for getting/setting field values by name
    // This is a complex area: type safety, reflection-like capabilities.
    // virtual std::any getField(const std::string& fieldName) const = 0;
    // virtual void setField(const std::string& fieldName, const std::any& value) = 0;

    // Placeholder for schema definition, could be static or virtual
    // static virtual std::vector<FieldDefinition> defineSchema() = 0; // Static virtual not allowed
    // This needs a pattern like CRTP or a registration mechanism.
    // For now, each model might just "know" its fields.

protected:
    // Models might not directly hold a DB connection pointer.
    // Operations would typically go through a `Session` or `DB` object
    // to which the model instance is passed.
    // e.g., session.Save(myUserInstance);
    //
    // If GORM-style chainable methods directly on model instances are desired (user.DB.Where(...)),
    // then a (possibly non-owning) pointer/reference to the DB context is needed.
    // This is a design choice with trade-offs.

    // Example: if models were to manage their own state persistence directly
    // cpporm::Database* db_ = nullptr; // Non-owning, set by a session or context

    // For now, let's keep models as Plain Old Data Objects (PODs) as much as possible,
    // with persistence logic handled by other classes (Repository, Session, QueryBuilder).

    // Primary key value (example, assuming 'id' and type int64_t)
    // This will be made generic later.
    // int64_t id_ = 0;
};

} // namespace cpporm

#endif // cpporm_MODEL_H